#include "ecs.hpp"
#include "components.hpp"
#include "physics_system.hpp"
#include "render_system.hpp"
#include <raylib.h>




auto main() -> int
{
    // setup 

    World world{};

    world.register_component<Transform2>();
    world.register_component<RigidBody2>();
    world.register_component<Gravity2>();

    // systems need a ref to ComponentManager — expose it from World
    world.register_system<PhysicsSystem>(world.get_component_manager());
    world.register_system<RenderSystem>(world.get_component_manager());

    // spawn a ball
    Entity ball = world.create_entity();
    world.add_component(ball, Transform2{ .pos={400, 100}, .rot={0,0}, .scale={1,1} });
    world.add_component(ball, RigidBody2{ .v={50, 0}, .a={0, 0} });
    world.add_component(ball, Gravity2{   .force={0, 500} });   // pixels/s^2 downward

    // raylib window
    InitWindow(800, 600, "ECS demo");
    SetTargetFPS(60);


    // pre-build schedules once at startup
    Schedule physics_only;
    physics_only.set(SystemManager::get_system_id<PhysicsSystem>());

    Schedule render_only;
    render_only.set(SystemManager::get_system_id<RenderSystem>());


    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        // runs PhysicsSystem then RenderSystem
        world.update(dt, physics_only);

        BeginDrawing();
            ClearBackground(RAYWHITE);
            world.update(dt, render_only);
        EndDrawing();
    }

    CloseWindow();
}



