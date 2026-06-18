# ECS Architecture: File Organisation & Modernization Steps

---

## The core problem: two registration points

Right now component types are declared in two places:

- `component_list.hpp` — the `Components` type list (source of truth for IDs)
- `system_list.hpp` — the `Systems` type list, which forward-declares system classes

And system types pull in component types indirectly through `system_base.hpp` →
`component_manager.hpp` → `component_list.hpp`. This chain works but it means
`system_list.hpp` has to forward-declare each system class by hand, and
`system_manager_inst.hpp` exists solely to break the circular dependency that
arises when `SystemManagerImpl` is templated on system types that haven't been
fully defined yet.

The circular dependency is:

```
system_list.hpp         forward-declares PhysicsSystem
system_manager.hpp      includes system_list.hpp, system_base.hpp
system_base.hpp         includes component_manager.hpp
physics_system.hpp      includes system_base.hpp  ← needs full ComponentManager
system_manager_inst.hpp includes system_manager.hpp + physics_system.hpp
                        ← resolves the circle by being the only place
                          that sees everything at once
```

`system_manager_inst.hpp` is a workaround file. It shouldn't need to exist.

---

## The fix: a single registration header per domain

### Principle

There are two "registration moments" — the two places where you name all your
types in a canonical ordered list:

1. **Component registration** — names all component types, assigns their IDs
2. **System registration** — names all system types, assigns their IDs

Each registration moment needs exactly one file. Everything else either feeds
into it (component/system definitions) or reads from it (managers, world).

The key insight is that `SystemList` only needs forward declarations of system
classes to build the ID table — it does NOT need the full class definitions.
Full definitions are only needed when `SystemManagerImpl<Ts...>` is
instantiated, which is a separate step.

### Target file layout

```
ecs/
  common.hpp              — Entity, Signature, Schedule, constants, concepts
                            no includes of your own headers

  components/
    2d_comps.hpp          — plain structs: Vec2, Transform2, RigidBody2, ...
                            no ECS includes at all

  component_registry.hpp  — THE single source of truth for component types
                            includes 2d_comps.hpp + common.hpp
                            defines ComponentList, component_id<T>, Components

  component_manager.hpp   — ComponentArray<T>, ComponentManagerImpl, ComponentManager
                            includes component_registry.hpp

  entity_manager.hpp      — EntityManager
                            includes common.hpp

  system_base.hpp         — SystemBase<Derived, Ts...> CRTP base
                            includes component_manager.hpp, common.hpp

  systems/
    physics_system.hpp    — class PhysicsSystem : public SystemBase<...>
                            includes system_base.hpp, 2d_comps.hpp
    render_system.hpp     — same pattern
    ...

  system_registry.hpp     — THE single source of truth for system types
                            forward-declares each system class
                            defines SystemList, system_id<T>, Systems
                            includes common.hpp ONLY — no system_base.hpp,
                            no physics_system.hpp, nothing that drags in
                            component_manager.hpp through the back door

  system_manager.hpp      — SystemManagerImpl<Ts...>
                            includes system_base.hpp + system_registry.hpp

  archetype.hpp           — Archetype<Ts...> with consteval signature()
                            includes component_registry.hpp

  world.hpp               — World: owns EntityManager, ComponentManager,
                            SystemManager
                            includes component_manager.hpp,
                                     entity_manager.hpp,
                                     system_manager.hpp,
                                     systems/physics_system.hpp  ← full defs
                                     systems/render_system.hpp
                                     archetype.hpp

  ecs.hpp                 — single convenience include for game code
                            includes world.hpp
                            optionally includes schedule_constants.hpp
```

### Why this kills the circular dependency

`system_registry.hpp` only forward-declares system classes — it does not include
`system_base.hpp` or any system headers. The forward declaration is enough to
instantiate `SystemList<PhysicsSystem, RenderSystem>` and compute `system_id<T>`
because those are purely type-level operations on the names, not on the
definitions.

The full definitions land in `world.hpp`, which is the one file that needs to
see everything. `world.hpp` is never included by anything inside the ECS itself
— it is the root. So there is no cycle.

`system_manager_inst.hpp` is deleted entirely. `world.hpp` does its job.

### Dependency graph (after)

```
common.hpp
  └─ entity_manager.hpp

2d_comps.hpp
  └─ component_registry.hpp
       └─ component_manager.hpp
            └─ system_base.hpp
                 └─ systems/physics_system.hpp
                 └─ systems/render_system.hpp

common.hpp
  └─ system_registry.hpp          ← forward decls only, no system_base.hpp
       └─ system_manager.hpp
            └─ system_base.hpp    ← system_manager needs full SystemBase

world.hpp
  ├─ component_manager.hpp
  ├─ entity_manager.hpp
  ├─ system_manager.hpp
  ├─ systems/physics_system.hpp   ← full defs needed to instantiate tuple
  ├─ systems/render_system.hpp
  └─ archetype.hpp
```

Every arrow points downward. No cycles.

### One place to add a component

Open `component_registry.hpp`, add the type to `Components`:

```cpp
using Components = ComponentList<
    Transform2,   // ID 0
    RigidBody2,   // ID 1
    Gravity2,     // ID 2
    Sprite,       // ID 3  ← add here
>;
```

That's it. The static asserts at the bottom catch mistakes immediately.

### One place to add a system

Open `system_registry.hpp`, forward-declare the class and add it to `Systems`:

```cpp
class PhysicsSystem;
class RenderSystem;    // ← add forward decl here

using Systems = SystemList<
    PhysicsSystem,   // ID 0
    RenderSystem,    // ID 1  ← add here
>;
```

Then add `#include "systems/render_system.hpp"` in `world.hpp`. That's the only
other file that needs to change.

---

## Modernization steps (ordered by dependency)

Each step says what file(s) to touch and what the end state looks like.

---

### Step 1 — Rename and clean up registration headers

**Files**: rename `component_list.hpp` → `component_registry.hpp`,
rename `system_list.hpp` → `system_registry.hpp`.

Fix up all includes. No logic changes yet — just establishing the canonical names
so the rest of the steps are unambiguous.

Also fix the `system_registry.hpp` static assert: `PhysicsSystem` is forward-
declared there but the assert `system_id<PhysicsSystem> == 0` requires the type
to be complete for `SystemType_t` check in `system_manager_inst.hpp`. Move the
`static_assert(SystemType_t<PhysicsSystem>)` into `world.hpp` instead, where
the full definition is visible.

---

### Step 2 — Delete `system_manager_inst.hpp`, move its job to `world.hpp`

**Files**: delete `system_manager_inst.hpp`, edit `world.hpp`.

`world.hpp` becomes the file that includes all full system definitions and
instantiates `SystemManager`. Add to `world.hpp`:

```cpp
#include "system_manager.hpp"
#include "systems/physics_system.hpp"  // full definition
// #include "systems/render_system.hpp"

template <typename List>
struct make_system_manager;

template <SystemType_t... Ts>
struct make_system_manager<SystemList<Ts...>> {
    using type = SystemManagerImpl<Ts...>;
};

using SystemManager = make_system_manager<Systems>::type;

static_assert(SystemType_t<PhysicsSystem>);
```

`world.hpp` already includes all of these transitively — this just makes it
explicit and removes the awkward intermediary file.

---

### Step 3 — Fix the bug in `system_manager.hpp`

**File**: `system_manager.hpp`.

`on_signature_change` calls `check_and_add_entity` but the method is defined as
`check_and_update_entity`. Pick one name and use it consistently:

```cpp
void on_signature_change(Entity e, Signature new_sig)
{
    std::apply([&](auto&... sys) -> void {
        (check_and_update_entity(sys, e, new_sig), ...);  // was: check_and_add_entity
    }, systems);
}
```

---

### Step 4 — Add sparse/dense policy to `ComponentArray`

**File**: `component_manager.hpp` (or split into `component_array.hpp` if it gets
large).

Add a `component_traits` external trait (keeps component structs clean):

```cpp
// in component_registry.hpp, after the component structs are included
template <typename T> struct component_traits {
    static constexpr bool is_sparse = false;  // dense by default
};
// specialise for sparse components:
// template <> struct component_traits<Lifetime> { static constexpr bool is_sparse = true; };
```

Then in `ComponentArray<T>`:

```cpp
static constexpr bool sparse = component_traits<T>::is_sparse;

using IndexMap = std::conditional_t<
    sparse,
    std::flat_map<Entity, u32>,
    std::array<u32, MAX_ENTITIES>
>;
```

Add private helpers `get_idx`, `set_idx`, `clear_idx` that branch on `if constexpr
(sparse)` — the public interface (`add_data`, `remove_data`, `get_data`,
`has_data`, `remove_if_present`) stays identical.

Also expose the sequential access API needed for step 6:

```cpp
auto active_count() const -> u32      { return data.size(); }
auto entity_at(u32 i) const -> Entity { return idx_to_entity[i]; }
auto data_at(u32 i)         -> T&     { return data[i]; }
auto data_at(u32 i) const   -> const T& { return data[i]; }
```

---

### Step 5 — Add `make_schedule` and named schedule constants

**New file**: `schedule_constants.hpp`. Include it from `ecs.hpp`.

```cpp
#pragma once
#include "system_registry.hpp"
#include "common.hpp"

template <typename... Ts>
static constexpr Schedule make_schedule() {
    Schedule s{};
    (s.set(system_id<Ts>), ...);
    return s;
}

// Add one line per named schedule your game needs.
// These are computed at compile time — zero runtime cost.
constexpr Schedule PHYSICS_ONLY = make_schedule<PhysicsSystem>();
// constexpr Schedule RENDER_ONLY  = make_schedule<RenderSystem>();
// constexpr Schedule ALL_SYSTEMS  = make_schedule<PhysicsSystem, RenderSystem>();
```

Game loop becomes:

```cpp
while (!WindowShouldClose()) {
    float dt = GetFrameTime();
    world.update(dt, PHYSICS_ONLY);
    BeginDrawing();
    world.update(dt, RENDER_ONLY);
    EndDrawing();
}
```

---

### Step 6 — Direct array iteration in systems

**Files**: each system's `.hpp`.

Instead of iterating `dense` (the entity list) and doing a lookup per component,
drive the loop from the smallest component array directly. For `PhysicsSystem`
that's `Gravity2` (fewest entities will have it):

```cpp
void update_impl(float dt) {
    auto& gravities    = comp_manager.get_arr<Gravity2>();
    auto& rigidbodies  = comp_manager.get_arr<RigidBody2>();
    auto& transforms   = comp_manager.get_arr<Transform2>();

    for (u32 i = 0; i < gravities.active_count(); ++i) {
        Entity e      = gravities.entity_at(i);    // sequential — free
        auto&  g      = gravities.data_at(i);      // sequential — free
        auto&  rb     = rigidbodies.get_data(e);   // one random lookup
        auto&  t      = transforms.get_data(e);    // one random lookup

        rb.v.x += g.force.x * dt;
        rb.v.y += g.force.y * dt;
        t.pos.x += rb.v.x * dt;
        t.pos.y += rb.v.y * dt;
    }
}
```

The system signature still guarantees every entity in `gravities` also has
`Transform2` and `RigidBody2` — so the lookups are always valid.

Do this per-system, profiling first to confirm which array is smallest. For
systems with uniform density across all their components (e.g. every entity has
`Transform2`), iterating `dense` directly with `data_at` on every array is fine.

---

### Step 7 — Implement `Archetype::for_each_type` and `World::create_from_archetype`

**Files**: `archetype.hpp`, `world.hpp`.

The `Overload 1` (default-constructed) form needs `Archetype` to expose its type
pack. Add a static template method:

```cpp
template <typename... Ts>
struct Archetype {
    static consteval auto signature() -> Signature { ... }  // already done

    // Calls f.operator()<T>() for each T in the pack
    template <typename F>
    static void for_each_type(F&& f) {
        (f.template operator()<Ts>(), ...);
    }
};
```

Then the two `World` overloads both work:

```cpp
// Overload 1: default-constructed
template <typename A>
auto create_from_archetype() -> Entity {
    Entity e = entity_manager.create();
    A::for_each_type([&]<typename T>() {
        component_manager.get_arr<T>().add_data(e, T{});
        entity_manager.set_component(e, component_id<T>);
    });
    system_manager.on_signature_change(e, entity_manager.get_signature(e));
    return e;
}

// Overload 2: caller-supplied values
template <typename A, typename... Args>
auto create_from_archetype(Args&&... args) -> Entity {
    Entity e = entity_manager.create();
    (add_component(e, std::forward<Args>(args)), ...);
    return e;
}
```

Note: the current `add_component` in `World` does not update the entity
signature or notify systems — that needs to be fixed at the same time (the old
`ecs.hpp` World did this correctly; the new one dropped it).

---

### Step 8 — Fix `World::add_component` (missing signature update)

**File**: `world.hpp`.

The current implementation only calls `add_data` — it never updates the entity's
signature in `EntityManager` or calls `on_signature_change`. That means systems
never see the entity. Fix:

```cpp
template <ComponentType_t T>
void add_component(Entity e, T comp)
{
    component_manager.get_arr<T>().add_data(e, std::move(comp));
    entity_manager.set_component(e, component_id<T>);                  // ← missing
    system_manager.on_signature_change(e, entity_manager.get_signature(e)); // ← missing
}

template <ComponentType_t T>
void remove_component(Entity e)
{
    component_manager.get_arr<T>().remove_data(e);
    entity_manager.unset_component(e, component_id<T>);                // ← missing
    system_manager.on_signature_change(e, entity_manager.get_signature(e)); // ← missing
}
```

This is a correctness bug — entities added to the world via `add_component` will
never appear in any system's entity list.

---

## Summary: what to do, in order

| # | What | Files touched |
|---|------|---------------|
| 1 | Rename registration headers | `component_list.hpp` → `component_registry.hpp`, `system_list.hpp` → `system_registry.hpp`, fix all includes |
| 2 | Delete `system_manager_inst.hpp`, move content to `world.hpp` | `world.hpp`, delete `system_manager_inst.hpp` |
| 3 | Fix `check_and_add_entity` / `check_and_update_entity` name mismatch | `system_manager.hpp` |
| 4 | Fix `World::add_component` / `remove_component` (missing signature update) | `world.hpp` |
| 5 | Sparse/dense policy on `ComponentArray` + sequential access API | `component_manager.hpp` |
| 6 | `make_schedule` + named schedule constants | new `schedule_constants.hpp` |
| 7 | Direct array iteration in systems | `systems/physics_system.hpp` (and others as written) |
| 8 | `Archetype::for_each_type` + `World::create_from_archetype` | `archetype.hpp`, `world.hpp` |

Steps 1–4 are correctness fixes — the ECS is not fully functional without them.
Steps 5–8 are the remaining modernization features from `ecs_modernization.md`.
