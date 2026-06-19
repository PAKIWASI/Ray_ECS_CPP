#pragma once

#include "common.hpp"
#include "system_base.hpp"
#include "2d_comps.hpp"


// TODO: not have a general Physics system, divide into movement, etc etc
class PhysicsSystem : public SystemBase<PhysicsSystem, Transform2, RigidBody2, Gravity2>
{
public:
    using Base = SystemBase<PhysicsSystem, Transform2, RigidBody2, Gravity2>;

    // Constructor with ComponentManager
    PhysicsSystem(ComponentManager& cm) : Base(cm) {}

    void update_impl(float dt)
    {
        auto& gravities    = comp_manager.get_arr<Gravity2>();
        auto& rigidbodies  = comp_manager.get_arr<RigidBody2>();
        auto& transforms   = comp_manager.get_arr<Transform2>();

        for (u32 i = 0; i < gravities.entity_count(); ++i)
        {
            Entity e      = gravities.entity_at(i);    // sequential — free
            auto&  g      = gravities.get_data(e);      // sequential — free
            auto&  rb     = rigidbodies.get_data(e);   // one random lookup
            auto&  t      = transforms.get_data(e);    // one random lookup

            rb.v.x += g.force.x * dt;
            rb.v.y += g.force.y * dt;
            t.pos.x += rb.v.x * dt;
            t.pos.y += rb.v.y * dt;
        }
    }
};
