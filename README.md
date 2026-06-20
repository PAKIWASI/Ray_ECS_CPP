# wasi-ecs

A small, header-only, compile-time Entity Component System for C++23, built for
a 2D game on top of raylib. No runtime polymorphism, no type erasure, no
virtual calls in the hot path.

## Design

- **Entities** are `u32` indices. An entity must be created with at least one
  component — there's no concept of an empty entity.
- **Components** are plain structs, each with a permanent compile-time ID
  derived from its position in a type list (`comp_type_index<T, List>`).
- **Storage** is a sparse set: one dense `std::vector<T>` per component type,
  O(1) add/remove via swap-and-pop, with an `entity → index` sparse array for
  O(1) lookup.
- **Systems** are CRTP types (`SystemBase<Derived, CMgr, Components...>`) with
  a `constexpr` signature computed from their component list. Active systems
  live in a `std::tuple<Systems...>`, dispatched via fold expressions — no
  `ISystem` interface, no vtable.
- **Archetypes** (`Archetype<CList, Ts...>`) are named component bundles for
  ergonomic batch entity creation. Not a storage strategy — components are
  still stored per-type, not per-archetype.

## Why not EnTT / flecs?

| | wasi-ecs | EnTT | flecs / bevy_ecs |
|---|---|---|---|
| Storage | sparse set per component type | sparse set per component type | archetype tables |
| Component/system IDs | compile-time `constexpr` | runtime, type-erased | runtime, cached |
| System dispatch | tuple + fold expression | none built in | scheduler |
| Multi-component iteration | probe smallest array, lookup rest | same | contiguous (pre-grouped) |
| Runtime type registration | no — fixed list at compile time | yes | yes |

Architecturally close to EnTT's sparse-set core, but trades runtime
flexibility for compile-time IDs and zero type erasure. No archetype-grouped
storage like flecs/bevy, so multi-component iteration isn't as cache-optimal —
the tradeoff is no migration cost when entities gain/lose components, which is
frequent in small 2D games.

## File map

```
include/ecs/
├── common.hpp              Core types, constants, concepts
├── component_registry.hpp  ComponentList<Ts...>, comp_type_index<T, List>
├── component_manager.hpp   ComponentArray<T>, ComponentManagerImpl<Ts...>
├── system_registry.hpp     SystemList<Ts...>, sys_type_index<T, List>
├── system_base.hpp         SystemBase<Derived, CMgr, Cs...> CRTP base, SystemType_t
├── system_manager.hpp      SystemManagerImpl<Ss...>, make_system_manager
├── entity_manager.hpp      EntityManager — ID issuing/recycling, signatures
├── archetype.hpp           Archetype<CList, Ts...> — entity templates
├── world.hpp               World<CList, SList> — the public API
├── game_registry.hpp       YOUR FILE — lists, aliases, GameWorld, schedules
├── components/
│   └── 2d_comps.hpp        Vec2, Transform2, RigidBody2
└── systems/
    ├── movement_system.hpp     implemented
    ├── input_system.hpp        stub, not yet implemented
    ├── render_system.hpp       stub, not yet implemented
    └── collision_system.hpp    stub, not yet implemented
```

**Rule:** library files (`common` → `world`) never include `game_registry.hpp`
or a concrete system header. Dependency flows one way only:
`common → {component,system}_registry → component_manager / system_base →
system_manager → {entity_manager, archetype} → world → game_registry`.

## Quick start

```cpp
#include "ecs/game_registry.hpp"

GameWorld world;

// Every entity needs an initial signature — no empty entities.
Signature sig;
sig.set(GameWorld::component_id<Transform2>);
sig.set(GameWorld::component_id<RigidBody2>);

Entity e = world.create_entity(sig);
world.add_component<Transform2>(e, Transform2{});
world.add_component<RigidBody2>(e, RigidBody2{.v = {1, 0}});

world.update(dt, ALL_SYSTEMS);
world.destroy_entity(e);
```

Or via an archetype, which computes the signature for you:

```cpp
Entity e = world.create_from_archetype<PlayerArchetype>();
// or with initial values, in archetype component order:
Entity e2 = world.create_from_archetype<PlayerArchetype>(
    Transform2{.pos = {0, 0}}, RigidBody2{.v = {1, 0}});
```

## Extending

**`game_registry.hpp` is the only file you edit.**

1. Define the component struct, append it to `MyComponents` (never reorder —
   position is the permanent ID).
2. Write the system as a `SystemBase` CRTP subclass, include its header,
   append it to `MySystems` (position is the schedule bit).
3. The `static_assert`s at the bottom of each list guard against accidental
   reorders.

## Constraints

| Constant | Value | Note |
|---|---|---|
| `MAX_ENTITIES` | 4096 | size of sparse arrays |
| `MAX_COMPONENTS` | 32 | width of `Signature` bitset |
| `MAX_SYSTEMS` | 16 | width of `Schedule` bitset |

Raise `MAX_ENTITIES` in `common.hpp` if you need more; each component array
and system holds one `std::array<u32, MAX_ENTITIES>` sparse table (4KB each).

## Known limitations

- `RigidBody2::a` (acceleration) is currently unused — no system integrates it.
- `input_system.hpp`, `render_system.hpp`, `collision_system.hpp` are empty
  stubs, not wired into `game_registry.hpp` yet.
- `EntityManager::create()` doesn't itself assert that the signature is
  non-empty — passing an empty signature is caught later, at `destroy()`,
  where it's indistinguishable from a double-destroy.
- Sparse/dense storage policy per component type (opt-in dense-only storage
  for components every entity has) is not yet implemented.
