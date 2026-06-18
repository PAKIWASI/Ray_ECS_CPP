# wasi-ecs

A small, header-only, compile-time Entity Component System for C++23, built for
a 2D game engine on top of raylib. No runtime polymorphism, no type erasure,
no virtual calls anywhere in the hot path.

## Design

- **Entities** are just `u32` indices.
- **Components** are plain structs. Each type gets a permanent compile-time ID
  from its position in a type list, resolved via `component_id<T>` — a
  `constexpr`, not a runtime lookup.
- **Component storage** is a sparse set: one dense `std::vector<T>` per
  component type, with O(1) add/remove via swap-and-pop, and an
  `entity → index` sparse array for O(1) lookup.
- **Systems** are CRTP types (`SystemBase<Derived, Components...>`), each with
  a `constexpr` signature computed from its component list. No `ISystem`
  interface, no vtable — the active systems live directly in a
  `std::tuple<Systems...>`, dispatched via fold expressions.
- **Archetypes** are named component bundles (`Archetype<Ts...>`) used for
  ergonomic batch entity creation — not a storage strategy. Components are
  still stored per-type, not per-archetype.

## Extending it

The engine ships a few built-in components (`Transform2`, `RigidBody2`,
`Gravity2`) and systems (`GravitySystem`, `MovementSystem`, `RenderSystem`).

`World` is templated on the two type lists, so there's no global
`Components`/`Systems` alias for the library to hardcode and no include-order
requirement for you to get wrong:

```cpp
template <typename ComponentList, typename SystemList>
class WorldImpl
{
    using ComponentManager = typename make_component_manager<ComponentList>::type;
    using SystemManager    = typename make_system_manager<SystemList>::type;
    // ...
};
```

Your game defines its own lists, mixing built-ins with your own types, and
instantiates `World` from them — that's the one place you touch:

```cpp
// game_registry.hpp — your project
using MyComponents = ComponentList<Transform2, RigidBody2, Gravity2, Sprite>;
using MySystems    = SystemList<GravitySystem, MovementSystem, RenderSystem, WindSystem>;
using World        = WorldImpl<MyComponents, MySystems>;
```

`component_id<T>` and `system_id<T>` become functions of which list you're
asking about (`component_id<T, MyComponents>`), so two different `World`
instantiations in the same program never collide.

## How it compares

| | wasi-ecs | EnTT | flecs / bevy_ecs |
|---|---|---|---|
| Storage | sparse set per component type | sparse set per component type | archetype tables (grouped by exact component set) |
| Component ID | compile-time, `constexpr` | runtime, type-erased | runtime, cached |
| System dispatch | `std::tuple` + fold expression, no vtable | none built in — you write `view<T...>()` loops | scheduler with dependency analysis |
| Add/remove component | O(1), no migration | O(1), no migration | moves entity's row between tables |
| Multi-component iteration | probe smallest array, lookup the rest | same strategy | contiguous — components pre-grouped |
| Runtime type registration | no — fixed list at compile time | yes | yes |

In short: this is architecturally close to EnTT's core (sparse-set storage,
same swap-and-pop removal), but trades EnTT's runtime flexibility for
compile-time component/system IDs and zero type erasure. It does not do
archetype-grouped storage like flecs or bevy, so multi-component iteration
isn't as cache-optimal — the tradeoff is no archetype migration cost when
entities gain or lose components, which is frequent in small 2D games.

## Status

Core (entities, components, systems, signatures, scheduling) is implemented
and working. Archetype-based batch creation and a sparse/dense storage policy
per component type are in progress.
