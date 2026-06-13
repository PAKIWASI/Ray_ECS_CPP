#pragma once

#include "components.hpp"
#include "ecs.hpp"
#include <raylib.h>



class RenderSystem : public System
{
  private:
    World& world;

  public:
    explicit RenderSystem(World& world) : world(world) {}

    [[nodiscard]] auto get_signature() const -> Signature override
    {
        Signature sig;
        sig.set(world.get_component_type<ECS_Components::Transform>());
        sig.set(world.get_component_type<ECS_Components::CircleShape>());
        return sig;
    }

    void update(float dt) override
    {
        // NOTE: Call BeginDrawing/EndDrawing in main.cpp
        // This system just draws the shapes
        for (Entity entity : entities) {
            auto& t = world.get_component<ECS_Components::Transform>(entity);
            auto& s = world.get_component<ECS_Components::CircleShape>(entity);

            DrawCircle(
                static_cast<int>(t.x),
                static_cast<int>(t.y),
                s.radius * t.scale,
                s.color
            );

            // Optional: draw health bar if entity has Health
            if (world.has_component<ECS_Components::Health>(entity)) {
                auto& h = world.get_component<ECS_Components::Health>(entity);
                float bar_width = s.radius * 2.0F;
                float bar_height = 4.0F;
                float bar_x = t.x - s.radius;
                float bar_y = t.y - s.radius - 8.0F;

                DrawRectangle(bar_x, bar_y, bar_width, bar_height, DARKGRAY);
                DrawRectangle(bar_x, bar_y, bar_width * h.percentage(), bar_height, GREEN);
            }
        }
    }

};
