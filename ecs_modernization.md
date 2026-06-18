# ECS Modernization Notes

A detailed reference for each planned change to the C++23 ECS codebase, with rationale,
code, and caveats. Changes are ordered by dependency — earlier sections are prerequisites
for later ones.

---

## 1. Compile-Time Component IDs via Type List

### The Problem

The current approach uses a static local variable to assign IDs at first call:

```cpp
template <ComponentType_t T>
static auto get_component_id() -> ComponentType {
    static ComponentType id = next_comp_id++;  // assigned on first call
    return id;
}
```

This is fragile. The ID depends on call order across translation units. Two different builds
or two different orderings of `register_component<T>()` calls can assign different IDs to
the same type, silently corrupting signatures.

### The Fix: Explicit Type List

Define a canonical ordered list of all component types once. The index into this list *is*
the component ID — stable, deterministic, zero runtime cost.

```cpp
// components_list.hpp
// This is the single source of truth for component IDs.
// The position of each type in this list is its permanent ID.

#include "components.hpp"

template <typename... Ts>
struct ComponentList {
    static constexpr u8 count = sizeof...(Ts);
};

// ADD NEW COMPONENTS HERE — order determines ID, never reorder
using Components = ComponentList<
    Transform2,     // ID 0
    RigidBody2,     // ID 1
    Gravity2,       // ID 2
    Sprite,         // ID 3
    Lifetime        // ID 4
    // ...
>;

// MAX_COMPONENTS can now be derived, not hardcoded
// constexpr u8 MAX_COMPONENTS = Components::count;
```

### `type_index`: Compile-Time ID Lookup

```cpp
// Query: "what is the ID of type T in list List?"
// Result is a constexpr u8, computed entirely at compile time.

template <typename T, typename List>
struct type_index;

template <typename T, typename... Ts>
struct type_index<T, ComponentList<Ts...>> {
    static constexpr u8 value = []() constexpr -> u8 {
        constexpr bool matches[] = { std::is_same_v<T, Ts>... };
        for (u8 i = 0; i < sizeof...(Ts); ++i)
            if (matches[i]) return i;
        // T is not in the list — hard error at compile time
        // static_assert inside a lambda is C++23
        static_assert(false, "Component type not registered in ComponentList");
    }();
};

// Convenience alias
template <typename T>
constexpr ComponentType component_id = type_index<T, Components>::value;

// Usage — these are compile-time constants, usable in constexpr contexts
static_assert(component_id<Transform2> == 0);
static_assert(component_id<RigidBody2> == 1);
```

If you try `component_id<SomeUnregisteredType>`, you get a compile error — the bug is
caught before the program runs.

### Eliminating `register_component<T>()`

With compile-time IDs, `ComponentManager` can initialize all arrays in its constructor
automatically by folding over the type list. No manual registration calls needed.

```cpp
// Helper to construct ComponentManager from a ComponentList
template <typename... Ts>
class ComponentManagerImpl {
    std::tuple<ComponentArray<Ts>...> arrays;  // covered in section 2

  public:
    ComponentManagerImpl() = default;  // tuple default-constructs each array

    template <typename T>
    auto get_arr() -> ComponentArray<T>& {
        return std::get<ComponentArray<T>>(arrays);
    }

    template <typename T>
    auto get_arr() const -> const ComponentArray<T>& {
        return std::get<ComponentArray<T>>(arrays);
    }

    void entity_destroyed(Entity e) {
        // fold expression: calls remove_if_present on each array
        std::apply([&](auto&... arr) {
            (arr.remove_if_present(e), ...);
        }, arrays);
    }
};

// Alias using our canonical list
// Unpack ComponentList<Ts...> into ComponentManagerImpl<Ts...>
template <typename List>
struct make_component_manager;

template <typename... Ts>
struct make_component_manager<ComponentList<Ts...>> {
    using type = ComponentManagerImpl<Ts...>;
};

using ComponentManager = make_component_manager<Components>::type;
```

`main` now needs zero `register_component` calls. The world constructor handles everything.

---

## 2. `std::tuple` of Arrays — Eliminate `ICompArr` and Pointer Indirection

### The Problem

Currently component arrays are stored as:

```cpp
std::array<u_ptr<ICompArr>, MAX_COMPONENTS> comp_arrays;
```

Every component access goes through:
1. `comp_arrays[id]` — index into array of pointers
2. Dereference the `unique_ptr` — potential cache miss
3. `static_cast<ComponentArray<T>&>(...)` — downcast
4. Virtual dispatch for `entity_destroyed` — vtable lookup

That's two indirections and a virtual call for what should be a direct array access.

### The Fix: `std::tuple<ComponentArray<Ts>...>`

Because we know all component types at compile time (from the type list), we can store
the arrays directly in a tuple — no heap allocation, no pointers, no virtual dispatch.

```cpp
template <typename... Ts>
class ComponentManagerImpl {
    // Each ComponentArray<T> lives directly in this tuple — inline storage,
    // no indirection, no heap allocation.
    std::tuple<ComponentArray<Ts>...> arrays;

  public:
    // get_arr<T>() is now a direct std::get — resolved at compile time
    template <typename T>
    auto get_arr() -> ComponentArray<T>& {
        return std::get<ComponentArray<T>>(arrays);
    }

    // entity_destroyed no longer needs virtual dispatch.
    // The fold expression expands to one call per array, all inlined.
    void entity_destroyed(Entity e) {
        std::apply([&](auto&... arr) {
            (arr.remove_if_present(e), ...);
        }, arrays);
    }
};
```

`ICompArr` and the entire inheritance hierarchy can be deleted. `ComponentArray<T>` becomes
a plain struct — no virtual destructor, no vtable, no base class.

```cpp
// Before
template <ComponentType_t T>
class ComponentArray : public ICompArr {
    void entity_destroyed(Entity e) override { remove_data(e); }
};

// After — no inheritance, no virtual anything
template <ComponentType_t T>
class ComponentArray {
    // add a safe version for the fold expression in entity_destroyed
    void remove_if_present(Entity e) {
        if (has_data(e)) remove_data(e);
    }
};
```

### Memory Layout Comparison

```
Before:
comp_arrays[0] -> [heap] ComponentArray<Transform2>  <- cache miss
comp_arrays[1] -> [heap] ComponentArray<RigidBody2>  <- cache miss
comp_arrays[2] -> [heap] ComponentArray<Gravity2>    <- cache miss

After:
tuple: [ ComponentArray<Transform2> | ComponentArray<RigidBody2> | ComponentArray<Gravity2> ]
       ^--- all inline, adjacent in memory, no pointer chasing
```

---

## 3. Archetypes — Compile-Time Entity Templates

### The Problem

Creating entities that share a component set requires repeating the same `add_component`
calls everywhere. It's verbose and error-prone — a missing component on one entity silently
means that entity won't be enrolled in the system that needs it.

### The Fix: Archetype as a Named Type Set

An archetype is a `ComponentList` with a name. It carries its own signature computation.

```cpp
// archetypes.hpp
#include "components_list.hpp"

template <typename... Ts>
struct Archetype {
    // Compute the signature from the component IDs — constexpr, done at compile time
    static constexpr Signature signature() {
        Signature sig{};
        (sig.set(component_id<Ts>), ...);
        return sig;
    }
};

// Named archetypes — define all entity templates here
using PlayerArchetype   = Archetype<Transform2, RigidBody2, Sprite>;
using BulletArchetype   = Archetype<Transform2, RigidBody2, Lifetime>;
using ParticleArchetype = Archetype<Transform2, Lifetime>;
using StaticArchetype   = Archetype<Transform2, Sprite>;
```

### `World::create_from_archetype`

```cpp
// Overload 1: default-constructed components
// Useful for pooled entities that will have their data set after creation
template <typename A>
Entity create_from_archetype() {
    Entity e = entity_manager->create();
    // fold: calls add_component for each T in A's pack
    // A must expose its Ts... — see ArchetypeImpl below
    A::for_each_type([&]<typename T>() {
        add_component<T>(e, T{});
    });
    return e;
}

// Overload 2: caller supplies initial component values
// Args must match the archetype's component types in order
template <typename A, typename... Args>
Entity create_from_archetype(Args&&... args) {
    Entity e = entity_manager->create();
    (add_component(e, std::forward<Args>(args)), ...);
    return e;
}

// Usage
Entity player = world.create_from_archetype<PlayerArchetype>(
    Transform2{300.0f, 200.0f, 0.0f},
    RigidBody2{0.0f, 0.0f},
    Sprite{player_texture}
);

Entity bullet = world.create_from_archetype<BulletArchetype>(
    Transform2{gun_x, gun_y, angle},
    RigidBody2{vel_x, vel_y},
    Lifetime{3.0f}
);
```

### Compile-Time Safety Check

With constexpr signatures, you can assert at compile time that an archetype satisfies a
system's requirements. This catches missing components before the program runs.

```cpp
// In system headers, expose the required signature as a constexpr
class PhysicsSystem : public ISystem {
  public:
    static constexpr Signature required = Archetype<Transform2, RigidBody2, Gravity2>::signature();
    PhysicsSystem(...) : ISystem(required) { ... }
};

// In archetypes.hpp or a validation header
static_assert(
    (PhysicsSystem::required & PlayerArchetype::signature()) == PhysicsSystem::required,
    "PlayerArchetype is missing components required by PhysicsSystem"
);
```

---

## 4. Cleaner Schedule Construction

### The Problem

Building a `Schedule` in `main` requires knowing internal system integer IDs and calling
`set()` manually. The caller leaks `SystemManager` internals.

```cpp
// current — caller must know about SystemManager internals
Schedule physics_only{};
physics_only.set(SystemManager::get_system_id<PhysicsSystem>());
```

### The Fix: Templated Helper + Constexpr Constants

```cpp
// Similarly to component_id, define system_id using a SystemList
template <typename... Ts>
struct SystemList {
    static constexpr u8 count = sizeof...(Ts);
};

using Systems = SystemList<
    PhysicsSystem,   // ID 0
    RenderSystem,    // ID 1
    AnimationSystem  // ID 2
>;

template <typename T>
constexpr SystemType system_id = type_index<T, Systems>::value;

// Helper on World (or free function)
template <typename... Ts>
static constexpr Schedule make_schedule() {
    Schedule s{};
    (s.set(system_id<Ts>), ...);
    return s;
}
```

Schedules become named `constexpr` constants defined once, used everywhere:

```cpp
// game_config.hpp
// These are computed at compile time and embedded as literals in the binary.
// No runtime cost. No magic numbers. No SystemManager calls in main.

constexpr Schedule PHYSICS_ONLY  = make_schedule<PhysicsSystem>();
constexpr Schedule RENDER_ONLY   = make_schedule<RenderSystem>();
constexpr Schedule FULL_UPDATE   = make_schedule<PhysicsSystem, AnimationSystem>();
constexpr Schedule ALL_SYSTEMS   = make_schedule<PhysicsSystem, RenderSystem, AnimationSystem>();
```

The game loop stays clean:

```cpp
while (!WindowShouldClose()) {
    float dt = GetFrameTime();
    world.update(dt, PHYSICS_ONLY);

    BeginDrawing();
    ClearBackground(RAYWHITE);
    world.update(dt, RENDER_ONLY);
    EndDrawing();
}
```

The `schedule.test(i)` inside `SystemManager::update` is a single bit shift and AND —
already essentially free. Making the schedule `constexpr` means the bitset value is baked
into the binary as an immediate, not constructed at startup.

---

## 5. Better Containers

### 5a. `entity_to_idx`: Array vs Flat Map Based on Component Density

The `entity_to_idx` structure is the main cache hotspot in component access. The right
choice depends on how many entities actually have this component.

**Dense component** (most entities have it — Transform2, RigidBody2):
- Flat array wins: O(1) lookup, sequential in memory, prefetcher-friendly
- Cost: 4KB per component regardless of occupancy
- Crossover: when occupancy > ~15% of MAX_ENTITIES, array is strictly better

**Sparse component** (few entities have it — Lifetime, Stunned, a rare buff):
- Flat map wins: pays per entry, not per slot
- `std::flat_map<Entity, u32>`: sorted vector of pairs, O(log n) lookup, excellent
  iteration, no per-entry heap allocation
- Crossover: when occupancy < ~15%, flat_map uses less cache and is competitive on lookup

### Compile-Time Policy Selection

Tag the component type with its expected density:

```cpp
// Option A: member tag on the component struct
struct Transform2 {
    float x, y, rot;
    static constexpr bool is_sparse = false;
};

struct Lifetime {
    float remaining;
    static constexpr bool is_sparse = true;
};

// Option B: external trait (keeps component structs clean)
template <typename T> struct component_traits {
    static constexpr bool is_sparse = false;  // dense by default
};
template <> struct component_traits<Lifetime> { static constexpr bool is_sparse = true; };
template <> struct component_traits<Stunned>  { static constexpr bool is_sparse = true; };
```

`ComponentArray` selects the index type at compile time:

```cpp
template <ComponentType_t T>
class ComponentArray {
    static constexpr bool sparse = component_traits<T>::is_sparse;

    // One or the other exists — zero cost for the unchosen branch
    using IndexMap = std::conditional_t<
        sparse,
        std::flat_map<Entity, u32>,       // sparse: pays per entry
        std::array<u32, MAX_ENTITIES>     // dense:  pays upfront, O(1) access
    >;

    std::vector<T>      data;
    std::vector<Entity> idx_to_entity;
    IndexMap            entity_to_idx;
    u32 active_entities = 0;

    // Access is the same at every call site — the branch is resolved at compile time
    auto get_idx(Entity e) const -> u32 {
        if constexpr (sparse) {
            auto it = entity_to_idx.find(e);
            assert(it != entity_to_idx.end() && "Entity does not have component");
            return it->second;
        } else {
            u32 idx = entity_to_idx[e];
            assert(idx != INVALID && "Entity does not have component");
            return idx;
        }
    }

    void set_idx(Entity e, u32 idx) {
        if constexpr (sparse) {
            entity_to_idx.emplace(e, idx);
        } else {
            entity_to_idx[e] = idx;
        }
    }

    void clear_idx(Entity e) {
        if constexpr (sparse) {
            entity_to_idx.erase(e);
        } else {
            entity_to_idx[e] = INVALID;
        }
    }

    // Initialization differs too
    ComponentArray() {
        data.reserve(PRE_INIT_SIZE);
        idx_to_entity.reserve(PRE_INIT_SIZE);
        if constexpr (!sparse) {
            entity_to_idx.fill(INVALID);  // only for array variant
        }
    }
};
```

`add_data`, `remove_data`, `get_data` call through these helpers and are identical for both
variants. The compiler generates two completely different specializations.

### 5b. `SystemBase::entities`: `vector` + Swap-and-Pop

`flat_set<Entity>` keeps entities sorted, costing O(n) on every insertion and removal
(elements must shift to maintain order). For system entity lists sorted order gives you
nothing — you just need to visit all of them each frame.

```cpp
// In SystemBase (see section 6)
std::vector<Entity> entities;

void add_entity(Entity e) {
    entities.push_back(e);
}

void remove_entity(Entity e) {
    auto it = std::find(entities.begin(), entities.end(), e);
    assert(it != entities.end() && "Entity not in system");
    // swap with last then pop — O(1), order irrelevant for iteration
    *it = entities.back();
    entities.pop_back();
}

bool has_entity(Entity e) const {
    return std::find(entities.begin(), entities.end(), e) != entities.end();
}
```

`has_entity` is O(n) vs O(log n) for `flat_set`. This only matters if
`on_signature_change` fires frequently — which it shouldn't (structural changes are rare).
Profile before changing. If it shows up, a parallel `Signature`-sized bitset indexed by
entity ID is a cheap O(1) membership check with minimal memory cost.

### Container Decision Reference

| Use Case | Container | Why |
|---|---|---|
| Dense component index (`entity_to_idx`) | `std::array<u32, MAX_ENTITIES>` | O(1), prefetcher-friendly |
| Sparse component index | `std::flat_map<Entity, u32>` | Pays per entry, good cache |
| System entity list | `std::vector<Entity>` + swap-and-pop | O(1) remove, sequential iteration |
| Component data | `std::vector<T>` | Already optimal — keep it |
| Component arrays container | `std::tuple<ComponentArray<Ts>...>` | No heap, no pointers |
| System container | `std::tuple<Ts...>` | No heap, direct calls, inlinable |

**Current choice for system entities:** `vector` + swap-and-pop. Profile under real load
before considering alternatives — if `on_signature_change` is infrequent (expected),
this is the correct default.

---

## 6. Compile-Time System Tuple — Eliminate `ISystem` Indirection

### The Problem

Systems are stored and called through the same pointer-indirection pattern as components:

```cpp
std::array<u_ptr<ISystem>, MAX_SYSTEMS> systems;

// update loop — virtual dispatch through base pointer every frame
for (u8 i = 0; i < MAX_SYSTEMS; ++i) {
    if (schedule.test(i) && systems.at(i)) {
        systems.at(i)->update(dt);   // vtable lookup, pointer dereference, cache miss
    }
}
```

`ISystem` serves two roles here: shared state (entity list, signature) and the polymorphic
`update()` interface. Both can be eliminated.

### The Fix: `std::tuple<Ts...>` of Concrete System Types

Exactly the same pattern as the component tuple. With the `SystemList` from section 4,
`SystemManager` stores systems directly:

```cpp
template <typename... Ts>
class SystemManagerImpl {
    std::tuple<Ts...> systems;  // concrete types, inline storage, no heap

  public:
    template <typename T, typename... Args>
    void register_system(Args&&... args) {
        std::get<T>(systems) = T(std::forward<Args>(args)...);
    }

    void on_signature_change(Entity e, Signature new_sig) {
        std::apply([&](auto&... sys) {
            (sys.on_signature_change(e, new_sig), ...);
        }, systems);
    }

    void on_entity_destroyed(Entity e) {
        std::apply([&](auto&... sys) {
            (sys.on_entity_destroyed(e), ...);
        }, systems);
    }

    void update(float dt, Schedule schedule) {
        std::apply([&](auto&... sys) {
            // for each system: test its bit in the schedule, call update if set
            // the comma operator inside the fold evaluates left to right
            ((schedule.test(system_id<std::remove_cvref_t<decltype(sys)>>)
                ? sys.update(dt)
                : void()), ...);
        }, systems);
    }
};

template <typename List>
struct make_system_manager;

template <typename... Ts>
struct make_system_manager<SystemList<Ts...>> {
    using type = SystemManagerImpl<Ts...>;
};

using SystemManager = make_system_manager<Systems>::type;
```

### What Happens to `ISystem`

`ISystem` currently provides:
- `entities` — the entity set
- `signature` — the component bitmask
- `add_entity` / `remove_entity` / `has_entity`
- `virtual update()` — the polymorphic call

With the tuple, `virtual update()` is gone — the fold expression calls `update()` directly
on each concrete type. The rest (`entities`, `signature`, shared methods) can move into a
non-virtual CRTP base or a plain struct mixin that each system inherits from:

```cpp
// No virtual anything. Just shared data and methods.
struct SystemBase {
    std::vector<Entity> entities;     // section 5b: vector + swap-pop
    const Signature     signature;

    SystemBase(Signature sig) : signature(sig) {}

    void on_signature_change(Entity e, Signature new_sig) {
        bool qualifies = (signature & new_sig) == signature;
        bool has       = has_entity(e);
        if      ( qualifies && !has) add_entity(e);
        else if (!qualifies &&  has) remove_entity(e);
    }

    void on_entity_destroyed(Entity e) {
        if (has_entity(e)) remove_entity(e);
    }

    void add_entity(Entity e) {
        entities.push_back(e);
    }

    void remove_entity(Entity e) {
        auto it = std::find(entities.begin(), entities.end(), e);
        *it = entities.back();
        entities.pop_back();
    }

    bool has_entity(Entity e) const {
        return std::find(entities.begin(), entities.end(), e) != entities.end();
    }
};

// Each system inherits from SystemBase, not ISystem
class PhysicsSystem : public SystemBase {
  public:
    static constexpr Signature required = Archetype<Transform2, RigidBody2, Gravity2>::signature();

    PhysicsSystem(const ComponentManager& cm)
        : SystemBase(required)
        , transforms(cm.get_arr<Transform2>())
        , rigidbodies(cm.get_arr<RigidBody2>())
        , gravities(cm.get_arr<Gravity2>())
    {}

    void update(float dt) {   // NOT virtual — called directly by the fold expression
        for (u32 i = 0; i < gravities.active_count(); ++i) {
            Entity e       = gravities.entity_at(i);
            Gravity2&  g   = gravities.data_at(i);
            RigidBody2& rb = rigidbodies.get_data(e);
            Transform2& t  = transforms.get_data(e);
            rb.vy += g.force * dt;
            t.y   += rb.vy * dt;
            t.x   += rb.vx * dt;
        }
    }

  private:
    ComponentArray<Transform2>& transforms;
    ComponentArray<RigidBody2>& rigidbodies;
    ComponentArray<Gravity2>&   gravities;
};
```

### Memory Layout Comparison

```
Before:
systems[0] -> [heap] PhysicsSystem   <- cache miss, vtable lookup
systems[1] -> [heap] RenderSystem    <- cache miss, vtable lookup

After:
tuple: [ PhysicsSystem | RenderSystem | AnimationSystem ]
       ^--- inline, adjacent, direct call, fully inlinable
```

The compiler can now inline `update()` into the fold expression if it sees fit — something
impossible through a virtual call.

---

## 7. Iterating Component Arrays Directly (Archetypal Access Pattern)

### The Problem

Systems currently iterate their entity set and look up each component:

```cpp
// Current pattern — two random lookups per entity
for (Entity e : entities) {
    Transform2&  t = transforms.get_data(e);   // entity_to_idx[e] -> data[idx]
    RigidBody2&  r = rigidbodies.get_data(e);  // entity_to_idx[e] -> data[idx]
}
```

For 1000 entities with two components each, that's up to 2000 random memory accesses per
frame. Both `entity_to_idx` arrays are 4KB and accessed at random offsets — high cache
miss probability.

### The Fix: Iterate the Smaller Array Directly

Your `ComponentArray::data` vector is already packed and dense. The inner loop should walk
it sequentially and do at most one random lookup per entity for the secondary component.

```cpp
void update(float dt) override {
    // Iterate the smaller of the two arrays.
    // Every entity in `gravities` must also have Transform2 and RigidBody2
    // (the system signature guarantees this), so we can drive from any array.
    // Gravity2 is likely the smallest — fewest entities.

    for (u32 i = 0; i < gravities.active_count(); ++i) {
        Entity e        = gravities.entity_at(i);    // sequential read
        Gravity2&   g   = gravities.data_at(i);      // sequential read — no lookup
        RigidBody2& rb  = rigidbodies.get_data(e);   // one random lookup
        Transform2& t   = transforms.get_data(e);    // one random lookup
        
        rb.vy += g.force * dt;
        t.y   += rb.vy   * dt;
        t.x   += rb.vx   * dt;
    }
}
```

To expose `active_count()`, `entity_at(i)`, and `data_at(i)` on `ComponentArray`:

```cpp
template <ComponentType_t T>
class ComponentArray {
  public:
    // Direct sequential access for the driving loop
    auto active_count() const -> u32   { return active_entities; }
    auto entity_at(u32 i) const -> Entity { return idx_to_entity[i]; }
    auto data_at(u32 i) -> T&         { return data[i]; }
    auto data_at(u32 i) const -> const T& { return data[i]; }

    // Existing random-access path (still needed for secondary lookups)
    auto get_data(Entity e) -> T& { ... }
};
```

### Why This Is Better

```
Before (entity-set iteration):
  for each entity e in system.entities:       <- random order, cold cache
      transforms[entity_to_idx[e]]            <- random access into 4KB table
      rigidbodies[entity_to_idx[e]]           <- random access into 4KB table

After (direct array iteration):
  for i in 0..gravities.active_count():       <- sequential, hardware prefetcher works
      gravities.data[i]                        <- sequential, L1 cache hit
      rigidbodies[entity_to_idx[e]]           <- one random lookup (unavoidable)
      transforms[entity_to_idx[e]]            <- one random lookup (unavoidable)
```

The sequential read of the driving array is essentially free. You go from two random
lookups to one sequential read + two random lookups — roughly halving the cache pressure
on the hot path.

### Full Archetypal Storage (Future Direction)

The logical endpoint of this is storing components for entities that share a signature
together in contiguous memory — no cross-array lookups at all. This is true **archetype
storage** (used by Unity DOTS, Bevy, flecs).

```
Archetype<Transform2, RigidBody2, Gravity2>:
  [T2|RB2|G2] [T2|RB2|G2] [T2|RB2|G2] ...  <- all data for one entity adjacent
```

This eliminates the secondary random lookups entirely. Every access in the inner loop is
sequential. The tradeoff is significantly more complex structural change handling — when
an entity gains or loses a component it must move between archetype storage blocks.

The direct-array iteration above is the pragmatic 80% solution. True archetypal storage
is the right next step if profiling shows the secondary lookups are still the bottleneck
after the other changes here are applied.

---

## Change Order / Migration Path

Apply these in dependency order:

1. **Type list + compile-time component IDs** — foundational, everything else builds on this
2. **Component tuple + remove `ICompArr`** — unlocks fold expressions, kills virtual dispatch
3. **System type list + compile-time system IDs** — mirrors step 1 for systems
4. **System tuple + replace `ISystem` with `SystemBase`** — mirrors step 2 for systems
5. **Archetypes** — depends on compile-time IDs from steps 1 and 3
6. **Cleaner schedule construction** — depends on system IDs from step 3
7. **Better containers** — independent, do alongside or after steps 1-4
8. **Direct array iteration** — independent, apply per-system as you profile

Steps 1-4 are the core structural rewrite. After them: no virtual dispatch anywhere in
the ECS hot path, no heap indirection, all type resolution at compile time. Steps 5-8
are additive improvements that don't touch the core.
