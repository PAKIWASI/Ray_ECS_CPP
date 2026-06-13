#pragma once

#include "components.hpp"
#include "ecs.hpp"
#include <raylib.h>
#include <cmath>



class EnemySystem : public System
{
  private:
    World& world;
  public:
    explicit EnemySystem(World& world) : world(world) {}

    [[nodiscard]] auto get_signature() const -> Signature override
    {
        Signature sig;
        sig.set(world.get_component_type<ECS_Components::Enemy>());
        sig.set(world.get_component_type<ECS_Components::Transform>());
        sig.set(world.get_component_type<ECS_Components::Velocity>());
        return sig;
    }

    void update(float dt) override
    {
        for (Entity entity : entities) {
            auto& enemy     = world.get_component<ECS_Components::Enemy>(entity);
            auto& transform = world.get_component<ECS_Components::Transform>(entity);
            auto& velocity  = world.get_component<ECS_Components::Velocity>(entity);

            // Chase target if it exists and has a Transform
            if (world.has_component<ECS_Components::Transform>(enemy.target)) {
                auto& target_t = world.get_component<ECS_Components::Transform>(enemy.target);

                float dx = target_t.x - transform.x;
                float dy = target_t.y - transform.y;
                float dist = std::sqrt((dx*dx) + (dy*dy));

                if (dist > 0.0F) {
                    velocity.dx = (dx / dist) * enemy.speed;
                    velocity.dy = (dy / dist) * enemy.speed;
                }

                // Attack timer
                enemy.attack_timer -= dt;
                if (enemy.attack_timer <= 0.0F && dist < 40.0F) {
                    enemy.attack_timer = 1.0F / enemy.attack_rate;
                    // Deal damage to target
                    if (world.has_component<ECS_Components::Health>(enemy.target)) {
                        auto& health = world.get_component<ECS_Components::Health>(enemy.target);
                        health.current -= 10.0F;
                    }
                }
            }
        }
    }
};
