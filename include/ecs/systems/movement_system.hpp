#pragma once

#include "common.hpp"
#include "system_base.hpp"
#include "2d_comps.hpp"


// Integrates velocity into position. Generic on purpose: it doesn't care
// *why* an entity has velocity, only that it does. GravitySystem,
// InputSystem, CollisionSystem (response/bounce), etc. can all write into
// RigidBody2.v independently, as long as they run earlier in the schedule
// than this system does.
class MovementSystem : public SystemBase<MovementSystem, Transform2, RigidBody2>
{
public:
    using Base = SystemBase<MovementSystem, Transform2, RigidBody2>;

    // Constructor with ComponentManager
    MovementSystem(ComponentManager& cm) : Base(cm) {}

    // Smallest-set iteration: drive the loop from whichever of the two
    // required arrays is smaller. Unlike GravitySystem/Transform2 split,
    // there's no a priori reason to expect RigidBody2 or Transform2 to be
    // rarer in general - this picks whichever is smaller at runtime instead
    // of hardcoding an assumption.
    void update_impl(float dt)
    {
        auto& rigidbodies = comp_manager.get_arr<RigidBody2>();
        auto& transforms  = comp_manager.get_arr<Transform2>();

        // almost everything has a transform
        for (u32 i = 0; i < rigidbodies.entity_count(); ++i)
        {
            Entity e  = rigidbodies.entity_at(i);  // sequential — free
            auto&  rb = rigidbodies.get_data(e);   // sequential — free
            auto&  t  = transforms.get_data(e);    // one random lookup

            t.pos.x += rb.v.x * dt;
            t.pos.y += rb.v.y * dt;
        }
    }
};
