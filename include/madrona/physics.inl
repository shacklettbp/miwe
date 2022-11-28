#pragma once

namespace madrona {
namespace phys {

namespace broadphase {

LeafID BVH::reserveLeaf()
{
    int32_t leaf_idx = num_leaves_.fetch_add(1, std::memory_order_relaxed);
    assert(leaf_idx < num_allocated_leaves_);

    return LeafID {
        leaf_idx,
    };
}

template <typename Fn>
void BVH::findOverlaps(const math::AABB &aabb, Fn &&fn) const
{
    int32_t stack[128];
    stack[0] = 0;
    CountT stack_size = 1;

    while (stack_size > 0) {
        int32_t node_idx = stack[--stack_size];
        const Node &node = nodes_[node_idx];
        for (int i = 0; i < 4; i++) {
            if (!node.hasChild(i)) {
                continue; // Technically this could be break?
            };

            madrona::math::AABB child_aabb {
                .pMin = {
                    node.minX[i],
                    node.minY[i],
                    node.minZ[i],
                },
                .pMax = {
                    node.maxX[i],
                    node.maxY[i],
                    node.maxZ[i],
                },
            };

            if (aabb.overlaps(child_aabb)) {
                if (node.isLeaf(i)) {
                    Entity e = leaf_entities_[node.leafIDX(i)];
                    fn(e);
                } else {
                    stack[stack_size++] = node.children[i];
                }
            }
        }
    }
}

bool BVH::Node::isLeaf(IdxT child) const
{
    return children[child] & 0x80000000;
}

int32_t BVH::Node::leafIDX(IdxT child) const
{
    return children[child] & ~0x80000000;
}

void BVH::Node::setLeaf(IdxT child, int32_t idx)
{
    children[child] = 0x80000000 | idx;
}

void BVH::Node::setInternal(IdxT child, int32_t internal_idx)
{
    children[child] = internal_idx;
}

bool BVH::Node::hasChild(IdxT child) const
{
    return children[child] != sentinel_;
}

void BVH::Node::clearChild(IdxT child)
{
    children[child] = sentinel_;
}

}

}
}