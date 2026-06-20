# wasi-ecs Reference

A complete reference for the compile-time ECS: what every file does, how they
connect, why the dependency graph is shaped the way it is, and exactly how to
extend it without breaking anything.

---

## Table of Contents

1. [What an ECS Is](#1-what-an-ecs-is)
2. [Core Design Principles](#2-core-design-principles)
3. [File Map](#3-file-map)
4. [Dependency Graph](#4-dependency-graph)
5. [Every File Explained](#5-every-file-explained)
6. [How the Pieces Connect at Runtime](#6-how-the-pieces-connect-at-runtime)
7. [The Compile-Time Pipeline](#7-the-compile-time-pipeline)
8. [Instantiation Order in `game_registry.hpp`](#8-instantiation-order-in-game_registryhpp)
9. [Data Flow: Adding a Component](#9-data-flow-adding-a-component)
10. [Data Flow: The Update Loop](#10-data-flow-the-update-loop)
11. [Data Flow: Destroying an Entity](#11-data-flow-destroying-an-entity)
12. [How to Add a Component](#12-how-to-add-a-component)
13. [How to Add a System](#13-how-to-add-a-system)
14. [Memory Layout](#14-memory-layout)
15. [Why the Dependency Graph Is Shaped This Way](#15-why-the-dependency-graph-is-shaped-this-way)
16. [Constraints and Limits](#16-constraints-and-limits)

---

## 1. What an ECS Is

An ECS separates *identity* (entities), *data* (components), and *logic* (systems).

```
Traditional OOP:             ECS:
┌──────────────┐             Entity = just a number (u32 id)
│ Player       │
│  pos: Vec2   │             Components = plain data structs
│  vel: Vec2   │               Transform2 { pos, rot, scale }
│  update() {  │               RigidBody2 { v, a }
│    pos += vel│
│  }           │             Systems = functions that iterate entities
└──────────────│               GravitySystem::update(dt) {
                                   for each entity with {RigidBody2, Gravity2}:
                                       rb.v += g.force * dt
                               }
```

**Why**: Components stored together by type (not by entity) means all `Transform2`
data is contiguous in memory. Systems iterate sequentially → cache friendly.
No inheritance, no vtable, no dynamic dispatch in the hot path.

---

## 2. Core Design Principles

**No virtual functions in the hot path.** The original ECS had `ISystem*` and
`ICompArr*` with vtable dispatch on every update and component access. The
current version has none. Dispatch happens via CRTP (compile-time) and fold
expressions over tuples.

**No global type lists.** `component_id<T>` and `system_id<T>` are not global
variables. They are defined in `game_registry.hpp` and scoped to specific lists.
Two different World instantiations in the same binary never collide.

**One file to edit.** `game_registry.hpp` is the only file you touch. Everything
else is library infrastructure that requires no modification when you add
components or systems.

**Compile-time IDs.** Component and system IDs are `constexpr u8` values derived
from position in a type list. They are resolved at compile time and embedded as
integer literals — not computed at runtime.

---

## 3. File Map

```
ecs/
├── common.hpp              Core types, constants, and concepts
├── component_registry.hpp  ComponentList<Ts...> and comp_type_index<T,List>
├── component_manager.hpp   ComponentArray<T> and ComponentManagerImpl<Ts...>
├── system_registry.hpp     SystemList<Ts...> and sys_type_index<T,List>
├── system_base.hpp         SystemBase<Derived,CMgr,CTs...> CRTP base
├── system_manager.hpp      SystemManagerImpl<Ss...> and make_system_manager
├── entity_manager.hpp      EntityManager — ID issuing and signatures
├── archetype.hpp           Archetype<CList,Ts...> — entity templates
├── world.hpp               World<CList,SList> — the public API
├── game_registry.hpp       YOUR FILE — lists, aliases, GameWorld, schedules
└── systems/
    ├── gravity_system.hpp
    ├── movement_system.hpp
    └── physics_system.hpp
```

**Rule**: library files (`common` through `world`) never include `game_registry.hpp`
or any concrete system header. The dependency only flows one way.

---

## 4. Dependency Graph

Arrows mean "includes / depends on". This graph is acyclic — every node has a
path to `wasi.hpp` without looping back.

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
         GameWorld, schedules, archetypes]
                │
                ├── systems/gravity_system.hpp
                ├── systems/movement_system.hpp
                └── systems/physics_system.hpp
```

**Critical edges that were previously broken and are now fixed:**

- `system_registry.hpp` used to include `physics_system.hpp` → cycle.
  Now it includes only `common.hpp`.
- `system_base.hpp` used to reference a global `ComponentManager` alias and
  `component_id<T>` → both didn't exist after World was templated.
  Now it templates on `CMgr` and uses `comp_type_index<T, CMgr::ListType>` directly.
- `SystemType_t` concept used to live in `system_base.hpp` but was needed by
  `system_registry.hpp` → pulling `system_base` into `system_registry` created
  a cycle. Now `SystemType_t` lives in `common.hpp`.

---

## 5. Every File Explained

### `common.hpp`

The foundation. Nothing depends on this except `wasi.hpp`.

```cpp
using Entity        = u32;               // just an index
using Signature     = std::bitset<32>;   // which components an entity has
using ComponentType = u8;                // component ID type
using SystemType    = u8;                // system ID type
using Schedule      = std::bitset<16>;   // which systems to run this frame
constexpr u32 INVALID = u32::max;        // sentinel for "not present"
```

**`ComponentType_t` concept** — a valid component must be:
- `default_initializable` (so `ComponentArray` can pre-allocate)
- `movable` (so data can be moved into the array without copying)

**`SystemType_t` concept** — a valid system must expose:
- `update_impl(float dt)` — the actual update logic
- `static get_signature() -> Signature` — which components it requires
- `add_entity / remove_entity / has_entity` — entity set management

`SystemType_t` lives here (not in `system_base.hpp`) specifically so
`system_registry.hpp` can constrain `SystemList<Ts...>` without transitively
including the entire `system_base → component_manager` chain, which would
create a circular dependency.

---

### `component_registry.hpp`

Defines `ComponentList` and the compile-time ID lookup mechanism.

```cpp
template <ComponentType_t... Ts>
struct ComponentList {
    static constexpr u8 count = sizeof...(Ts);
};
```

`ComponentList` is a wrapper around a parameter pack. Its only purpose is to
carry a set of types so that `comp_type_index` can unpack them.

```cpp
template <typename T, typename... Ts>
struct comp_type_index<T, ComponentList<Ts...>> {
    static constexpr u8 value = []() consteval -> u8 {
        constexpr bool matches[] { std::is_same_v<T, Ts>... };
        for (u8 i = 0; i < sizeof...(Ts); ++i)
            if (matches[i]) return i;
        return u8::max;  // caught by static_asserts in game_registry.hpp
    }();
};
```

The consteval lambda runs at compile time. `std::is_same_v<T, Ts>...` expands
to a `bool` array — one entry per type in the list. The loop finds the `true`
entry and returns its index. That index is the permanent component ID.

**This file intentionally has no game component includes.** It is pure
infrastructure. `2d_comps.hpp` is included in `game_registry.hpp`, not here.

---

### `component_manager.hpp`

Two classes: `ComponentArray<T>` (storage for one component type) and
`ComponentManagerImpl<Ts...>` (holds all arrays in a tuple).

#### `ComponentArray<T>` — dense/sparse storage

```
entity_to_idx[e] = i     ← sparse: which slot does entity e use? (INVALID = none)
data[i]          = T{}   ← dense: component value at slot i
idx_to_entity[i] = e     ← reverse map: which entity owns slot i
```

**Add** (`add_data`):
```
entity_to_idx[e] = data.size()
data.push_back(comp)
idx_to_entity.push_back(e)
```

**Remove** (`remove_data`) — swap-and-pop, O(1):
```
idx = entity_to_idx[e]
last = data.back()
data[idx] = move(last)          ← move last into vacated slot
idx_to_entity[idx] = last_entity
entity_to_idx[last_entity] = idx
entity_to_idx[e] = INVALID
data.pop_back()
idx_to_entity.pop_back()
```

**Direct sequential access** — systems can drive their loop from the smallest
component array instead of going through the entity set:
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

`entity_destroyed(e)` uses a fold expression:
```cpp
std::apply([&](auto&... arr) {
    (arr.remove_if_present(e), ...);
}, arrays);
```
No loop, no vtable, no checking which components the entity has — each array's
`remove_if_present` does a single `has_data(e)` check internally.

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

**This file has zero system header includes.** Previously it included
`physics_system.hpp` to get the full type for `SystemList<PhysicsSystem>`.
That created a cycle. The fix: system headers are included in
`game_registry.hpp` after the lists are defined. `SystemList` only needs
type names to exist — which they do because `SystemType_t` is checked at
instantiation time, not at the point of the `using MySystems = ...` declaration.

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
- `CMgr&` is the reference to store
- `CMgr::ListType` is the component list, accessible without a separate parameter

**Signature computation:**
```cpp
static constexpr Signature signature =
    make_signature<typename CMgr::ListType, ComponentTypes...>();
```

`make_signature` is a free `consteval` function:
```cpp
template <typename CList, typename... Ts>
consteval auto make_signature() -> Signature {
    Signature sig{};
    ((sig.set(comp_type_index<Ts, CList>::value)), ...);
    return sig;
}
```

This runs once at compile time. `signature` is a `constexpr` bitset baked into
the binary as a literal.

**Entity set — sparse set:**
```
dense[i]   = entity at position i    ← contiguous, iteration is sequential
sparse[e]  = index of e in dense     ← O(1) lookup
```

`has_entity(e)` uses a triple check to guard against stale sparse entries:
```cpp
return sparse[e] != INVALID
    && sparse[e] < dense.size()
    && dense[sparse[e]] == e;
```

The third check (`dense[sparse[e]] == e`) catches cases where `sparse[e]`
holds a valid index but another entity now lives there after swap-and-pop.

**CRTP dispatch:**
```cpp
void update(float dt) {
    static_cast<Derived*>(this)->update_impl(dt);
}
```

`SystemManagerImpl` calls `sys.update(dt)` which calls `Derived::update_impl(dt)`
directly. The compiler resolves this at compile time — no vtable, fully inlinable.

---

### `system_manager.hpp`

```cpp
template <SystemType_t... Systems>
class SystemManagerImpl {
    std::tuple<Systems...> systems;
    using MySysList = SystemList<Systems...>;
};
```

Systems live inline in the tuple — no heap, no pointers. `std::get<T>(systems)`
retrieves a system at compile time.

**Constructor:**
```cpp
SystemManagerImpl(CMgr& cm) : systems(Systems(cm)...) {}
```

The pack expansion `Systems(cm)...` constructs each system with the same
component manager reference. The tuple's aggregate initialization handles
placement.

**`on_signature_change`** — called when an entity gains or loses a component:
```cpp
std::apply([&](auto&... sys) {
    (check_and_update_entity(sys, e, new_sig), ...);
}, systems);
```

`check_and_update_entity` checks if `new_sig` contains all bits in the system's
required signature. Adds the entity if it now qualifies and didn't before.
Removes it if it no longer qualifies and was enrolled.

**`update`** — the fold that replaces the vtable loop:
```cpp
std::apply([&](auto&... sys) {
    ((schedule.test(
        sys_type_index<std::remove_cvref_t<decltype(sys)>, MySysList>::value
    ) ? sys.update(dt) : void()), ...);
}, systems);
```

`remove_cvref_t<decltype(sys)>` extracts the concrete system type from the
`auto&` pack parameter. `sys_type_index<T, MySysList>::value` is the system's
bit position in the `Schedule` bitset — a compile-time constant. The whole
conditional is evaluated per-system at compile time for the bit test, and at
runtime only for the actual `update(dt)` call.

---

### `entity_manager.hpp`

Issues and recycles entity IDs. Maintains per-entity component signatures.

```
next_id          — next fresh ID to issue
free_ids         — recycled IDs waiting for reuse
signatures[e]    — which components entity e currently has
```

`set_component(e, c)` sets bit `c` in `signatures[e]`. Called by
`World::add_component` after the component data is stored.

`get_signature(e)` returns the current `Signature` bitset. Used by
`SystemManager::on_signature_change` to check system qualification.

---

### `archetype.hpp`

Named bundles of component types. Not a storage strategy — components are still
stored per-type in `ComponentArray<T>`, not grouped by archetype.

```cpp
template <typename CList, typename... Ts>
struct Archetype {
    static consteval auto signature() -> Signature;    // bitset of required components
    static constexpr auto size() -> u8;                // number of components
    template <typename F>
    static constexpr void for_each_type(F&& f);       // iterate types at compile time
    template <typename T>
    static constexpr auto contains() -> bool;          // type membership check
};
```

`CList` must be passed explicitly because there is no global `ComponentList`.
In practice you use the alias `GameWorld::Archetype<Ts...>` which binds `CList`
automatically.

`for_each_type` is used by `World::create_from_archetype`:
```cpp
A::for_each_type([&]<typename T>() {
    component_manager.get_arr<T>().add_data(e, T{});
    entity_manager.set_component(e, component_id<T>);
});
```

The lambda with `<typename T>` is a C++20 explicit template parameter in a
generic lambda. The fold `(f.template operator()<Ts>(), ...)` calls it once
per type in the archetype's pack.

---

### `world.hpp`

The public API. Templated on two lists:

```cpp
template <typename... CList, typename... SList>
class World<ComponentList<CList...>, SystemList<SList...>> {
    using ComponentManager = make_component_manager<MyComponentList>::type;
    using SystemManager    = make_system_manager<MySystemList>::type;

    EntityManager    entity_manager;
    ComponentManager component_manager{};
    SystemManager    system_manager{component_manager};
};
```

Declaration order matters: `component_manager` must be fully constructed before
`system_manager` because systems hold a `CMgr&` reference to it.

`World` exposes scoped `component_id<T>` and `system_id<T>` as static template
variables:
```cpp
template <typename T>
static constexpr ComponentType component_id =
    comp_type_index<T, MyComponentList>::value;
```

`make_schedule<Ts...>()` is a static `consteval` member:
```cpp
template <typename... Ts>
static consteval auto make_schedule() -> Schedule {
    Schedule s{};
    (s.set(sys_type_index<Ts, MySystemList>::value), ...);
    return s;
}
```

`Archetype<Ts...>` is a member alias that binds `CList`:
```cpp
template <typename... Ts>
using Archetype = ::Archetype<MyComponentList, Ts...>;
```

---

### `game_registry.hpp`

**The only file you edit.** Everything else is library code.

Its contents in order (order is mandatory — see §8):

1. `#include "world.hpp"` — brings in all library infrastructure
2. Game component headers (`2d_comps.hpp`)
3. `MyComponents = ComponentList<...>` — the canonical component list
4. `component_id<T>` alias + static_asserts
5. `ComponentManager` alias (needed by system headers)
6. System headers (`gravity_system.hpp`, etc.)
7. `MySystems = SystemList<...>` — the canonical system list
8. `system_id<T>` alias + static_asserts
9. `GameWorld = World<MyComponents, MySystems>`
10. Named `constexpr Schedule` constants
11. Named archetype aliases

---

## 6. How the Pieces Connect at Runtime

```
┌─────────────────────────────────────────────────────┐
│                     GameWorld                        │
│                                                      │
│  EntityManager          ComponentManager             │
│  ┌────────────┐         ┌─────────────────────────┐ │
│  │ next_id    │         │ tuple<                  │ │
│  │ free_ids   │         │   ComponentArray<T2>,   │ │
│  │ signatures │         │   ComponentArray<RB2>,  │ │
│  │  [e] = bits│         │   ComponentArray<G2>    │ │
│  └────────────┘         │ >                       │ │
│                         └─────────────────────────┘ │
│  SystemManager                                       │
│  ┌────────────────────────────────────────────────┐ │
│  │ tuple<                                         │ │
│  │   GravitySystem<CMgr>   ← holds CMgr& ref     │ │
│  │   MovementSystem<CMgr>  ← holds CMgr& ref     │ │
│  │   PhysicsSystem<CMgr>   ← holds CMgr& ref     │ │
│  │ >                                              │ │
│  └────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

Systems hold a reference to `ComponentManager`. When a system calls
`get_component<Transform2>(e)`, it goes:

```
comp_manager.get_arr<Transform2>()    ← std::get<ComponentArray<Transform2>>(tuple)
    .get_data(e)                      ← data[entity_to_idx[e]]
```

Two memory accesses. No vtable. No type erasure.

---

## 7. The Compile-Time Pipeline

```
game_registry.hpp defines:
    MyComponents = ComponentList<Transform2, RigidBody2, Gravity2>
                                      │           │          │
                                    ID=0        ID=1       ID=2

comp_type_index<Transform2, MyComponents>::value = 0  (constexpr, compile time)
comp_type_index<RigidBody2, MyComponents>::value = 1
comp_type_index<Gravity2,   MyComponents>::value = 2

GravitySystem<CMgr> inherits SystemBase<GravitySystem<CMgr>, CMgr, RigidBody2, Gravity2>

SystemBase computes at compile time:
    signature = make_signature<CMgr::ListType, RigidBody2, Gravity2>()
              = Signature{} with bits 1 and 2 set
              = 0b000...00110
              = a constexpr bitset literal baked into the binary

GameWorld::make_schedule<GravitySystem<CMgr>, MovementSystem<CMgr>>()
    = Schedule{} with bits 0 and 1 set
    = 0b000...00011
    = a constexpr bitset literal baked into the binary

PHYSICS_ONLY constexpr Schedule = 0b000...00100
```

None of this runs at startup. The compiler computes it and embeds it as literals.

---

## 8. Instantiation Order in `game_registry.hpp`

The order inside `game_registry.hpp` is not arbitrary. Each step depends on
the previous one being complete.

```
Step  Defines                    Needed by
────  ──────────────────────     ──────────────────────────────────────────
1     world.hpp included         brings in all library templates (unevaluated)
2     2d_comps.hpp               component structs exist as types
3     MyComponents               comp_type_index can now be instantiated
4     component_id<T>            convenience alias, static_asserts
5     ComponentManager alias     system headers need the concrete CMgr type
6     system headers             GravitySystem<CMgr> etc. now fully defined
7     MySystems                  sys_type_index can now be instantiated
8     system_id<T>               convenience alias, static_asserts
9     GameWorld                  both lists exist, World<CL,SL> instantiates
10    constexpr Schedules        GameWorld::make_schedule uses MySystems
11    Archetype aliases          GameWorld::Archetype<Ts...> binds MyComponents
```

If you break this order — e.g. include a system header before step 5 — the
system's `SystemBase<D, CMgr, ...>` won't find `ComponentManager` and the
`comp_type_index` call inside `make_signature` won't have a valid list to
query.

---

## 9. Data Flow: Adding a Component

```cpp
world.add_component<Gravity2>(e, Gravity2{{0, -9.8f}});
```

```
World::add_component<Gravity2>(e, comp)
    │
    ├─ component_manager.get_arr<Gravity2>()
    │      └─ std::get<ComponentArray<Gravity2>>(arrays)   ← compile time
    │  .add_data(e, move(comp))
    │      └─ entity_to_idx[e] = data.size()
    │         data.push_back(comp)
    │         idx_to_entity.push_back(e)
    │
    ├─ entity_manager.set_component(e, component_id<Gravity2>)
    │      └─ signatures[e].set(2)   ← bit 2 is now set
    │
    └─ system_manager.on_signature_change(e, signatures[e])
           │
           └─ for each system in tuple (fold expression):
                  check_and_update_entity(sys, e, new_sig)
                      qualifies = (new_sig & sys.signature) == sys.signature
                      ├─ if qualifies && !has: sys.add_entity(e)
                      │      sparse[e] = dense.size()
                      │      dense.push_back(e)
                      └─ if !qualifies && has: sys.remove_entity(e)
```

---

## 10. Data Flow: The Update Loop

```cpp
world.update(dt, GRAVITY_AND_MOVEMENT);
// GRAVITY_AND_MOVEMENT = Schedule{bits 0 and 1 set} — a constexpr literal
```

```
World::update(dt, schedule)
    │
    └─ system_manager.update(dt, schedule)
           │
           └─ std::apply([&](auto&... sys) {
                  ((schedule.test(sys_type_index<T, MySysList>::value)
                      ? sys.update(dt)
                      : void()), ...);
              }, systems);
           │
           expands to:
           ├─ schedule.test(0) → true  → GravitySystem::update(dt)
           │      → static_cast<GravitySystem*>(this)->update_impl(dt)
           │
           ├─ schedule.test(1) → true  → MovementSystem::update(dt)
           │      → static_cast<MovementSystem*>(this)->update_impl(dt)
           │
           └─ schedule.test(2) → false → void()   ← PhysicsSystem skipped
```

Inside `GravitySystem::update_impl`:
```
comp_manager.get_arr<Gravity2>()          ← std::get in tuple, compile time
    for i in 0..entity_count():
        e  = entity_at(i)                 ← idx_to_entity[i], sequential
        g  = data_at(i)                   ← data[i], sequential, L1 cache
        rb = get_arr<RigidBody2>().get_data(e)  ← one random lookup
        rb.v += g.force * dt
```

---

## 11. Data Flow: Destroying an Entity

```cpp
world.destroy_entity(e);
```

Order is mandatory — systems and components must clean up before the ID is recycled.

```
World::destroy_entity(e)
    │
    ├─ system_manager.on_entity_destroyed(e)
    │      └─ std::apply([&](auto&... sys) {
    │             ((sys.has_entity(e) ? sys.remove_entity(e) : void()), ...);
    │         }, systems);
    │         ← O(1) per system: sparse[e] check, then swap-and-pop if present
    │
    ├─ component_manager.entity_destroyed(e)
    │      └─ std::apply([&](auto&... arr) {
    │             (arr.remove_if_present(e), ...);
    │         }, arrays);
    │         ← one has_data(e) check per array, remove_data if present
    │
    └─ entity_manager.destroy(e)
           └─ signatures[e].reset()
              free_ids.push_back(e)   ← ID available for reuse
```

---

## 12. How to Add a Component

**Step 1** — define the struct in a component header:
```cpp
// ecs/components/my_comps.hpp
struct Health {
    float current;
    float max;
};
```

**Step 2** — include it in `game_registry.hpp` and add to `MyComponents`:
```cpp
#include "components/my_comps.hpp"

using MyComponents = ComponentList<
    Transform2,   // ID 0  ← never reorder
    RigidBody2,   // ID 1
    Gravity2,     // ID 2
    Health        // ID 3  ← add at the end
>;

// Add a static_assert:
static_assert(component_id<Health> == 3, "Health ID changed");
```

That's it. The component is now:
- Stored in `ComponentManagerImpl`'s tuple (a new `ComponentArray<Health>`)
- Accessible via `world.add_component<Health>(e, Health{100, 100})`
- Queryable by systems via `get_component<Health>(e)`

---

## 13. How to Add a System

**Step 1** — create the system header:
```cpp
// ecs/systems/health_system.hpp
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
            auto& h  = healths.data_at(i);
            // ... logic
        }
    }
};
```

**Step 2** — add to `game_registry.hpp`:
```cpp
// After ComponentManager alias (step 5 in the ordering):
#include "systems/health_system.hpp"

// In MySystems (step 7):
using MySystems = SystemList<
    GravitySystem<ComponentManager>,    // bit 0  ← never reorder
    MovementSystem<ComponentManager>,   // bit 1
    PhysicsSystem<ComponentManager>,    // bit 2
    HealthSystem<ComponentManager>      // bit 3  ← add at the end
>;

// Add a static_assert:
static_assert(system_id<HealthSystem<ComponentManager>> == 3, "HealthSystem ID changed");

// Optional: named schedule
constexpr Schedule HEALTH_UPDATE = GameWorld::make_schedule<HealthSystem<ComponentManager>>();
```

**Step 3** — use in the game loop:
```cpp
world.update(dt, HEALTH_UPDATE);
// or combine with others:
constexpr Schedule FULL = GameWorld::make_schedule<
    GravitySystem<ComponentManager>,
    MovementSystem<ComponentManager>,
    HealthSystem<ComponentManager>
>();
world.update(dt, FULL);
```

---

## 14. Memory Layout

### `ComponentManagerImpl` — the tuple

```
Stack/inline (no heap for the tuple itself):
┌────────────────────────┬────────────────────────┬────────────────────────┐
│ ComponentArray<T2>     │ ComponentArray<RB2>    │ ComponentArray<G2>     │
│ ┌──────────────────┐   │ ┌──────────────────┐   │ ┌──────────────────┐   │
│ │ vector<T2> data  │   │ │ vector<RB2> data │   │ │ vector<G2> data  │   │
│ │ [T2,T2,T2,T2...] │   │ │ [RB2,RB2,...]    │   │ │ [G2,G2,...]      │   │
│ │                  │   │ │                  │   │ │                  │   │
│ │ vector<Entity>   │   │ │ vector<Entity>   │   │ │ vector<Entity>   │   │
│ │ idx_to_entity    │   │ │ idx_to_entity    │   │ │ idx_to_entity    │   │
│ │                  │   │ │                  │   │ │                  │   │
│ │ array<u32,1000>  │   │ │ array<u32,1000>  │   │ │ array<u32,1000>  │   │
│ │ entity_to_idx    │   │ │ entity_to_idx    │   │ │ entity_to_idx    │   │
│ └──────────────────┘   │ └──────────────────┘   │ └──────────────────┘   │
└────────────────────────┴────────────────────────┴────────────────────────┘
 4KB sparse array per component type. Dense data heap-allocated by vector.
```

### `SystemManagerImpl` — the tuple

```
Stack/inline:
┌──────────────────────────┬──────────────────────────┬─────────────────────────┐
│ GravitySystem<CMgr>      │ MovementSystem<CMgr>      │ PhysicsSystem<CMgr>     │
│ ┌──────────────────────┐ │ ┌──────────────────────┐ │ ┌─────────────────────┐ │
│ │ vector<Entity> dense │ │ │ vector<Entity> dense │ │ │ vector<Entity> dense│ │
│ │ array<u32,1000>sparse│ │ │ array<u32,1000>sparse│ │ │ array<u32,1000>...  │ │
│ │ CMgr& comp_manager   │ │ │ CMgr& comp_manager   │ │ │ CMgr& comp_manager  │ │
│ │ [ref to same CMgr]   │ │ │ [ref to same CMgr]   │ │ │ [ref to same CMgr]  │ │
│ └──────────────────────┘ │ └──────────────────────┘ │ └─────────────────────┘ │
└──────────────────────────┴──────────────────────────┴─────────────────────────┘
 All systems hold a reference to the single ComponentManager. No copies.
```

---

## 15. Why the Dependency Graph Is Shaped This Way

### Why `SystemType_t` is in `common.hpp` not `system_base.hpp`

`system_registry.hpp` needs to constrain `SystemList<Ts...>` with `SystemType_t`.
If `SystemType_t` were in `system_base.hpp`, then `system_registry.hpp` would
need to include `system_base.hpp`, which includes `component_manager.hpp`.
But `system_manager.hpp` includes both `system_base.hpp` and `system_registry.hpp` —
you'd get a cycle. `common.hpp` has no upstream dependencies so it's the safe
place for concepts that need to be shared across the graph.

### Why systems are templated on `CMgr` not `ComponentList`

Systems need to store `CMgr&` to call `get_arr<T>()`. If they only had
`ComponentList`, they'd need a separate reference to the manager. Templating on
`CMgr` gives both: the reference type to store, and `CMgr::ListType` for the
compile-time ID lookup. One template parameter instead of two.

### Why `component_id<T>` and `system_id<T>` are not in library headers

They reference a specific `ComponentList` / `SystemList`. There is no global list
— the lists live in `game_registry.hpp`. Library headers use
`comp_type_index<T, List>::value` directly, passing the list explicitly via
template parameters. The convenience aliases exist only in `game_registry.hpp`
for use in user code and `main.cpp`.

### Why `system_registry.hpp` has zero system includes

Previously it included `physics_system.hpp` to get the full type for
`SystemList<PhysicsSystem>`. That created:

```
system_registry → physics_system → system_base → component_manager → component_registry
```

And `system_manager` includes both `system_base` and `system_registry`, closing
a cycle. The fix: `SystemList` accepts forward-declared or incomplete types at
the point of the `using MySystems = ...` declaration. The `SystemType_t`
constraint is checked when the type is actually used, at which point the full
definition is available. System headers are included in `game_registry.hpp`
before `MySystems` is defined.

---

## 16. Constraints and Limits

| Constant | Value | Where |
|---|---|---|
| `MAX_ENTITIES` | 1000 | `common.hpp` |
| `MAX_COMPONENTS` | 32 | `common.hpp` |
| `MAX_SYSTEMS` | 16 | `common.hpp` |
| `PRE_INIT_SIZE` | 100 | `common.hpp` — vector reserve hint |

**`MAX_ENTITIES`** — size of `entity_to_idx` array in each `ComponentArray`
and `sparse` array in each `SystemBase`. Each costs `MAX_ENTITIES * 4` bytes
= 4KB. With 32 components + 16 systems that's 192KB of sparse arrays total,
all inline.

**`MAX_COMPONENTS`** — width of `Signature` bitset. Must be `≥` number of
entries in `MyComponents`. Increase if you add more than 32 component types.

**`MAX_SYSTEMS`** — width of `Schedule` bitset. Must be `≥` number of entries
in `MySystems`. Increase if you add more than 16 system types.

**Never reorder `MyComponents` or `MySystems`.** Reordering changes IDs.
Existing serialized data, saved states, or anything that stores component/system
IDs numerically will silently read the wrong data. Always append new types at
the end.
