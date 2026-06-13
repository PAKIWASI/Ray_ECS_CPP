#pragma once

#include "components.hpp"
#include "ecs.hpp"
#include <raylib.h>
#include <cmath>



class InputSystem : public System
{
  private:
    World& world;

  public:
    explicit InputSystem(World& world) : world(world) {}

    [[nodiscard]] auto get_signature() const -> Signature override
    {
        Signature sig;
        sig.set(world.get_component_type<ECS_Components::Player>());
        sig.set(world.get_component_type<ECS_Components::Velocity>());
        return sig;
    }

    void update(float dt) override
    {
        for (Entity entity : entities) {
            auto& player   = world.get_component<ECS_Components::Player>(entity);
            auto& velocity = world.get_component<ECS_Components::Velocity>(entity);

            float dx = 0.0F;
            float dy = 0.0F;
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    { dy -= 1.0F; }
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  { dy += 1.0F; }
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  { dx -= 1.0F; }
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) { dx += 1.0F; }

            // Normalize diagonal movement
            float len = std::sqrt((dx*dx) + (dy*dy));
            if (len > 0.0F) {
                dx /= len;
                dy /= len;
            }

            velocity.dx = dx * player.speed;
            velocity.dy = dy * player.speed;
        }
    }
};
