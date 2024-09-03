#define MADRONA_MWGPU_MAX_BLOCKS_PER_SM 4

#include <madrona/bvh.hpp>
#include <madrona/mesh_bvh.hpp>
#include <madrona/mw_gpu/host_print.hpp>

#define LOG(...) mwGPU::HostPrint::log(__VA_ARGS__)

using namespace madrona;
using namespace madrona::math;
using namespace madrona::render;

namespace sm {
// Only shared memory to be used
extern __shared__ uint8_t buffer[];
}

extern "C" __constant__ BVHParams bvhParams;

inline Vector3 lighting(Vector3 diffuse,
                        Vector3 normal,
                        Vector3 raydir,
                        float roughness,
                        float metalness)
{
    constexpr float ambient = 0.4;
    Vector3 lightDir = Vector3{0.5,0.5,0};
    return (fminf(fmaxf(normal.dot(lightDir),0.f)+ambient, 1.0f)) * diffuse;
}

inline Vector3 calculateOutRay(PerspectiveCameraData *view_data,
                               uint32_t pixel_x, uint32_t pixel_y)
{
    Quat rot = view_data->rotation;
    Vector3 ray_start = view_data->position;
    Vector3 look_at = rot.inv().rotateVec({0, 1, 0});
 
    // const float h = tanf(theta / 2);
    const float h = 1.0f / (-view_data->yScale);

    const auto viewport_height = 2 * h;
    const auto viewport_width = viewport_height;
    const auto forward = look_at.normalize();

    auto u = rot.inv().rotateVec({1, 0, 0});
    auto v = cross(forward, u).normalize();

    auto horizontal = u * viewport_width;
    auto vertical = v * viewport_height;

    auto lower_left_corner = ray_start - horizontal / 2 - vertical / 2 + forward;
  
    float pixel_u = ((float)pixel_x + 0.5f) / (float)bvhParams.renderOutputResolution;
    float pixel_v = ((float)pixel_y + 0.5f) / (float)bvhParams.renderOutputResolution;

    Vector3 ray_dir = lower_left_corner + pixel_u * horizontal + 
        pixel_v * vertical - ray_start;
    ray_dir = ray_dir.normalize();

    return ray_dir;
}

struct TraceResult {
    bool hit;
    Vector3 color;
    float depth;
};

struct TraceInfo {
    Vector3 rayOrigin;
    Vector3 rayDirection;
    float tMin;
    float tMax;
};

struct TraceWorldInfo {
    QBVHNode *nodes;
    InstanceData *instances;
};

enum class GroupType : uint8_t {
    TopLevel,
    BottomLevel,
    Triangles,
    None
};

using NodeGroup = uint64_t;

// Node group just has 8 present bits
static NodeGroup encodeNodeGroup(
        uint32_t node_idx,
        uint8_t present_bits,
        GroupType type)
{
    return (uint64_t)node_idx |
           ((uint64_t)present_bits << 32) |
           ((uint64_t)types << 62;
}

// Triangle group will have more present bits
static NodeGroup encodeTriangleGroup(
        uint32_t node_idx,
        uint32_t present_bits,
        GroupType type)
{
    return (uint64_t)node_idx |
           ((uint64_t)present_bits << 32) |
           ((uint64_t)types << 62;
}

static NodeGroup invalidNodeGroup()
{
    return encodeNodeGroup(0xFFFF'FFFF, 0, GroupType::None);
}

static NodeGroup getRootGroup(TraceWorldInfo world_info)
{
    uint32_t children_count = world_info.nodes[0].numChildren;
    uint8_t present_bits = (uint8_t)((1 << children_count) - 1);
    return encodeNodeGroup(0, present_bits, GroupType::TopLevel);
}

static GroupType getGroupType(NodeGroup grp)
{
    return (GroupType)((grp >> 62) & 0b11);
}

static NodeGroup unsetPresentBit(NodeGroup grp, uint32_t idx)
{
    grp &= ~(1 << (idx + 32));
}

static uint32_t getPresentBits(NodeGroup grp);
{
    return (uint32_t)((grp >> 32) & 0xFF),
}

static uint32_t getTrianglesPresentBits(NodeGroup grp);
{
    // 24 bits used for triangle presence
    return (uint32_t)((grp >> 32) & ((1 << 24) - 1)),
}

static __device__ TraceResult traceRay(
    TraceInfo trace_info,
    TraceWorldInfo world_info)
{
    TraceResult result = {
        .hit = false
    };

    NodeGroup stack[64];
    uint32_t stack_size = 0;

    NodeGroup current_grp = getRootGroup(world_info);
    NodeGroup triangle_grp = NodeGroup::invalid();

    for (;;) {
        if (getGroupType(current_grp) != GroupType::Triangles) {
            // TODO: Make sure to have the node traversal order
            // sorted according to the ray direction.
            // NOTE: This should never underflow
            uint32_t child_idx = __ffs(getPresentBits(current_grp)) - 1;
            current_grp.presentBits &= ~(1 << child_idx);

            if (current_grp.presentBits != 0)
                stack[stack_size++] = encodeNodeGroup(current_grp);

            // Intersect with the children of the child to get a new node group
            // and calculate the present bits according to which were
            // intersected
            // TODO: Differentiate between TLAS and BLAS. For now, we are just
            // rewriting the TLAS tracing code for testing.
            uint32_t child_node_idx =
                world_info.nodes[current_grp.nodeIndex].childrenIdx[child_idx];
            
            QBVHNode new_current = world_info.nodes[child_node_idx];

            Vector3 dir_quant = {
                __uint_as_float((node.expX + 127) << 23) * inv_ray_d.d0,
                __uint_as_float((node.expY + 127) << 23) * inv_ray_d.d1,
                __uint_as_float((node.expZ + 127) << 23) * inv_ray_d.d2,
            };

            Vector3 origin_quant = {
                (node.minPoint.x - trace_info.rayOrigin.x) * inv_ray_d.d0,
                (node.minPoint.y - trace_info.rayOrigin.y) * inv_ray_d.d1,
                (node.minPoint.z - trace_info.rayOrigin.z) * inv_ray_d.d2,            
            };

            uint8_t present_bits = 0;

            for (int i = 0; i < new_current.numChildren; ++i) {
                Vector3 t_near3 = {
                    node.qMinX[i] * dir_quant.x + origin_quant.x,
                    node.qMinY[i] * dir_quant.y + origin_quant.y,
                    node.qMinZ[i] * dir_quant.z + origin_quant.z,
                };

                Vector3 t_far3 = {
                    node.qMaxX[i] * dir_quant.x + origin_quant.x,
                    node.qMaxY[i] * dir_quant.y + origin_quant.y,
                    node.qMaxZ[i] * dir_quant.z + origin_quant.z,
                };

                float t_near = fmaxf(fminf(t_near3.x, t_far3.x), 
                                     fmaxf(fminf(t_near3.y, t_far3.y),
                                           fmaxf(fminf(t_near3.z, t_far3.z), 
                                                 0.f)));

                float t_far = fminf(fmaxf(t_far3.x, t_near3.x), 
                                    fminf(fmaxf(t_far3.y, t_near3.y),
                                          fminf(fmaxf(t_far3.z, t_near3.z), 
                                                trace_info.tMax)));

                if (t_near <= t_far) {
                    // Intersection has happened, change the current_grp
                    present_bits |= (1 << i);
                }
            }

            current_grp = NodeGroup {
                .nodeIndex = child_node_idx,
                .presentBits = present_bits,
                .type = GroupType::TopLevel
            };

            // The intersect children might lead to triangles - in which case
            // we need to fill in triangle_grp
        } else {
            triangle_grp = current_grp;
            current_grp = NodeGroup::invalid();
        }

        while (getTrianglesPresentBits(triangle_grp) != 0) {

        }
    }
}







static __device__ TraceResult traceRayTLAS(
        TraceInfo trace_info,
        TraceWorldInfo world_info)
{
    static constexpr float inv_epsilon = 100000.0f;

    // Stack (needs to be declared locally due to a weird CUDA compiler bug).
    int32_t stack[32];
    int32_t stack_size = 0;
    stack[stack_size++] = 1;

    MeshBVH::HitInfo closest_hit_info = {};

    Diag3x3 inv_ray_d = {
        copysignf(trace_info.rayDirection.x == 0.f ? inv_epsilon : 
                1.f / trace_info.rayDirection.x, trace_info.rayDirection.x),
        copysignf(trace_info.rayDirection.y == 0.f ? inv_epsilon : 
                1.f / trace_info.rayDirection.y, trace_info.rayDirection.y),
        copysignf(trace_info.rayDirection.z == 0.f ? inv_epsilon : 
                1.f / trace_info.rayDirection.z, trace_info.rayDirection.z),
    };

    TraceResult result = {
        .hit = false
    };

    while (stack_size > 0) {
        int32_t node_idx = stack[--stack_size] - 1;
        QBVHNode node = world_info.nodes[node_idx];

        Vector3 dir_quant = {
            __uint_as_float((node.expX + 127) << 23) * inv_ray_d.d0,
            __uint_as_float((node.expY + 127) << 23) * inv_ray_d.d1,
            __uint_as_float((node.expZ + 127) << 23) * inv_ray_d.d2,
        };

        Vector3 origin_quant = {
            (node.minPoint.x - trace_info.rayOrigin.x) * inv_ray_d.d0,
            (node.minPoint.y - trace_info.rayOrigin.y) * inv_ray_d.d1,
            (node.minPoint.z - trace_info.rayOrigin.z) * inv_ray_d.d2,            
        };
        
        for (int i = 0; i < node.numChildren; ++i) {
            Vector3 t_near3 = {
                node.qMinX[i] * dir_quant.x + origin_quant.x,
                node.qMinY[i] * dir_quant.y + origin_quant.y,
                node.qMinZ[i] * dir_quant.z + origin_quant.z,
            };

            Vector3 t_far3 = {
                node.qMaxX[i] * dir_quant.x + origin_quant.x,
                node.qMaxY[i] * dir_quant.y + origin_quant.y,
                node.qMaxZ[i] * dir_quant.z + origin_quant.z,
            };

            float t_near = fmaxf(fminf(t_near3.x, t_far3.x), 
                                 fmaxf(fminf(t_near3.y, t_far3.y),
                                       fmaxf(fminf(t_near3.z, t_far3.z), 
                                             0.f)));

            float t_far = fminf(fmaxf(t_far3.x, t_near3.x), 
                                fminf(fmaxf(t_far3.y, t_near3.y),
                                      fminf(fmaxf(t_far3.z, t_near3.z), 
                                            trace_info.tMax)));

            if (t_near <= t_far) {
                if (node.childrenIdx[i] < 0) {
                    // This child is a leaf.
                    int32_t instance_idx = (int32_t)(-node.childrenIdx[i] - 1);

                    MeshBVH *model_bvh = bvhParams.bvhs +
                        world_info.instances[instance_idx].objectID;

                    InstanceData *instance_data =
                        &world_info.instances[instance_idx];

                    // Skip the instance if it doesn't have any scale
                    if (instance_data->scale.d0 == 0.0f &&
                        instance_data->scale.d1 == 0.0f &&
                        instance_data->scale.d2 == 0.0f) {
                        continue;
                    }

                    Vector3 txfm_ray_o = instance_data->scale.inv() *
                        instance_data->rotation.inv().rotateVec(
                            (trace_info.rayOrigin - instance_data->position));

                    Vector3 txfm_ray_d = instance_data->scale.inv() *
                        instance_data->rotation.inv().rotateVec(
                                trace_info.rayDirection);

                    float t_scale = txfm_ray_d.length();

                    txfm_ray_d /= t_scale;

                    MeshBVH::HitInfo hit_info = {};

                    bool leaf_hit = model_bvh->traceRay(
                            txfm_ray_o, txfm_ray_d, &hit_info, 
                            stack, stack_size, trace_info.tMax * t_scale);

                    if (leaf_hit) {
                        result.hit = true;

                        trace_info.tMax = hit_info.tHit / t_scale;

                        closest_hit_info = hit_info;
                        closest_hit_info.normal = 
                            instance_data->rotation.rotateVec(
                                instance_data->scale * closest_hit_info.normal);

                        closest_hit_info.normal = 
                            closest_hit_info.normal.normalize();

                        closest_hit_info.bvh = model_bvh;
                    }
                } else {
                    stack[stack_size++] = node.childrenIdx[i];
                }
            }
        }
    }

    if (result.hit) {
        if (bvhParams.raycastRGBD) {
            int32_t material_idx = 
                closest_hit_info.bvh->getMaterialIDX(closest_hit_info);

            Material *mat = &bvhParams.materials[material_idx];

            Vector3 color = {mat->color.x, mat->color.y, mat->color.z};

            if (mat->textureIdx != -1) {
                cudaTextureObject_t *tex = &bvhParams.textures[mat->textureIdx];

                float4 sampled_color = tex2D<float4>(*tex,
                    closest_hit_info.uv.x, closest_hit_info.uv.y);

                Vector3 tex_color = { sampled_color.x,
                                      sampled_color.y,
                                      sampled_color.z };

                color.x *= tex_color.x;
                color.y *= tex_color.y;
                color.z *= tex_color.z;
            }

            result.color = lighting(
                    color, closest_hit_info.normal, 
                    trace_info.rayDirection, 1, 1);
        }
        
        result.depth = trace_info.tMax;
    }

    return result;
}

static __device__ void writeRGB(uint32_t pixel_byte_offset,
                           const Vector3 &color)
{
    uint8_t *rgb_out = (uint8_t *)bvhParams.rgbOutput + pixel_byte_offset;

    *(rgb_out + 0) = (color.x) * 255;
    *(rgb_out + 1) = (color.y) * 255;
    *(rgb_out + 2) = (color.z) * 255;
    *(rgb_out + 3) = 255;
}

static __device__ void writeDepth(uint32_t pixel_byte_offset,
                             float depth)
{
    float *depth_out = (float *)
        ((uint8_t *)bvhParams.depthOutput + pixel_byte_offset);
    *depth_out = depth;
}

extern "C" __global__ void bvhRaycastEntry()
{
    uint32_t pixels_per_block = blockDim.x;

    const uint32_t total_num_views = bvhParams.internalData->numViews;

    // This is the number of views currently being processed.
    const uint32_t num_resident_views = gridDim.x;

    // This is the offset into the resident view processors that we are
    // currently in.
    const uint32_t resident_view_offset = blockIdx.x;

    uint32_t current_view_offset = resident_view_offset;

    uint32_t bytes_per_view =
        bvhParams.renderOutputResolution * bvhParams.renderOutputResolution * 4;

    uint32_t num_processed_pixels = 0;

    uint32_t pixel_x = blockIdx.y * pixels_per_block + threadIdx.x;
    uint32_t pixel_y = blockIdx.z * pixels_per_block + threadIdx.y;

    while (current_view_offset < total_num_views) {
        // While we still have views to generate, trace.
        PerspectiveCameraData *view_data = 
            &bvhParams.views[current_view_offset];

        uint32_t world_idx = (uint32_t)view_data->worldIDX;

        Vector3 ray_start = view_data->position;
        Vector3 ray_dir = calculateOutRay(view_data, pixel_x, pixel_y);

        uint32_t internal_nodes_offset = bvhParams.instanceOffsets[world_idx];

        TraceResult result = traceRayTLAS(
            TraceInfo {
                .rayOrigin = ray_start,
                .rayDirection = ray_dir,
                .tMin = bvhParams.nearSphere,
                .tMax = 10000.f,
            },
            TraceWorldInfo {
                .nodes = bvhParams.internalData->traversalNodes + 
                         internal_nodes_offset,
                .instances = bvhParams.instances + internal_nodes_offset
            }
        );

        uint32_t linear_pixel_idx = 4 * 
            (pixel_y + pixel_x * bvhParams.renderOutputResolution);

        uint32_t global_pixel_byte_off = current_view_offset * bytes_per_view +
            linear_pixel_idx;

        if (bvhParams.raycastRGBD) {
            // Write both depth and color information
            if (result.hit) {
                writeRGB(global_pixel_byte_off, result.color);
                writeDepth(global_pixel_byte_off, result.depth);
            } else {
                writeRGB(global_pixel_byte_off, { 0.f, 0.f, 0.f });
                writeDepth(global_pixel_byte_off, 0.f);
            }
        } else {
            // Only write depth information
            if (result.hit) {
                writeDepth(global_pixel_byte_off, result.depth);
            } else {
                writeDepth(global_pixel_byte_off, 0.f);
            }
        }

        current_view_offset += num_resident_views;

        num_processed_pixels++;

        __syncwarp();
    }
}

