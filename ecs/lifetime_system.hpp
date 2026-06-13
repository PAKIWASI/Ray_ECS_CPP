#pragma once

#include "components.hpp"
#include "ecs.hpp"
#include <vector>


class LifetimeSystem : public System
{
  private:
    World& world;

  public:
    explicit LifetimeSystem(World& world) : world(world) {}

    [[nodiscard]] auto get_signature() const -> Signature override
    {
        Signature sig;
        sig.set(world.get_component_type<ECS_Components::Lifetime>());
        return sig;
    }

    void update(float dt) override {
        // Collect expired entities first — can't remove while iterating
        std::vector<Entity> to_destroy;

        for (Entity entity : entities) {
            auto& lifetime = world.get_component<ECS_Components::Lifetime>(entity);
            lifetime.remaining -= dt;
            if (lifetime.remaining <= 0.0F) {
                to_destroy.push_back(entity);
            }
        }

        for (Entity entity : to_destroy) {
            world.destroy_entity(entity);
        }
    }
};
