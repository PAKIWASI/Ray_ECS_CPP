#include "raylib.h"
#include "raymath.h"
#include "ecs/game_registry.hpp"

#include <cstdlib>
#include <cmath>
#include <array>
#include <memory>

// ─── Config ────────────────────────────────────────────────────────────────

constexpr int   SCREEN_W        = 1280;
constexpr int   SCREEN_H        = 720;
constexpr u32   INITIAL_BALLS   = 4096;
constexpr u32   SPAWN_BATCH     = 256;
constexpr float ARENA_HALF      = 20.0f;
constexpr float GRAVITY         = -18.0f;
constexpr float BALL_RADIUS     = 0.35f;
constexpr float MAX_SPEED       = 12.0f;

// ─── Helpers ───────────────────────────────────────────────────────────────

static float randf(float lo, float hi)
{
    return lo + (hi - lo) * (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX));
}

static Color random_color()
{
    return ColorFromHSV(randf(0.f, 360.f), randf(0.65f, 1.f), randf(0.75f, 1.f));
}

static void spawn_ball(GameWorld& world, u32& entity_count)
{
    if (entity_count >= MAX_ENTITIES) return;

    // Use add_component individually — avoids the T=SomeType& deduction
    // issue in create_from_archetype overload 2 when passing named lvalues.
    Entity e = world.create_entity();

    world.add_component<Transform3>(e, Transform3{
        { randf(-ARENA_HALF * 0.8f, ARENA_HALF * 0.8f),
          randf(0.f, ARENA_HALF * 1.6f),
          randf(-ARENA_HALF * 0.8f, ARENA_HALF * 0.8f) },
        {0,0,0}, BALL_RADIUS
    });

    world.add_component<Velocity3>(e, Velocity3{
        { randf(-MAX_SPEED, MAX_SPEED),
          randf(-MAX_SPEED * 0.5f, MAX_SPEED),
          randf(-MAX_SPEED, MAX_SPEED) }
    });

    world.add_component<Gravity3>(e, Gravity3{ GRAVITY });

    world.add_component<SphereRender>(e, SphereRender{ BALL_RADIUS, random_color() });

    world.add_component<BoundsBox>(e, BoundsBox{
        -ARENA_HALF, ARENA_HALF,
        -ARENA_HALF, ARENA_HALF * 2.0f,
        -ARENA_HALF, ARENA_HALF
    });

    ++entity_count;
}

static void reset_world(std::unique_ptr<GameWorld>& world, u32& entity_count, u32 count)
{
    world = std::make_unique<GameWorld>();
    entity_count = 0;
    std::srand(42);
    for (u32 i = 0; i < count; ++i)
        spawn_ball(*world, entity_count);
}

// ─── Main ──────────────────────────────────────────────────────────────────

int main()
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "wasi-ecs  |  3D stress test");
    SetTargetFPS(0);

    Camera3D camera{};
    camera.position   = { 0.f, ARENA_HALF * 1.2f, ARENA_HALF * 2.5f };
    camera.target     = { 0.f, 0.f, 0.f };
    camera.up         = { 0.f, 1.f, 0.f };
    camera.fovy       = 60.f;
    camera.projection = CAMERA_PERSPECTIVE;

    // World lives on heap — avoids the deleted move-assign from systems
    // holding CMgr& refs. Replacing the world = heap-allocate a new one.
    std::unique_ptr<GameWorld> world;
    u32 entity_count = 0;
    reset_world(world, entity_count, INITIAL_BALLS);

    // FPS graph — last 256 frames
    constexpr int GRAPH_W = 256;
    std::array<float, GRAPH_W> fps_history{};
    fps_history.fill(0.f);
    int  graph_head  = 0;
    bool auto_orbit  = true;
    float cam_angle  = 0.f;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        // Input
        if (IsKeyPressed(KEY_UP))
            for (u32 i = 0; i < SPAWN_BATCH; ++i)
                spawn_ball(*world, entity_count);

        if (IsKeyPressed(KEY_DOWN) && entity_count > SPAWN_BATCH)
        {
            u32 target = entity_count - SPAWN_BATCH;
            reset_world(world, entity_count, target);
        }

        if (IsKeyPressed(KEY_SPACE)) auto_orbit = !auto_orbit;

        if (IsKeyPressed(KEY_R))
            reset_world(world, entity_count, INITIAL_BALLS);

        // Camera
        if (auto_orbit)
        {
            cam_angle += dt * 0.25f;
            float r = ARENA_HALF * 2.6f;
            camera.position.x = sinf(cam_angle) * r;
            camera.position.z = cosf(cam_angle) * r;
            camera.position.y = ARENA_HALF * 1.1f;
        }
        else
        {
            UpdateCamera(&camera, CAMERA_FREE);
        }

        // ECS physics
        world->update(dt, PHYSICS_SCHEDULE);

        // FPS history
        float fps = (dt > 0.f) ? (1.f / dt) : 0.f;
        fps_history[graph_head] = fps;
        graph_head = (graph_head + 1) % GRAPH_W;

        // ─── Draw ──────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground({ 12, 12, 18, 255 });

        BeginMode3D(camera);

            DrawCubeWires(
                { 0.f, ARENA_HALF * 0.5f, 0.f },
                ARENA_HALF * 2.f, ARENA_HALF * 3.f, ARENA_HALF * 2.f,
                { 60, 60, 80, 200 }
            );
            DrawGrid(40, 1.f);
            world->update(0.f, RENDER_SCHEDULE);

        EndMode3D();

        // ─── HUD ───────────────────────────────────────────────────────────
        DrawRectangle(8, 8, 300, 172, { 0, 0, 0, 160 });

        DrawText(TextFormat("FPS          %4d",   GetFPS()),          18, 18,  20, GREEN);
        DrawText(TextFormat("Frame        %.2f ms", dt * 1000.f),     18, 42,  20, { 180,255,180,255 });
        DrawText(TextFormat("Entities     %d",    entity_count),      18, 66,  20, SKYBLUE);
        DrawText(TextFormat("MAX_ENTITIES %d",    MAX_ENTITIES),      18, 90,  20, { 120,180,255,255 });
        DrawText("UP/DOWN  add/remove 256 balls",   18, 120, 16, { 200,200,200,220 });
        DrawText("SPACE    toggle orbit / free cam", 18, 140, 16, { 200,200,200,220 });
        DrawText("R        reset to 4096",           18, 160, 16, { 200,200,200,220 });

        // FPS graph
        constexpr int GX = 8, GY = 192, GH = 80;
        DrawRectangle(GX, GY, GRAPH_W, GH, { 0, 0, 0, 140 });
        DrawText("FPS", GX + 4, GY + 4, 14, { 100,255,100,200 });

        float peak = 10.f;
        for (float v : fps_history) if (v > peak) peak = v;

        for (int i = 0; i < GRAPH_W; ++i)
        {
            int   idx = (graph_head + i) % GRAPH_W;
            float h   = (fps_history[idx] / peak) * (GH - 4);
            Color gc  = (fps_history[idx] >= 55.f) ? GREEN
                      : (fps_history[idx] >= 30.f) ? YELLOW : RED;
            DrawLine(GX + i, GY + GH, GX + i, GY + GH - (int)h, gc);
        }

        if (entity_count >= MAX_ENTITIES)
            DrawText("MAX ENTITIES REACHED", SCREEN_W/2 - 160, SCREEN_H - 38, 24, RED);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
