#include "ecs.hpp"
#include "raylib.h"
#include "2d_comps.hpp"
#include "physics_system.hpp"
#include "render_system.hpp"
#include "wasi.hpp"
#include "component_list.hpp"

using namespace wasi;
using namespace ECS_COMPS_2D;

auto main() -> int
{
    // Game config
    // ============
    constexpr u32 SCREEN_W { 600 };
    constexpr u32 SCREEN_H { 400 };
    const std::string TITLE { "ECS Demo" };
    constexpr float FPS { 60.0F };

    // Setup
    // ======
    InitWindow(SCREEN_W, SCREEN_H, TITLE.c_str());
    SetTargetFPS(FPS);

    // ECS Creation
    // =============
    World world {};

    // Register components - MUST register all components before using them
    world.register_component<Transform2>();
    world.register_component<RigidBody2>();
    world.register_component<Gravity2>();

    // Create some entities
    Entity player = world.create_entity();
    
    // Add components to the player entity

    // Register systems
    world.register_system<PhysicsSystem>(world.get_component_manager());
    world.register_system<RenderSystem>(world.get_component_manager());

    // Create a schedule that runs both systems every frame
    Schedule physics_only{};
    physics_only.set(SystemManager::get_system_id<PhysicsSystem>());
    Schedule render_only{};
    render_only.set(SystemManager::get_system_id<RenderSystem>());


    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        
        // Update physics
        world.update(dt, physics_only);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        world.update(dt, render_only);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
