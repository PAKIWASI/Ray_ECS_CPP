#pragma once

#include "common.hpp"
#include "system_base.hpp"
#include "2d_comps.hpp"


class PhysicsSystem : public SystemBase<PhysicsSystem, Transform2, RigidBody2, Gravity2>
{
public:
    using Base = SystemBase<PhysicsSystem, Transform2, RigidBody2, Gravity2>;

    // Constructor with ComponentManager
    explicit PhysicsSystem(ComponentManager& cm) : Base(cm) {}

    void update_impl(float dt) {

        // Direct access - no references stored, just compile-time lookup
        for (u32 i = 0; i < count; ++i)
        {
            Entity e = dense[i];
            auto& transform = get_component<Transform2>(e);
            auto& rb = get_component<RigidBody2>(e);
            auto& gravity = get_component<Gravity2>(e);

            // Physics update
            rb.v.x += gravity.force.x * dt;
            rb.v.y += gravity.force.y * dt;
            transform.pos.x += rb.v.x * dt;
            transform.pos.y += rb.v.y * dt;
        }
    }
};
