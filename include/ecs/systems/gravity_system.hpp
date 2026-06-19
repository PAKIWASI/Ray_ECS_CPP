#pragma once

#include "common.hpp"
#include "system_base.hpp"
#include "2d_comps.hpp"


// Applies gravitational force to velocity. Does NOT touch position -
// that's MovementSystem's job (see movement_system.hpp). Splitting these
// apart means anything else that wants to add to RigidBody2.v (input,
// collision response, wind zones, etc.) can run between this system and
// MovementSystem in the schedule, instead of being crammed into one
// general-purpose "PhysicsSystem".
class GravitySystem : public SystemBase<GravitySystem, RigidBody2, Gravity2>
{
public:
    using Base = SystemBase<GravitySystem, RigidBody2, Gravity2>;

    // Constructor with ComponentManager
    GravitySystem(ComponentManager& cm) : Base(cm) {}

    // Smallest-set iteration: Gravity2 is expected to be the rarer
    // component of the two (not every moving body is affected by gravity -
    // e.g. anything kinematically driven), so we drive the loop from
    // Gravity2's array and do one random lookup into RigidBody2 per entity,
    // rather than walking `dense` and doing two random lookups each time.
    void update_impl(float dt)
    {
        auto& gravities   = comp_manager.get_arr<Gravity2>();
        auto& rigidbodies = comp_manager.get_arr<RigidBody2>();

        for (u32 i = 0; i < gravities.entity_count(); ++i)
        {
            Entity e  = gravities.entity_at(i);   // sequential — free
            auto&  g  = gravities.get_data(e);     // sequential — free
            auto&  rb = rigidbodies.get_data(e);   // one random lookup

            rb.v.x += g.force.x * dt;
            rb.v.y += g.force.y * dt;
        }
    }
};
