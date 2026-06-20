# wasi-ecs

A small, header-only, compile-time Entity Component System for C++23, built for
a 2D game engine on top of raylib. No runtime polymorphism, no type erasure,
no virtual calls anywhere in the hot path.

---

## Design

- **Entities** are just `u32` indices.
- **Components** are plain structs. Each type gets a permanent compile-time ID
  from its position in a type list, resolved via `comp_type_index<T, List>` — a
  `constexpr`, not a runtime lookup.
- **Component storage** is a sparse set: one dense `std::vector<T>` per
  component type, with O(1) add/remove via swap-and-pop, and an
  `entity → index` sparse array for O(1) lookup.
- **Systems** are CRTP types (`SystemBase<Derived, CMgr, Components...>`), each
  with a `constexpr` signature computed from its component list. No `ISystem`
  interface, no vtable — the active systems live directly in a
  `std::tuple<Systems...>`, dispatched via fold expressions.
- **Archetypes** are named component bundles (`Archetype<Ts...>`) used for
  ergonomic batch entity creation — not a storage strategy. Components are
  still stored per-type, not per-archetype.

---

## Core Design Principles

**No virtual functions in the hot path.** Dispatch happens via CRTP
(compile-time) and fold expressions over tuples.

**No global type lists.** `component_id<T>` and `system_id<T>` are not global
variables. They are defined in `game_registry.hpp` and scoped to specific lists.
Two different `World` instantiations in the same binary never collide.

**One file to edit.** `game_registry.hpp` is the only file you touch when adding
components or systems. Everything else is library infrastructure.

**Compile-time IDs.** Component and system IDs are `constexpr u8` values derived
from position in a type list. Resolved at compile time and embedded as integer
literals — not computed at runtime.

---

## Comparison

| | wasi-ecs | EnTT | flecs / bevy_ecs |
|---|---|---|---|
| Storage | sparse set per component type | sparse set per component type | archetype tables (grouped by exact component set) |
| Component ID | compile-time, `constexpr` | runtime, type-erased | runtime, cached |
| System dispatch | `std::tuple` + fold expression, no vtable | none built in — you write `view<T...>()` loops | scheduler with dependency analysis |
| Add/remove component | O(1), no migration | O(1), no migration | moves entity's row between tables |
| Multi-component iteration | probe smallest array, lookup the rest | same strategy | contiguous — components pre-grouped |
| Runtime type registration | no — fixed list at compile time | yes | yes |

In short: architecturally close to EnTT's core (sparse-set storage, same
swap-and-pop removal), but trades EnTT's runtime flexibility for compile-time
component/system IDs and zero type erasure. Does not do archetype-grouped
storage like flecs or bevy, so multi-component iteration isn't as cache-optimal
— the tradeoff is no archetype migration cost when entities gain or lose
components, which is frequent in small 2D games.

---

## Status

Core (entities, components, systems, signatures, scheduling) is implemented
and working. Sparse/dense storage policy per component type is in progress.

---

## File Map

```
include/
├── wasi.hpp                    Type aliases (u8, u32, uptr, etc.)
└── ecs/
    ├── common.hpp              Core types, constants, concepts
    ├── component_registry.hpp  ComponentList<Ts...> and comp_type_index<T,List>
    ├── component_manager.hpp   ComponentArray<T> and ComponentManagerImpl<Ts...>
    ├── system_registry.hpp     SystemList<Ts...> and sys_type_index<T,List>
    ├── system_base.hpp         SystemBase<Derived,CMgr,CTs...> CRTP base
    ├── system_manager.hpp      SystemManagerImpl<Ss...> and make_system_manager
    ├── entity_manager.hpp      EntityManager — ID issuing and signatures
    ├── archetype.hpp           Archetype<CList,Ts...> — entity templates
    ├── world.hpp               World<CList,SList> — the public API
    ├── game_registry.hpp       YOUR FILE — lists, aliases, GameWorld, schedules
    ├── components/
    │   └── 2d_comps.hpp        Transform2, RigidBody2, Gravity2, Vec2
    └── systems/
        ├── gravity_system.hpp
        ├── movement_system.hpp
        └── physics_system.hpp
```

**Rule:** library files (`common` through `world`) never include
`game_registry.hpp` or any concrete system header. Dependency flows one way only.

---

## Dependency Graph

Arrows mean "includes / depends on". This graph is acyclic.

```
wasi.hpp  (external)
    │
    ▼
common.hpp
[Entity, Signature, ComponentType, SystemType, Schedule, INVALID,
 ComponentType_t concept, SystemType_t concept]
    │
    ├──────────────────────────┐
    ▼                          ▼
component_registry.hpp    system_registry.hpp
[ComponentList,           [SystemList,
 comp_type_index]          sys_type_index]
    │                          │
    ▼                          │
component_manager.hpp          │
[ComponentArray,               │
 ComponentManagerImpl,         │
 make_component_manager]       │
    │                          │
    ▼                          │
system_base.hpp ───────────────┘
[make_signature,
 SystemBase<D,CMgr,CTs...>]
    │
    ▼
system_manager.hpp
[SystemManagerImpl,
 make_system_manager]
    │
    │    entity_manager.hpp   archetype.hpp
    │    [EntityManager]      [Archetype<CList,Ts...>]
    │           │                    │
    └───────────┴────────────────────┘
                │
                ▼
            world.hpp
            [World<CList,SList>]
                │
                ▼
        game_registry.hpp  ← YOU EDIT THIS
        [MyComponents, MySystems,
         component_id<T>, system_id<T>,
         ComponentManager alias,
         GameWorld, Schedules, Archetypes]
                │
                ├── systems/gravity_system.hpp
                ├── systems/movement_system.hpp
                └── systems/physics_system.hpp
```

**Why `SystemType_t` is in `common.hpp` not `system_base.hpp`:** If it lived in
`system_base.hpp`, then `system_registry.hpp` would need to include
`system_base.hpp`, which includes `component_manager.hpp`. But
`system_manager.hpp` includes both — you'd get a cycle. `common.hpp` has no
upstream dependencies, so it's the safe place for concepts shared across the
graph.

---

## Every File Explained

### `wasi.hpp`

Just type aliases — nothing else depends on this except it wrapping `<cstdint>`.

```cpp
namespace wasi {
    using u8  = uint8_t;
    using u32 = uint32_t;
    using u64 = uint64_t;
    // ...smart pointer aliases (uptr, sptr, wptr)
}
```

---

### `common.hpp`

The foundation. Defines all core types, configuration constants, and concepts.

```cpp
constexpr u32 MAX_ENTITIES  = 1000;
constexpr u8  MAX_COMPONENTS = 32;
constexpr u8  MAX_SYSTEMS    = 16;
constexpr u32 PRE_INIT_SIZE  = 100;   // vector reserve hint

using Entity        = u32;
using Signature     = std::bitset<MAX_COMPONENTS>;
using ComponentType = u8;
using SystemType    = u8;
using Schedule      = std::bitset<MAX_SYSTEMS>;
constexpr u32 INVALID = std::numeric_limits<u32>::max();
```

**`ComponentType_t` concept** — a valid component must be:
- `default_initializable` (so `ComponentArray` can pre-allocate)
- `movable` (so data can be moved in without copying)

**`SystemType_t` concept** — a valid system must expose:
- `update_impl(float dt)`
- `add_entity / remove_entity / has_entity`

---

### `component_registry.hpp`

Defines `ComponentList` and the compile-time ID lookup.

```cpp
template <ComponentType_t... Ts>
struct ComponentList {
    static constexpr u8 count = sizeof...(Ts);
};
```

`ComponentList` is just a type-pack wrapper. Its only job is carrying a set of
types so `comp_type_index` can unpack them.

```cpp
// Primary template — intentionally undefined (hard error if T not in a ComponentList)
template <typename T, typename List>
struct comp_type_index;

// Partial specialization — fires when List = ComponentList<Ts...>
template <typename T, typename... Ts>
struct comp_type_index<T, ComponentList<Ts...>> {
    static constexpr u8 value = []() consteval -> u8 {
        constexpr bool matches[] { std::is_same_v<T, Ts>... };
        for (u8 i = 0; i < sizeof...(Ts); ++i)
            if (matches[i]) return i;
        return std::numeric_limits<u8>::max(); // caught by static_asserts in game_registry.hpp
    }();
};
```

The `consteval` lambda runs at compile time. `std::is_same_v<T, Ts>...` expands
to a `bool` array — one entry per type in the list. The loop finds the `true`
entry and returns its index. That index is the permanent component ID.

---

### `component_manager.hpp`

Two classes: `ComponentArray<T>` (storage for one component type) and
`ComponentManagerImpl<Ts...>` (holds all arrays in a tuple).

#### `ComponentArray<T>` — dense/sparse storage

```
entity_to_idx[e] = i     sparse: which slot does entity e use? (INVALID = none)
data[i]          = T{}   dense:  component value at slot i
idx_to_entity[i] = e     reverse: which entity owns slot i (needed for swap-and-pop)
```

**Add** (`add_data`) — O(1):
```
entity_to_idx[e] = data.size()
data.push_back(comp)
idx_to_entity.push_back(e)
```

**Remove** (`remove_data`) — O(1) swap-and-pop:
```
idx          = entity_to_idx[e]
last_entity  = idx_to_entity.back()

if idx != last_idx:                      ← guard: removing the last element is a no-op swap
    data[idx]                = move(data.back())
    idx_to_entity[idx]       = last_entity
    entity_to_idx[last_entity] = idx

entity_to_idx[e] = INVALID
data.pop_back()
idx_to_entity.pop_back()
```

**Direct sequential access** — used by systems to drive iteration from the
smallest component array instead of going through the entity set:
```cpp
for (u32 i = 0; i < arr.entity_count(); ++i) {
    Entity e = arr.entity_at(i);   // idx_to_entity[i]  — sequential
    T&     v = arr.data_at(i);     // data[i]            — sequential
}
```

#### `ComponentManagerImpl<Ts...>`

```cpp
std::tuple<ComponentArray<Ts>...> arrays;
using ListType = ComponentList<Ts...>;
```

`ListType` is exported so `SystemBase` can call
`comp_type_index<T, CMgr::ListType>::value` without needing the list as a
separate template parameter.

`get_arr<T>()` is `std::get<ComponentArray<T>>(arrays)` — resolved at compile
time, zero runtime overhead.

`entity_destroyed(e)` uses a fold expression over `std::apply` — no loop,
no vtable, no checking which components the entity has:
```cpp
std::apply([&](auto&... arr) {
    (arr.remove_if_present(e), ...);
}, arrays);
```

---

### `system_registry.hpp`

Mirror of `component_registry.hpp` for systems.

```cpp
template <SystemType_t... Ts>
struct SystemList {
    static constexpr u8 count = sizeof...(Ts);
};
```

`sys_type_index<T, SystemList<Ts...>>` works identically to `comp_type_index`.

**This file has zero system header includes.** System headers are included in
`game_registry.hpp` after the lists are defined. `SystemList` only needs type
names to exist — the `SystemType_t` constraint is checked at instantiation time,
not at the point of the `using MySystems = ...` declaration.

---

### `system_base.hpp`

The CRTP base for all systems.

```cpp
template <typename Derived, typename CMgr, typename... ComponentTypes>
class SystemBase { ... };
```

**Why template on `CMgr` instead of `ComponentList`?**

`SystemBase` needs two things: a reference to the component manager (to call
`get_arr<T>()` inside `get_component<T>(e)`) and the component list (to compute
the system's signature via `comp_type_index`). Templating on `CMgr` gives both:
`CMgr&` is the reference to store, and `CMgr::ListType` is the component list,
accessible without a separate template parameter.

**Signature computation:**
```cpp
static constexpr Signature signature =
    make_signature<typename CMgr::ListType, ComponentTypes...>();
```

`make_signature` is a free `consteval` function — runs once at compile time:
```cpp
template <typename CList, typename... Ts>
consteval auto make_signature() -> Signature {
    Signature sig{};
    ((sig.set(comp_type_index<Ts, CList>::value)), ...);
    return sig;
}
```

**Entity set** — sparse set inside every system:
```
dense[i]   = entity at position i    contiguous, iteration is sequential
sparse[e]  = index of e in dense     O(1) lookup
```

`has_entity(e)` uses a triple check to guard against stale sparse entries after
swap-and-pop:
```cpp
return sparse[e] != INVALID
    && sparse[e] < static_cast<u32>(dense.size())
    && dense[sparse[e]] == e;
```

**CRTP dispatch** — no vtable:
```cpp
void update(float dt) {
    static_cast<Derived*>(this)->update_impl(dt);
}
```

---

### `system_manager.hpp`

```cpp
template <SystemType_t... Systems>
class SystemManagerImpl {
    std::tuple<Systems...> systems;
    using MySysList = SystemList<Systems...>;
};
```

Systems live inline in the tuple — no heap, no pointers.

**Constructor:** the pack expansion constructs each system with the same
component manager reference:
```cpp
template <typename CMgr>
explicit SystemManagerImpl(CMgr& cm) : systems(Systems(cm)...) {}
```

**`on_signature_change`** — called when an entity gains or loses a component:
```cpp
std::apply([&](auto&... sys) {
    (check_and_update_entity(sys, e, new_sig), ...);
}, systems);
```

`check_and_update_entity` checks if `new_sig` contains all bits required by
the system's signature. Adds the entity if it now qualifies; removes it if it
no longer does.

**`update`** — the fold expression that replaces the vtable loop:
```cpp
std::apply([&](auto&... sys) {
    ((schedule.test(
        sys_type_index<std::remove_cvref_t<decltype(sys)>, MySysList>::value
    ) ? sys.update(dt) : void()), ...);
}, systems);
```

`sys_type_index<T, MySysList>::value` is a compile-time constant. The bit test
is evaluated per-system at compile time; the actual `update(dt)` call happens
at runtime only if the bit is set.

---

### `entity_manager.hpp`

Issues and recycles entity IDs. Stores per-entity component signatures.

```
next_id          next fresh ID to issue
free_ids         recycled IDs waiting for reuse
signatures[e]    which components entity e currently has (Signature bitset)
```

`set_component(e, c)` sets bit `c` in `signatures[e]`. Called by
`World::add_component` after component data is stored.

`get_signature(e)` returns the current bitset. Used by `SystemManager` to
check system qualification after every component change.

---

### `archetype.hpp`

Named bundles of component types. Not a storage strategy — components are still
stored per-type in `ComponentArray<T>`.

```cpp
template <typename CList, typename... Ts>
struct Archetype {
    static consteval auto signature() -> Signature;
    static constexpr  auto size()      -> u8;
    template <typename F>
    static constexpr  void for_each_type(F f);
    template <typename T>
    static constexpr  auto contains()  -> bool;
};
```

`CList` must be passed explicitly because there is no global `ComponentList`.
In practice you use `GameWorld::Archetype<Ts...>` which binds `CList`
automatically via the member alias in `World`.

`for_each_type` is used by `World::create_from_archetype`:
```cpp
A::for_each_type([&]<typename T>() {
    component_manager.get_arr<T>().add_data(e, T{});
    entity_manager.set_component(e, component_id<T>);
});
```

The lambda `[&]<typename T>()` uses a C++20 explicit template parameter in a
generic lambda. `for_each_type` expands it via fold: `(f.template operator()<Ts>(), ...)`.

---

### `world.hpp`

The public API. Uses partial specialization to unpack both lists:

```cpp
template <typename... CList, typename... SList>
class World<ComponentList<CList...>, SystemList<SList...>> {
    EntityManager    entity_manager;
    ComponentManager component_manager{};
    SystemManager    system_manager{component_manager};   // takes CMgr& — must come after
};
```

Declaration order matters: `component_manager` must be fully constructed before
`system_manager` because systems hold a `CMgr&` reference to it.

**Scoped IDs** — never collide across two `World` instantiations:
```cpp
template <typename T>
static constexpr ComponentType component_id =
    comp_type_index<T, MyComponentList>::value;
```

**`make_schedule<Ts...>()`** — static `consteval`, builds a `Schedule` bitset:
```cpp
template <typename... Ts>
static consteval auto make_schedule() -> Schedule {
    Schedule s{};
    (s.set(sys_type_index<Ts, MySystemList>::value), ...);
    return s;
}
```

**`Archetype<Ts...>`** — member alias that binds `CList`:
```cpp
template <typename... Ts>
using Archetype = ::Archetype<MyComponentList, Ts...>;
```

---

### `game_registry.hpp` — the one file you edit

Include order inside this file is mandatory. Each step depends on the previous.

```
Step  Defines                     Needed by
────  ─────────────────────────   ──────────────────────────────────────────
1     world.hpp included          brings in all library templates (unevaluated)
2     2d_comps.hpp                component structs exist as types
3     MyComponents                comp_type_index can now be instantiated
4     component_id<T>             convenience alias, static_asserts
5     ComponentManager alias      system headers need the concrete CMgr type
6     system headers              GravitySystem<CMgr> etc. now fully defined
7     MySystems                   sys_type_index can now be instantiated
8     system_id<T>                convenience alias, static_asserts
9     GameWorld                   both lists exist, World<CL,SL> instantiates
10    constexpr Schedules         GameWorld::make_schedule uses MySystems
11    Archetype aliases           GameWorld::Archetype<Ts...> binds MyComponents
```

```cpp
// 1
#include "world.hpp"
// 2
#include "2d_comps.hpp"
using namespace ECS_COMPS_2D;
// 3
using MyComponents = ComponentList<Transform2, RigidBody2, Gravity2>;
// 4
template <typename T>
constexpr ComponentType component_id = comp_type_index<T, MyComponents>::value;
static_assert(component_id<Transform2> == 0);
static_assert(component_id<RigidBody2> == 1);
static_assert(component_id<Gravity2>   == 2);
// 5
using ComponentManager = make_component_manager<MyComponents>::type;
// 6
#include "gravity_system.hpp"
#include "movement_system.hpp"
#include "physics_system.hpp"
// 7
using MySystems = SystemList<
    GravitySystem<ComponentManager>,    // bit 0
    MovementSystem<ComponentManager>,   // bit 1
    PhysicsSystem<ComponentManager>     // bit 2
>;
// 8
template <typename T>
constexpr SystemType system_id = sys_type_index<T, MySystems>::value;
// 9
using GameWorld = World<MyComponents, MySystems>;
// 10
constexpr Schedule GRAVITY_AND_MOVEMENT =
    GameWorld::make_schedule<GravitySystem<ComponentManager>, MovementSystem<ComponentManager>>();
constexpr Schedule PHYSICS_ONLY =
    GameWorld::make_schedule<PhysicsSystem<ComponentManager>>();
constexpr Schedule ALL_SYSTEMS =
    GameWorld::make_schedule<
        GravitySystem<ComponentManager>,
        MovementSystem<ComponentManager>,
        PhysicsSystem<ComponentManager>
    >();
// 11
using PlayerArchetype = GameWorld::Archetype<Transform2, RigidBody2>;
using BulletArchetype = GameWorld::Archetype<Transform2, RigidBody2, Gravity2>;
```

---

## Data Flows

### Adding a component

```cpp
world.add_component<Gravity2>(e, Gravity2{{0, -9.8f}});
```

```
World::add_component<Gravity2>(e, comp)
    │
    ├─ component_manager.get_arr<Gravity2>()          ← std::get in tuple, compile time
    │  .add_data(e, move(comp))
    │      └─ entity_to_idx[e] = data.size()
    │         data.push_back(comp)
    │         idx_to_entity.push_back(e)
    │
    ├─ entity_manager.set_component(e, component_id<Gravity2>)
    │      └─ signatures[e].set(2)
    │
    └─ system_manager.on_signature_change(e, signatures[e])
           └─ fold over all systems:
                  check_and_update_entity(sys, e, new_sig)
                      qualifies = (new_sig & sys.signature) == sys.signature
                      if qualifies && !has: sys.add_entity(e)
                      if !qualifies && has: sys.remove_entity(e)
```

### The update loop

```cpp
world.update(dt, GRAVITY_AND_MOVEMENT);
```

```
World::update(dt, schedule)
    └─ system_manager.update(dt, schedule)
           └─ std::apply fold over systems tuple:
                  schedule.test(sys_index) ? sys.update(dt) : void()

           expands to:
           ├─ schedule.test(0) → true  → GravitySystem::update(dt)
           │      → static_cast<GravitySystem*>(this)->update_impl(dt)
           │      → iterate Gravity2 array sequentially, one RigidBody2 random lookup
           │
           ├─ schedule.test(1) → true  → MovementSystem::update(dt)
           │      → iterate RigidBody2 array sequentially, one Transform2 random lookup
           │
           └─ schedule.test(2) → false → void()   ← PhysicsSystem skipped
```

### Destroying an entity

```cpp
world.destroy_entity(e);
```

Order is mandatory — systems and components must clean up before the ID is recycled:

```
World::destroy_entity(e)
    ├─ system_manager.on_entity_destroyed(e)
    │      └─ fold: sys.has_entity(e) ? sys.remove_entity(e) : void()
    │         O(1) per system: sparse[e] check, swap-and-pop if present
    │
    ├─ component_manager.entity_destroyed(e)
    │      └─ fold: arr.remove_if_present(e)
    │         one has_data(e) check per array, remove_data if present
    │
    └─ entity_manager.destroy(e)
           └─ signatures[e].reset()
              free_ids.push_back(e)
```

---

## How to Extend

### Adding a component

**1.** Define the struct:
```cpp
// components/my_comps.hpp
struct Health {
    float current;
    float max;
};
```

**2.** Add to `game_registry.hpp`:
```cpp
#include "components/my_comps.hpp"

using MyComponents = ComponentList<
    Transform2,   // ID 0  ← never reorder
    RigidBody2,   // ID 1
    Gravity2,     // ID 2
    Health        // ID 3  ← append at the end
>;
static_assert(component_id<Health> == 3);
```

That's it. The new component is automatically stored in `ComponentManagerImpl`'s
tuple, accessible via `world.add_component<Health>(e, Health{100, 100})`, and
queryable inside systems via `get_component<Health>(e)`.

---

### Adding a system

**1.** Create the system header:
```cpp
// systems/health_system.hpp
#pragma once
#include "system_base.hpp"
#include "my_comps.hpp"

template <typename CMgr>
class HealthSystem : public SystemBase<HealthSystem<CMgr>, CMgr, Health>
{
  public:
    using Base = SystemBase<HealthSystem<CMgr>, CMgr, Health>;
    explicit HealthSystem(CMgr& cm) : Base(cm) {}

    void update_impl(float dt) {
        auto& healths = this->comp_manager.template get_arr<Health>();
        for (u32 i = 0; i < healths.entity_count(); ++i) {
            Entity e = healths.entity_at(i);
            auto&  h = healths.data_at(i);
            // ... logic
        }
    }
};
```

**2.** Add to `game_registry.hpp` (after step 5 — `ComponentManager` alias):
```cpp
#include "systems/health_system.hpp"

using MySystems = SystemList<
    GravitySystem<ComponentManager>,    // bit 0  ← never reorder
    MovementSystem<ComponentManager>,   // bit 1
    PhysicsSystem<ComponentManager>,    // bit 2
    HealthSystem<ComponentManager>      // bit 3  ← append at the end
>;
static_assert(system_id<HealthSystem<ComponentManager>> == 3);

constexpr Schedule HEALTH_UPDATE =
    GameWorld::make_schedule<HealthSystem<ComponentManager>>();
```

---

## Memory Layout

### `ComponentManagerImpl` — all arrays inline in a tuple

```
Stack/inline:
┌──────────────────────┬──────────────────────┬──────────────────────┐
│ ComponentArray<T2>   │ ComponentArray<RB2>  │ ComponentArray<G2>   │
│  vector<T2>  data ──→heap                  │                      │
│  vector<Entity> ────→heap                  │                      │
│  array<u32,1000>  ← 4KB on stack/in-place  │                      │
│  entity_to_idx       │                      │                      │
└──────────────────────┴──────────────────────┴──────────────────────┘
 One 4KB sparse array per component type. Dense data heap-allocated by vector.
 With MAX_ENTITIES=1000 and 32 component types: ~128KB of sparse arrays total.
```

### `SystemManagerImpl` — all systems inline in a tuple

```
Stack/inline:
┌──────────────────────────┬──────────────────────────┐
│ GravitySystem<CMgr>      │ MovementSystem<CMgr>     │
│  vector<Entity> dense    │  vector<Entity> dense    │
│  array<u32,1000> sparse  │  array<u32,1000> sparse  │
│  CMgr& comp_manager ─────┤──────────────────────────┤→ same ComponentManager
└──────────────────────────┴──────────────────────────┘
 All systems hold a reference to the single ComponentManager. No copies.
```

---

## Constraints and Limits

| Constant | Value | Note |
|---|---|---|
| `MAX_ENTITIES` | 1000 | Size of `entity_to_idx` and `sparse` arrays |
| `MAX_COMPONENTS` | 32 | Width of `Signature` bitset |
| `MAX_SYSTEMS` | 16 | Width of `Schedule` bitset |
| `PRE_INIT_SIZE` | 100 | `vector::reserve` hint on construction |

**Never reorder `MyComponents` or `MySystems`.** Reordering changes IDs. Always
append new types at the end. The `static_assert` guards in `game_registry.hpp`
catch accidental reorders at compile time.

**`MAX_ENTITIES` is a hard compile-time cap.** Each `ComponentArray` and each
system's `sparse` array is a `std::array<u32, MAX_ENTITIES>` — 4KB per array.
With 32 components + 16 systems that's 192KB of inline sparse storage. Fine
for a small 2D game; raise `MAX_ENTITIES` in `common.hpp` to increase.

---

## Known Issues and Limitations

- `RigidBody2` carries an `a` (acceleration) field that is currently unused.
  `GravitySystem` writes directly into `v`, ignoring `a`. Either remove `a`
  or integrate it in the gravity update: `rb.v += (rb.a + g.force) * dt`.

- `PhysicsSystem` (the combined gravity+movement system) does a double
  integration in one step — it adds gravity to velocity and velocity to position
  in the same update. This gives slightly different numerical behavior from
  running `GravitySystem` then `MovementSystem` separately. Prefer the split
  systems for new code; `PhysicsSystem` is kept as a reference/fallback.

- `create_from_archetype` has a `// TODO: understand` comment in `world.hpp`
  that should be resolved or removed.

- Sparse/dense storage policy per component type (opt-in dense-only for components
  that every entity has) is in progress.
