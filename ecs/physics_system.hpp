#pragma once

#include "ecs.hpp"
#include "components.hpp"

using namespace ECS_COMPS_2D;

class PhysicsSystem : public ISystem
{
  private:
    static auto make_signature() -> Signature
    {
        Signature sig;
        sig.set(ComponentManager::get_component_id<Transform2>());
        sig.set(ComponentManager::get_component_id<RigidBody2>());
        sig.set(ComponentManager::get_component_id<Gravity2>());
        return sig;
    }

  public:
    PhysicsSystem(const ComponentManager& cm)
        : ISystem(make_signature(), cm) {}

    void update(float dt) override
    {
        for (Entity e : entities)
        {
            auto& transform  = component_manager.get_component<Transform2>(e);
            auto& rigidbody  = component_manager.get_component<RigidBody2>(e);
            auto& gravity    = component_manager.get_component<Gravity2>(e);

            // apply gravity to acceleration
            rigidbody.a.x += gravity.force.x;
            rigidbody.a.y += gravity.force.y;

            // integrate velocity
            rigidbody.v.x += rigidbody.a.x * dt;
            rigidbody.v.y += rigidbody.a.y * dt;

            // integrate position
            transform.pos.x += rigidbody.v.x * dt;
            transform.pos.y += rigidbody.v.y * dt;

            // reset acceleration (forces are re-applied each frame)
            rigidbody.a = {.x=0, .y=0};
        }
    }

};
