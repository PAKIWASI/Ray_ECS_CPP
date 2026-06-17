// ecs/physics_system.hpp
#pragma once

#include "ecs.hpp"
#include "components.hpp"

using namespace ECS_COMPS_2D;

class PhysicsSystem : public ISystem
{
  public:
    PhysicsSystem(const ComponentManager& cm)
        : ISystem(make_signature())
        , transforms(cm.get_arr<Transform2>())
        , rigidbodies(cm.get_arr<RigidBody2>())
        , gravities(cm.get_arr<Gravity2>())
    {}


    void update(float dt) override
    {
    }


  private:
    ComponentArray<Transform2>& transforms;
    ComponentArray<RigidBody2>& rigidbodies;
    ComponentArray<Gravity2>&   gravities;

    static auto make_signature() -> Signature
    {
        Signature sig{};
        sig.set(ComponentManager::get_component_id<Transform2>());
        sig.set(ComponentManager::get_component_id<RigidBody2>());
        sig.set(ComponentManager::get_component_id<Gravity2>());
        return sig;
    }
};
