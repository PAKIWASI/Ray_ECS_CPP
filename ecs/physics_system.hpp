#pragma once

#include "components.hpp"
#include "ecs.hpp"


class PhysicsSystem : public System {
  private:
    World& world;

  public:
    explicit PhysicsSystem(World& world) : world(world) {}

    // Declare what components this system needs
    [[nodiscard]] auto get_signature() const -> Signature override
    {
        Signature sig;
        sig.set(world.get_component_type<ECS_Components::Transform>());
        sig.set(world.get_component_type<ECS_Components::Velocity>());
        return sig;
    }

    void update(float dt) override
    {
        for (Entity entity : entities) {
            auto& transform = world.get_component<ECS_Components::Transform>(entity);
            auto& velocity  = world.get_component<ECS_Components::Velocity>(entity);

            transform.x += velocity.dx * dt;
            transform.y += velocity.dy * dt;

            // Screen wrapping
            if (transform.x < 0) {
                transform.x = GetScreenWidth();
            }
            if (GetScreenWidth() < transform.x) {
                transform.x = 0;
            }
            if (transform.y < 0) {
                transform.y = GetScreenHeight();
            }
            if (transform.y > GetScreenHeight()) {
                transform.y = 0;
            }
        }
    }
};
