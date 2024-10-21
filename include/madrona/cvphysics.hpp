#pragma once

#include <madrona/components.hpp>
#include <madrona/taskgraph_builder.hpp>

namespace madrona::phys::cv {

// Attach this component to entities that you want to have obey physics.
struct CVPhysicalComponent {
    // This is going to be one of the DOF entities (i.e. DOFFreeBodyArchetype).
    Entity physicsEntity;
};



static constexpr uint32_t kMaxCoords = 6;

enum class DofType {
    FreeBody = 6,

    // When we add other types of physics DOF objects, we will encode
    // the number of degrees of freedom they all have here.
};

struct DofObjectPosition {
    float q[kMaxCoords];
};

struct DofObjectVelocity {
    float qv[kMaxCoords];
};

struct DofObjectNumDofs {
    uint32_t numDofs;
};

struct DofObjectArchetype : public Archetype<
    DofObjectPosition,
    DofObjectVelocity,
    DofObjectNumDofs
> {};

 

    
// For now, initial velocities are just going to be 0
void makeFreeBodyEntityPhysical(Context &ctx, Entity e,
                                base::Position position,
                                base::Rotation rotation);
void cleanupPhysicalEntity(Context &ctx, Entity e);



void registerTypes(ECSRegistry &registry);
void getSolverArchetypeIDs(uint32_t *contact_archetype_id,
                           uint32_t *joint_archetype_id);
void init(Context &ctx);
TaskGraphNodeID setupCVSolverTasks(TaskGraphBuilder &builder,
                                   TaskGraphNodeID broadphase,
                                   CountT num_substeps);

}