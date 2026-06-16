#include "ecs.hpp"
#include "components.hpp"
#include "physics_system.hpp"
#include "render_system.hpp"
#include <raylib.h>
#include <cstdlib>   // rand

using namespace ECS_COMPS_2D;

// ----------------------------------------------------------------
// Bounce system: reflects velocity when entity hits screen edges
// ----------------------------------------------------------------
class BounceSystem : public ISystem
{
    int screen_w;
    int screen_h;

  public:
    BounceSystem(const ComponentManager& cm, int w, int h)
        : ISystem(make_signature(), cm)
        , screen_w(w), screen_h(h) {}

    void update(float dt) override
    {
        for (Entity e : entities)
        {
            auto& t = component_manager.get_component<Transform2>(e);
            auto& r = component_manager.get_component<RigidBody2>(e);

            if (t.pos.x < 0.0F)            { t.pos.x = 0.0F;              r.v.x =  std::abs(r.v.x); }
            if (t.pos.x > (float)screen_w) { t.pos.x = (float)screen_w;   r.v.x = -std::abs(r.v.x); }
            if (t.pos.y < 0.0F)            { t.pos.y = 0.0F;              r.v.y =  std::abs(r.v.y); }
            if (t.pos.y > (float)screen_h) { t.pos.y = (float)screen_h;   r.v.y = -std::abs(r.v.y); }
        }
    }

  private:
    static auto make_signature() -> Signature
    {
        Signature sig;
        sig.set(ComponentManager::get_component_id<Transform2>());
        sig.set(ComponentManager::get_component_id<RigidBody2>());
        return sig;
    }
};


// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------

static auto randf(float lo, float hi) -> float
{
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

static void spawn_entity(World& world, int screen_w, int screen_h)
{
    Entity e = world.create_entity();
    world.add_component(e, Transform2{
        .pos   = { randf(0, (float)screen_w), randf(0, (float)screen_h) },
        .rot   = { 0, 0 },
        .scale = { 1, 1 }
    });
    world.add_component(e, RigidBody2{
        .v = { randf(-300, 300), randf(-300, 300) },
        .a = { 0, 0 }
    });
    world.add_component(e, Gravity2{
        .force = { 0, 200 }    // mild gravity so bouncing is visible
    });
}


// ----------------------------------------------------------------
// Main
// ----------------------------------------------------------------

auto main() -> int
{
    constexpr int SCREEN_W      = 1280;
    constexpr int SCREEN_H      = 720;
    constexpr int INITIAL_COUNT = 500;   // start with this many
    constexpr int SPAWN_BATCH   = 100;   // Space to add more, Del to remove batch
    constexpr int HISTORY_LEN   = 120;   // frames of FPS history for graph

    InitWindow(SCREEN_W, SCREEN_H, "ECS stress test");
    SetTargetFPS(0);   // uncapped — we want to see real throughput

    // ---- ECS setup ----
    World world{};

    world.register_component<Transform2>();
    world.register_component<RigidBody2>();
    world.register_component<Gravity2>();

    ComponentManager& cm = world.get_component_manager();

    world.register_system<BounceSystem> (cm, SCREEN_W, SCREEN_H);
    world.register_system<PhysicsSystem>(cm);
    world.register_system<RenderSystem> (cm);

    Schedule physics_schedule;
    physics_schedule.set(SystemManager::get_system_id<BounceSystem>());
    physics_schedule.set(SystemManager::get_system_id<PhysicsSystem>());

    Schedule render_schedule;
    render_schedule.set(SystemManager::get_system_id<RenderSystem>());

    // ---- spawn initial entities ----
    int entity_count = 0;
    std::vector<Entity> alive;  // track so we can destroy in batches
    alive.reserve(MAX_ENTITIES);

    auto spawn_batch = [&](int n) {
        for (int i = 0; i < n && entity_count < (int)MAX_ENTITIES; i++) {
            spawn_entity(world, SCREEN_W, SCREEN_H);
            alive.push_back(entity_count);   // entity ids are sequential from 0
            entity_count++;
        }
    };

    auto destroy_batch = [&](int n) {
        for (int i = 0; i < n && !alive.empty(); i++) {
            world.destroy_entity(alive.back());
            alive.pop_back();
            entity_count--;
        }
    };

    spawn_batch(INITIAL_COUNT);

    // ---- perf tracking ----
    float fps_history[HISTORY_LEN] = {};
    int   history_idx = 0;

    double physics_ms = 0.0;
    double render_ms  = 0.0;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        dt = (dt > 0.05F) ? 0.05F : dt;    // clamp so explosion on lag spike

        // input
        if (IsKeyPressed(KEY_SPACE))  spawn_batch(SPAWN_BATCH);
        if (IsKeyPressed(KEY_DELETE)) destroy_batch(SPAWN_BATCH);

        // physics pass — timed
        double t0 = GetTime();
        world.update(dt, physics_schedule);
        physics_ms = (GetTime() - t0) * 1000.0;

        // render pass — timed
        BeginDrawing();
            ClearBackground({ 15, 15, 20, 255 });

            double t1 = GetTime();
            world.update(dt, render_schedule);
            render_ms = (GetTime() - t1) * 1000.0;

            // FPS history
            float fps = GetFPS();
            fps_history[history_idx] = fps;
            history_idx = (history_idx + 1) % HISTORY_LEN;

            float avg_fps = 0;
            for (int i = 0; i < HISTORY_LEN; i++) avg_fps += fps_history[i];
            avg_fps /= HISTORY_LEN;

            // ---- HUD ----
            // FPS graph (bottom-left)
            constexpr int  GRAPH_X = 10, GRAPH_Y = SCREEN_H - 80;
            constexpr int  GRAPH_W = 240, GRAPH_H = 60;
            constexpr float GRAPH_MAX_FPS = 500.0F;

            DrawRectangle(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, { 0, 0, 0, 160 });
            for (int i = 0; i < HISTORY_LEN; i++) {
                int idx  = (history_idx + i) % HISTORY_LEN;
                float f  = fps_history[idx] / GRAPH_MAX_FPS;
                if (f > 1.0F) f = 1.0F;
                int bh = (int)(f * GRAPH_H);
                int bx = GRAPH_X + i * GRAPH_W / HISTORY_LEN;
                DrawRectangle(bx, GRAPH_Y + GRAPH_H - bh, GRAPH_W / HISTORY_LEN, bh,
                              { 80, 200, 80, 220 });
            }
            DrawRectangleLines(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, DARKGRAY);

            // text overlay
            char buf[128];
            snprintf(buf, sizeof(buf), "FPS: %.0f  (avg %.0f)", fps, avg_fps);
            DrawText(buf, 10, 10, 20, GREEN);

            snprintf(buf, sizeof(buf), "Entities: %d / %d", entity_count, MAX_ENTITIES);
            DrawText(buf, 10, 35, 20, WHITE);

            snprintf(buf, sizeof(buf), "Physics: %.2f ms    Render: %.2f ms", physics_ms, render_ms);
            DrawText(buf, 10, 60, 20, YELLOW);

            snprintf(buf, sizeof(buf), "[SPACE] +%d    [DEL] -%d", SPAWN_BATCH, SPAWN_BATCH);
            DrawText(buf, 10, SCREEN_H - 100, 18, LIGHTGRAY);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
