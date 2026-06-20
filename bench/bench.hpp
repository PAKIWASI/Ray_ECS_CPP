#pragma once

// bench.hpp — single-header, zero-dependency micro-benchmark harness.
//
// No external libraries. Just <chrono> + <vector> + <cstdio>. Designed for
// timing small, repeated operations (the kind that show up in an ECS hot
// path: add_component, update(), create/destroy, etc.) where you control
// the entity count and want to sweep across sizes.
//
// Usage:
//
//   #include "bench.hpp"
//
//   int main() {
//       bench::Suite suite;
//
//       suite.add("add_component", {64, 1024, 8192}, [](bench::State& st) {
//           GameWorld world;
//           Signature sig;
//           sig.set(GameWorld::component_id<Transform2>);
//           std::vector<Entity> es;
//           for (uint32_t i = 0; i < st.n; ++i)
//               es.push_back(world.create_entity(sig));
//
//           for (auto _ : st) {                 // timed region
//               for (auto e : es)
//                   world.add_component<Transform2>(e, Transform2{});
//               // un-time cleanup before the next iteration:
//               st.pause();
//               for (auto e : es)
//                   world.remove_component<Transform2>(e);
//               st.resume();
//           }
//       });
//
//       suite.run();   // prints a table, picks iteration counts automatically
//   }
//
// Design notes:
//   - st.n is the "problem size" for this run (e.g. entity count).
//   - The `for (auto _ : st)` loop runs until either a minimum wall-clock
//     budget or a minimum iteration count is satisfied (whichever takes
//     longer), the same idea as Google Benchmark's auto-tuning, just much
//     simpler.
//   - st.pause()/st.resume() let you exclude setup/teardown inside the loop
//     body from the timed region, without a second outer loop.
//   - Results are reported as time/op (problem size factored in), not just
//     time/iteration, so e.g. "add_component" at n=8192 is reported as
//     ns per single add_component call, comparable across different n.
//   - benchmark::DoNotOptimize equivalent: bench::sink(x) forces the
//     compiler to treat x as observed, preventing dead-code elimination of
//     work the optimizer would otherwise prove unused.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bench {

// ---------------------------------------------------------------------------
// Prevent the optimizer from eliding work whose result is "unused".
// Forces a value through a volatile write, which the compiler cannot prove
// has no observable effect.
// ---------------------------------------------------------------------------

template <typename T>
inline void sink(T&& value)
{
    static volatile std::remove_reference_t<T> trap;
    trap = value;
    (void)trap;
}

// A no-arg version for "I just want a compiler fence here".
inline void fence()
{
    static volatile int trap = 0;
    trap = trap + 1;
}

// ---------------------------------------------------------------------------
// State — passed into each benchmark lambda. Drives the timed loop via the
// range-for protocol (begin()/end()/operator!=/operator++), same trick
// Google Benchmark uses so `for (auto _ : state)` reads naturally.
// ---------------------------------------------------------------------------

class State
{
  public:
    uint32_t n; // problem size for this run (e.g. entity count)

  private:
    using Clock = std::chrono::steady_clock;

    uint64_t target_iterations;
    uint64_t iterations_done = 0;

    Clock::time_point run_start;
    Clock::duration    paused_total{0};
    Clock::time_point  pause_started{};
    bool               is_paused = false;

  public:
    explicit State(uint32_t n_, uint64_t target_iterations_)
        : n(n_), target_iterations(target_iterations_)
    {}

    // Range-for protocol: `for (auto _ : state) { ... }`
    struct Iterator
    {
        State* state;
        bool   operator!=(const Iterator&) const { return state->iterations_done < state->target_iterations; }
        void   operator++()                       { ++state->iterations_done; }
        int    operator*() const                  { return 0; }
    };

    Iterator begin() { run_start = Clock::now(); return Iterator{this}; }
    Iterator end()   { return Iterator{this}; }

    // Exclude the time between pause()/resume() from the measured duration.
    // Use this to skip setup/teardown that lives inside the loop body but
    // isn't the operation you're trying to measure.
    void pause()
    {
        if (!is_paused) { pause_started = Clock::now(); is_paused = true; }
    }

    void resume()
    {
        if (is_paused) { paused_total += Clock::now() - pause_started; is_paused = false; }
    }

    // Total elapsed wall-clock time across all iterations, excluding any
    // paused intervals. Call after the loop completes.
    [[nodiscard]] double elapsed_seconds(Clock::time_point run_end) const
    {
        auto wall = (run_end - run_start) - paused_total;
        return std::chrono::duration<double>(wall).count();
    }

    [[nodiscard]] uint64_t iterations() const { return iterations_done; }
};

// ---------------------------------------------------------------------------
// One named benchmark, run across a list of problem sizes.
// ---------------------------------------------------------------------------

struct Result
{
    std::string name;
    uint32_t    n;
    uint64_t    iterations;
    double      total_seconds;
    double      ns_per_op; // total_seconds / iterations / n, in nanoseconds
};

class Suite
{
  public:
    // min_time: keep iterating until at least this much wall-clock time has
    // elapsed for a given (name, n) run — mirrors Google Benchmark's default
    // auto-tuning so fast operations still get enough samples to be stable.
    explicit Suite(double min_time_seconds = 0.5, uint64_t min_iterations = 8)
        : min_time(min_time_seconds), min_iters(min_iterations)
    {}

    // fn signature: void(State&)
    void add(std::string name, std::vector<uint32_t> sizes, std::function<void(State&)> fn)
    {
        cases.push_back({std::move(name), std::move(sizes), std::move(fn)});
    }

    void run()
    {
        std::vector<Result> results;

        for (auto& c : cases) {
            for (uint32_t n : c.sizes) {
                results.push_back(run_one(c.name, n, c.fn));
            }
        }

        print_table(results);
    }

  private:
    struct Case
    {
        std::string                 name;
        std::vector<uint32_t>       sizes;
        std::function<void(State&)> fn;
    };

    double   min_time;
    uint64_t min_iters;
    std::vector<Case> cases;

    Result run_one(const std::string& name, uint32_t n, const std::function<void(State&)>& fn)
    {
        // First pass: run a small number of iterations to estimate cost per
        // iteration, then decide how many iterations are needed to hit
        // min_time. This avoids either (a) running a slow op for way longer
        // than needed, or (b) running a fast op so few times the timer
        // resolution dominates the measurement.
        uint64_t target = min_iters;

        for (int attempt = 0; attempt < 6; ++attempt) {
            State st(n, target);
            fn(st);
            auto run_end = std::chrono::steady_clock::now();
            double elapsed = st.elapsed_seconds(run_end);

            bool long_enough  = elapsed >= min_time;
            bool enough_iters = st.iterations() >= min_iters;

            if (long_enough && enough_iters) {
                return Result{
                    name, n, st.iterations(), elapsed,
                    (elapsed / static_cast<double>(st.iterations()) / static_cast<double>(n)) * 1e9
                };
            }

            // Extrapolate how many iterations would hit min_time, with a
            // safety multiplier since the next run also pays cold-cache /
            // branch-predictor warmup again.
            double per_iter = elapsed / static_cast<double>(std::max<uint64_t>(st.iterations(), 1));
            uint64_t needed = per_iter > 0.0
                ? static_cast<uint64_t>((min_time / per_iter) * 1.5) + 1
                : target * 4;
            target = std::max(needed, target * 2);
        }

        // Fallback: report whatever the last attempt measured.
        State st(n, target);
        fn(st);
        auto run_end = std::chrono::steady_clock::now();
        double elapsed = st.elapsed_seconds(run_end);
        return Result{
            name, n, st.iterations(), elapsed,
            (elapsed / static_cast<double>(std::max<uint64_t>(st.iterations(), 1)) / static_cast<double>(n)) * 1e9
        };
    }

    static void print_table(const std::vector<Result>& results)
    {
        std::printf("%-28s %10s %12s %12s %14s\n",
                    "benchmark", "n", "iterations", "total (ms)", "ns/op");
        std::printf("%-28s %10s %12s %12s %14s\n",
                    "---------", "-", "----------", "----------", "-----");
        for (auto& r : results) {
            std::printf("%-28s %10u %12llu %12.3f %14.2f\n",
                        r.name.c_str(),
                        r.n,
                        static_cast<unsigned long long>(r.iterations),
                        r.total_seconds * 1e3,
                        r.ns_per_op);
        }
    }
};

} // namespace bench
