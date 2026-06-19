#pragma once

// GravitySystem
//
// Applies gravitational force to velocity. Does NOT touch position —
// that is MovementSystem's job. Splitting these apart means anything that
// wants to modify RigidBody2.v (input, collision response, wind zones) can
// run between GravitySystem and MovementSystem in the schedule.
//
// Template parameter CMgr is the concrete ComponentManager type from
// game_registry.hpp. It is passed through SystemBase so make_signature can
// derive the ComponentList from CMgr::ListType without a separate parameter.

#include "common.hpp"
#include "system_base.hpp"
#include "2d_comps.hpp"

using namespace ECS_COMPS_2D;

template <typename CMgr>
class GravitySystem : public SystemBase<GravitySystem<CMgr>, CMgr, RigidBody2, Gravity2>
{
  public:
    using Base = SystemBase<GravitySystem<CMgr>, CMgr, RigidBody2, Gravity2>;

    explicit GravitySystem(CMgr& cm) : Base(cm) {}

    // Smallest-set iteration: Gravity2 is expected to be rarer than RigidBody2
    // (not every moving body is affected by gravity — kinematically driven bodies
    // are not). Drive the loop from Gravity2's array for one free sequential read,
    // then one random lookup into RigidBody2 per entity.
    void update_impl(float dt)
    {
        auto& gravities   = this->comp_manager.template get_arr<Gravity2>();
        auto& rigidbodies = this->comp_manager.template get_arr<RigidBody2>();

        for (u32 i = 0; i < gravities.entity_count(); ++i)
        {
            Entity e  = gravities.entity_at(i);   // sequential — free
            auto&  g  = gravities.data_at(i);      // sequential — free
            auto&  rb = rigidbodies.get_data(e);   // one random lookup

            rb.v.x += g.force.x * dt;
            rb.v.y += g.force.y * dt;
        }
    }
};
