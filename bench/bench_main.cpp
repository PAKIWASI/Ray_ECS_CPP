// bench_main.cpp — benchmarks for wasi-ecs, using the single-header harness
// in bench.hpp. No external dependencies.
//
// Build (from repo root):
//   g++ -std=c++23 -O3 -Iinclude/ecs bench/bench_main.cpp -o bench_main
//   ./bench_main
//
// Each case sweeps a few entity counts so you can see whether an operation
// scales the way the design claims (O(1) per-entity add/remove, O(n) dense
// iteration for update()).

#include "bench.hpp"
#include "game_registry.hpp"

#include <vector>

namespace {

Signature transform_rb_sig()
{
    Signature s;
    s.set(GameWorld::component_id<Transform2>);
    s.set(GameWorld::component_id<RigidBody2>);
    return s;
}

} // namespace

int main()
{
    bench::Suite suite;
    const std::vector<uint32_t> sizes = {64, 512, 4096};

    // -------------------------------------------------------------------
    // create_entity + add_component — cost of bringing one entity with
    // two components into existence, including the on_signature_change
    // fold over every system.
    // -------------------------------------------------------------------
    suite.add("create+add_component(x2)", sizes, [](bench::State& st) {
        GameWorld world;
        Signature sig = transform_rb_sig();
        std::vector<Entity> alive;
        alive.reserve(st.n);

        for ([[maybe_unused]] auto _ : st) {
            for (uint32_t i = 0; i < st.n; ++i) {
                Entity e = world.create_entity(sig);
                world.add_component<Transform2>(e, Transform2{});
                world.add_component<RigidBody2>(e, RigidBody2{.v = {1, 0}, .a = {0, 0}});
                alive.push_back(e);
            }

            // Cleanup so the next iteration starts from an empty world.
            // Excluded from the timed region.
            st.pause();
            for (auto e : alive) world.destroy_entity(e);
            alive.clear();
            st.resume();
        }
    });

    // -------------------------------------------------------------------
    // remove_component — swap-and-pop cost in isolation, on a population
    // that's already warm (allocations already happened).
    // -------------------------------------------------------------------
    suite.add("remove_component", sizes, [](bench::State& st) {
        GameWorld world;
        Signature sig = transform_rb_sig();
        std::vector<Entity> es;
        es.reserve(st.n);
        for (uint32_t i = 0; i < st.n; ++i) {
            Entity e = world.create_entity(sig);
            world.add_component<Transform2>(e, Transform2{});
            world.add_component<RigidBody2>(e, RigidBody2{.v = {1, 0}, .a = {0, 0}});
            es.push_back(e);
        }

        for ([[maybe_unused]] auto _ : st) {
            for (auto e : es) world.remove_component<RigidBody2>(e);

            st.pause();
            for (auto e : es) world.add_component<RigidBody2>(e, RigidBody2{.v = {1, 0}, .a = {0, 0}});
            st.resume();
        }

        st.pause();
        for (auto e : es) world.destroy_entity(e);
        st.resume();
    });

    // -------------------------------------------------------------------
    // create_from_archetype — overload 2, caller-supplied values. Should
    // be roughly the cost of create_entity + N add_data calls, minus the
    // per-component on_signature_change overhead (fired once, not N times).
    // -------------------------------------------------------------------
    suite.add("create_from_archetype", sizes, [](bench::State& st) {
        GameWorld world;
        std::vector<Entity> alive;
        alive.reserve(st.n);

        for ([[maybe_unused]] auto _ : st) {
            for (uint32_t i = 0; i < st.n; ++i) {
                Entity e = world.create_from_archetype<PlayerArchetype>(
                    Transform2{}, RigidBody2{.v = {1, 0}, .a = {0, 0}});
                alive.push_back(e);
            }

            st.pause();
            for (auto e : alive) world.destroy_entity(e);
            alive.clear();
            st.resume();
        }
    });

    // -------------------------------------------------------------------
    // update() — the steady-state per-frame cost. This is the number that
    // matters most for a game: how long does one world.update(dt, ...)
    // take with N live entities under MovementSystem.
    // -------------------------------------------------------------------
    suite.add("update(MOVEMENT_ONLY)", sizes, [](bench::State& st) {
        GameWorld world;
        Signature sig = transform_rb_sig();
        std::vector<Entity> es;
        es.reserve(st.n);
        for (uint32_t i = 0; i < st.n; ++i) {
            Entity e = world.create_entity(sig);
            world.add_component<Transform2>(e, Transform2{});
            world.add_component<RigidBody2>(e, RigidBody2{.v = {1, 0}, .a = {0, 0}});
            es.push_back(e);
        }

        for ([[maybe_unused]] auto _ : st) {
            world.update(0.016f, MOVEMENT_ONLY);
        }

        // Touch the result so the optimizer can't prove the loop above is
        // dead code with no observable effect.
        bench::sink(world.get_component<Transform2>(es.front()).pos.x);

        st.pause();
        for (auto e : es) world.destroy_entity(e);
        st.resume();
    });

    // -------------------------------------------------------------------
    // destroy_entity — full teardown path: system removal fold,
    // component-array removal fold, ID recycle.
    // -------------------------------------------------------------------
    suite.add("destroy_entity", sizes, [](bench::State& st) {
        GameWorld world;
        Signature sig = transform_rb_sig();

        for ([[maybe_unused]] auto _ : st) {
            std::vector<Entity> es;
            es.reserve(st.n);

            st.pause();
            for (uint32_t i = 0; i < st.n; ++i) {
                Entity e = world.create_entity(sig);
                world.add_component<Transform2>(e, Transform2{});
                world.add_component<RigidBody2>(e, RigidBody2{.v = {1, 0}, .a = {0, 0}});
                es.push_back(e);
            }
            st.resume();

            for (auto e : es) world.destroy_entity(e);
        }
    });

    suite.run();
    return 0;
}
