#pragma once

// PhysicsSystem — combined gravity + movement in one system.
//
// Kept here as a reference / fallback. For new code prefer using
// GravitySystem + MovementSystem separately — splitting them lets other
// systems (input, collision response) write into RigidBody2.v between the
// two passes without being crammed into one monolithic update.

#include "common.hpp"
#include "system_base.hpp"
#include "2d_comps.hpp"

using namespace ECS_COMPS_2D;

template <typename CMgr>
class PhysicsSystem : public SystemBase<PhysicsSystem<CMgr>, CMgr, Transform2, RigidBody2, Gravity2>
{
  public:
    using Base = SystemBase<PhysicsSystem<CMgr>, CMgr, Transform2, RigidBody2, Gravity2>;

    explicit PhysicsSystem(CMgr& cm) : Base(cm) {}

    // Drive loop from Gravity2 — smallest of the three required arrays.
    // Gravity2 entities are a subset of RigidBody2 which are a subset of Transform2.
    // One sequential read (Gravity2), two random lookups (RigidBody2, Transform2).
    void update_impl(float dt)
    {
        auto& gravities   = this->comp_manager.template get_arr<Gravity2>();
        auto& rigidbodies = this->comp_manager.template get_arr<RigidBody2>();
        auto& transforms  = this->comp_manager.template get_arr<Transform2>();

        for (u32 i = 0; i < gravities.entity_count(); ++i)
        {
            Entity e  = gravities.entity_at(i);    // sequential — free
            auto&  g  = gravities.data_at(i);      // sequential — free
            auto&  rb = rigidbodies.get_data(e);   // random lookup
            auto&  t  = transforms.get_data(e);    // random lookup

            rb.v.x += g.force.x * dt;
            rb.v.y += g.force.y * dt;
            t.pos.x += rb.v.x  * dt;
            t.pos.y += rb.v.y  * dt;
        }
    }
};
