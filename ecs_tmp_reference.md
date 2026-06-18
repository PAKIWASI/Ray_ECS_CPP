# C++23 Template Metaprogramming — ECS Reference

Every compile-time technique used in the ECS modernization, explained from first
principles with visual diagrams and annotated code. Each section builds on the last.

---

## Table of Contents

1. [Parameter Packs and Variadic Templates](#1-parameter-packs-and-variadic-templates)
2. [Type Lists](#2-type-lists)
3. [Compile-Time Index Lookup — `type_index`](#3-compile-time-index-lookup--type_index)
4. [`std::tuple` — Heterogeneous Storage](#4-stdtuple--heterogeneous-storage)
5. [Fold Expressions](#5-fold-expressions)
6. [`std::apply` — Calling a Function Over a Tuple](#6-stdapply--calling-a-function-over-a-tuple)
7. [`if constexpr` — Compile-Time Branching](#7-if-constexpr--compile-time-branching)
8. [`std::conditional_t` — Type Selection](#8-stdconditional_t--type-selection)
9. [Concepts — Constrained Templates](#9-concepts--constrained-templates)
10. [`constexpr` Functions and Variables](#10-constexpr-functions-and-variables)
11. [Putting It All Together — The ECS Type Pipeline](#11-putting-it-all-together--the-ecs-type-pipeline)

---

## 1. Parameter Packs and Variadic Templates

### What Is a Parameter Pack

A parameter pack is a template parameter that holds **zero or more types** (or values).
It is the foundation of everything else in this document.

```cpp
// A regular template — exactly one type
template <typename T>
struct Box { T value; };

// A variadic template — any number of types
template <typename... Ts>   // Ts is a "parameter pack"
struct MultiBox { };
```

The `...` before the name declares the pack. The `...` after the name expands it.

### Expanding a Pack

```cpp
template <typename... Ts>
struct Example {
    // sizeof...(Ts) — number of types in the pack, no ... needed
    static constexpr int count = sizeof...(Ts);

    // Ts... expands the pack in a context that accepts multiple types
    std::tuple<Ts...> data;
};

Example<int, float, char> e;
// Ts = {int, float, char}
// count = 3
// data  = std::tuple<int, float, char>
```

### Visualizing Pack Expansion

```
template <typename... Ts>   where Ts = {int, float, char}
                                        |      |      |
std::tuple<Ts...>    --->   std::tuple<int, float, char>
                                        |      |      |
                             slot 0    slot 1  slot 2
```

The compiler replaces `Ts...` with each type in the pack, separated by commas, in the
position where the expansion appears.

---

## 2. Type Lists

### The Problem They Solve

We need a way to name a set of types, iterate over them at compile time, and query them.
A type list is a struct that wraps a parameter pack and makes it inspectable.

### Definition

```cpp
template <typename... Ts>
struct ComponentList {
    //  Ts...  is stored as part of the template specialization
    //  We can get it back by specializing on ComponentList<Ts...>

    static constexpr u8 count = sizeof...(Ts);
};

// Instantiation — the types are baked into the type itself
using Components = ComponentList<Transform2, RigidBody2, Gravity2, Sprite, Lifetime>;
//                                    |           |          |        |        |
//                               position 0   position 1  pos 2   pos 3   pos 4
//                               = ID 0       = ID 1      = ID 2  = ID 3  = ID 4
```

### Visualizing the Type List

```
ComponentList < Transform2,  RigidBody2,  Gravity2,  Sprite,  Lifetime >
                     |            |           |          |         |
                  index 0      index 1     index 2    index 3   index 4
                  = ID 0       = ID 1      = ID 2     = ID 3    = ID 4
```

The position in this list IS the component ID. Changing the order changes IDs — so once
defined, never reorder.

### How to Access the Pack Later

To query `ComponentList`, you specialize a helper on `ComponentList<Ts...>` to unpack
the `Ts...` again:

```cpp
// Primary template — matches nothing on its own
template <typename T, typename List>
struct type_index;

// Partial specialization — matches ComponentList<Ts...> and unpacks Ts
template <typename T, typename... Ts>
struct type_index<T, ComponentList<Ts...>> {
    //                              ^^^^^^
    //                              Ts is now accessible here
    //                              This is the key pattern
};
```

This partial specialization trick is used everywhere in TMP. The outer type (`ComponentList`)
is the "envelope" — the specialization "opens" it and gets the contents.

---

## 3. Compile-Time Index Lookup — `type_index`

### Goal

Given a type `T` and a `ComponentList`, find the position of `T` in the list at compile
time, with a hard error if `T` is not present.

### The Implementation

```cpp
template <typename T, typename List>
struct type_index;  // primary — intentionally undefined

template <typename T, typename... Ts>
struct type_index<T, ComponentList<Ts...>> {
    //  Step 1: expand a pack of booleans — one per type in Ts
    //  std::is_same_v<T, Ts>... expands to:
    //  { is_same_v<T, Transform2>, is_same_v<T, RigidBody2>, ... }

    static constexpr u8 value = []() constexpr -> u8 {
        //  constexpr array of booleans — built at compile time
        constexpr bool matches[] = { std::is_same_v<T, Ts>... };
        //                                              ^^^
        //                            pack expansion inside braced-init-list

        for (u8 i = 0; i < sizeof...(Ts); ++i)
            if (matches[i]) return i;

        //  T is not in the list — compile error
        static_assert(false, "Type not in ComponentList");
    }();
    //  ^^ immediately invoked constexpr lambda — evaluates at compile time
};

// Convenience alias — hides the struct::value syntax
template <typename T>
constexpr u8 component_id = type_index<T, Components>::value;
```

### Step-by-Step for `component_id<Gravity2>`

```
Components = ComponentList<Transform2, RigidBody2, Gravity2, Sprite, Lifetime>

T = Gravity2

Step 1 — expand std::is_same_v<Gravity2, Ts>...:
  matches[] = { false, false, true, false, false }
               Transform2  RigidBody2  Gravity2  Sprite  Lifetime

Step 2 — loop:
  i=0: matches[0] = false, skip
  i=1: matches[1] = false, skip
  i=2: matches[2] = true,  return 2

component_id<Gravity2> = 2   (a compile-time constant, like a literal)
```

### What the Compiler Sees at the Call Site

```cpp
// You write this:
sig.set(component_id<Gravity2>);

// The compiler replaces component_id<Gravity2> with the integer 2:
sig.set(2);

// It is identical to having written:
sig.set(2);
// But type-safe — wrong type = compile error, not a silent wrong number
```

---

## 4. `std::tuple` — Heterogeneous Storage

### What It Is

`std::tuple<Ts...>` stores one value of each type in `Ts`, in order, in a single object.
Unlike an array (homogeneous — all elements same type), a tuple is heterogeneous.

```cpp
std::tuple<int, float, std::string> t { 42, 3.14f, "hello" };

std::get<0>(t)  // -> 42         (int)
std::get<1>(t)  // -> 3.14f      (float)
std::get<2>(t)  // -> "hello"    (string)

// You can also get by type (if the type is unique in the tuple)
std::get<float>(t)  // -> 3.14f
```

### Memory Layout

```
std::tuple<int, float, std::string>

[ int (4B) ][ float (4B) ][ string (24B) ]
     ^            ^              ^
  get<0>()     get<1>()      get<2>()

All inline — one allocation, no pointers, no heap.
```

Compare to what we had before:

```
std::array<unique_ptr<ICompArr>, 3>

[ ptr ]---> [heap] ComponentArray<Transform2>   <- cache miss
[ ptr ]---> [heap] ComponentArray<RigidBody2>   <- cache miss
[ ptr ]---> [heap] ComponentArray<Gravity2>     <- cache miss

Three separate heap allocations, three pointer dereferences per access.
```

### Using a Pack to Build a Tuple

```cpp
// Given: ComponentList<Transform2, RigidBody2, Gravity2>
// Want:  std::tuple<ComponentArray<Transform2>,
//                   ComponentArray<RigidBody2>,
//                   ComponentArray<Gravity2>>

template <typename... Ts>
class ComponentManagerImpl {
    //  Ts... expands inside the tuple's template argument list
    std::tuple<ComponentArray<Ts>...> arrays;
    //                         ^^^
    //         This is: tuple<ComponentArray<Transform2>,
    //                        ComponentArray<RigidBody2>,
    //                        ComponentArray<Gravity2>>
};
```

### `std::get<T>()` for Component Access

```cpp
template <typename T>
auto get_arr() -> ComponentArray<T>& {
    return std::get<ComponentArray<T>>(arrays);
    //             ^^^^^^^^^^^^^^^^^^^
    //             get by type — no index needed
    //             resolved entirely at compile time
}

// Usage:
get_arr<Transform2>()  // returns ComponentArray<Transform2>&
                       // compiler knows exactly which slot — zero overhead
```

### Unpacking a Type List into a Tuple

`ComponentManagerImpl<Ts...>` takes a pack. But we have a `ComponentList`. We need to
unpack the list into the manager's pack parameter:

```cpp
// The "unwrapper" pattern — specialize to extract the pack
template <typename List>
struct make_component_manager;

template <typename... Ts>
struct make_component_manager<ComponentList<Ts...>> {
    //                                      ^^^^^
    //  Specialization extracts Ts... from ComponentList
    using type = ComponentManagerImpl<Ts...>;
    //                                ^^^^^
    //  Then forwards it to ComponentManagerImpl
};

// Final alias — this is what the rest of the code uses
using ComponentManager = make_component_manager<Components>::type;
//    ^^^^^^^^^^^^^^^
//    = ComponentManagerImpl<Transform2, RigidBody2, Gravity2, Sprite, Lifetime>
```

Visually:

```
Components
= ComponentList< Transform2, RigidBody2, Gravity2 >
                      |           |          |
                      v           v          v
make_component_manager unpacks...
                      |           |          |
                      v           v          v
ComponentManagerImpl< Transform2, RigidBody2, Gravity2 >
                      |           |          |
                      v           v          v
tuple< ComponentArray<Transform2>, ComponentArray<RigidBody2>, ComponentArray<Gravity2> >
```

---

## 5. Fold Expressions

### What They Are

A fold expression collapses a parameter pack using a binary operator. They are the
replacement for recursive template metaprogramming in C++17 and later.

```
Syntax:   (expr op ...)        — right fold
          (... op expr)        — left fold
          (init op ... op expr) — left fold with initial value
```

### The Simplest Case — Summing a Pack

```cpp
template <typename... Ts>
constexpr int sum_sizes() {
    return (sizeof(Ts) + ...);
    //      ^^^^^^^^^^   ^^^
    //      expression   fold marker
}

sum_sizes<int, float, char>()
// expands to: sizeof(int) + sizeof(float) + sizeof(char)
//           = 4 + 4 + 1
//           = 9
```

### Setting Bits in a Signature

```cpp
template <typename... Ts>
static constexpr Signature make_signature() {
    Signature sig{};
    (sig.set(component_id<Ts>), ...);
    //  ^^^^^^^^^^^^^^^^^^^^^^^^  ^^^
    //  expression (with comma op) fold marker
    //
    // expands to:
    //   sig.set(component_id<Transform2>),
    //   sig.set(component_id<RigidBody2>),
    //   sig.set(component_id<Gravity2>)
    return sig;
}
```

The comma operator sequences the calls left-to-right. The fold expands the expression
once per type in the pack.

### Calling a Method on Every Tuple Element — The Core ECS Pattern

This is used in `entity_destroyed` and `SystemManager::update`:

```cpp
// Goal: call remove_if_present(e) on each ComponentArray in the tuple

void entity_destroyed(Entity e) {
    std::apply([&](auto&... arr) {
    //                    ^^^
    //  auto&... is a pack of references to each tuple element
    //  each arr is a different type (ComponentArray<Transform2>, etc.)
        (arr.remove_if_present(e), ...);
        //  ^^^^^^^^^^^^^^^^^^^^^^  ^^^
        //  called on each arr      fold
    }, arrays);
}

// With arrays = tuple<ComponentArray<T2>, ComponentArray<RB2>, ComponentArray<G2>>
// expands to:
//   arr0.remove_if_present(e),   <- ComponentArray<Transform2>
//   arr1.remove_if_present(e),   <- ComponentArray<RigidBody2>
//   arr2.remove_if_present(e)    <- ComponentArray<Gravity2>
```

### Conditional Fold — The `update` Loop

```cpp
void update(float dt, Schedule schedule) {
    std::apply([&](auto&... sys) {
        ((schedule.test(system_id<std::remove_cvref_t<decltype(sys)>>)
            ? sys.update(dt)
            : void()), ...);
    }, systems);
}
```

Breaking this down piece by piece:

```cpp
decltype(sys)
// The type of the current sys in the expansion.
// When expanding over tuple<PhysicsSystem, RenderSystem>:
//   first iteration:  decltype(sys) = PhysicsSystem&
//   second iteration: decltype(sys) = RenderSystem&

std::remove_cvref_t<decltype(sys)>
// Strips reference and const:
//   PhysicsSystem& -> PhysicsSystem
//   RenderSystem&  -> RenderSystem

system_id<PhysicsSystem>
// Compile-time integer — the bit position in the Schedule bitset

schedule.test(system_id<PhysicsSystem>)
// Runtime: is this system enabled in the current schedule?

? sys.update(dt) : void()
// If enabled: call update directly (not virtual)
// If disabled: no-op (void() is a no-op expression)

(expr, ...)
// Fold with comma — evaluates the whole conditional for each system
```

Expanded for two systems:

```
(schedule.test(0) ? physics.update(dt) : void()),
(schedule.test(1) ? render.update(dt)  : void())
```

---

## 6. `std::apply` — Calling a Function Over a Tuple

### What It Does

`std::apply(f, tuple)` calls `f` with the tuple's elements unpacked as individual
arguments. It bridges the gap between "data in a tuple" and "function that takes a pack."

```cpp
auto t = std::make_tuple(1, 2.0f, 'c');

std::apply([](int a, float b, char c) {
    // a = 1, b = 2.0f, c = 'c'
}, t);

// Equivalent to:
// f(std::get<0>(t), std::get<1>(t), std::get<2>(t))
```

### With `auto&...` — Generic Lambdas Over Heterogeneous Tuples

The power comes from using a generic lambda with a pack parameter:

```cpp
std::tuple<ComponentArray<Transform2>, ComponentArray<RigidBody2>> arrays;

std::apply([&](auto&... arr) {
//               ^^^^^^^^
//  arr is a pack: {ComponentArray<Transform2>&, ComponentArray<RigidBody2>&}
//  Each element has a different type — only possible with auto&...

    (arr.remove_if_present(e), ...);
//   ^^^
//  Works on any type that has remove_if_present(Entity)
//  The compiler generates a separate call for each type

}, arrays);
```

Without `std::apply`, you'd need recursive template functions or index sequences to
iterate a tuple — much more verbose. `std::apply` + a generic lambda + a fold is the
idiomatic C++17/23 pattern.

### Why Not a Range-Based For Loop

```cpp
// This does NOT work — tuple elements have different types
for (auto& arr : arrays) {  // ERROR: tuple is not a range
    arr.remove_if_present(e);
}

// std::apply is the correct replacement
std::apply([&](auto&... arr) {
    (arr.remove_if_present(e), ...);
}, arrays);
```

### Visualizing `std::apply` + Fold

```
arrays = tuple< CompArr<T2>,      CompArr<RB2>,     CompArr<G2> >
                    |                  |                 |
                   arr0               arr1              arr2
                    |                  |                 |
                    v                  v                 v
             arr0.remove_if_present(e)
                                   arr1.remove_if_present(e)
                                                    arr2.remove_if_present(e)

All three calls are direct, inlined by the compiler, zero virtual dispatch.
```

---

## 7. `if constexpr` — Compile-Time Branching

### What It Is

`if constexpr` evaluates its condition at compile time. The branch not taken is not
compiled — it doesn't even need to be syntactically valid for the current type.

```cpp
template <typename T>
void print_type() {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "integer: " << T{} << "\n";
        // This branch only compiled when T is an integer type
    } else {
        std::cout << "not an integer\n";
        // This branch only compiled otherwise
    }
}
```

### In the ECS — Sparse vs Dense Index Selection

```cpp
template <ComponentType_t T>
class ComponentArray {
    static constexpr bool sparse = component_traits<T>::is_sparse;

    auto get_idx(Entity e) const -> u32 {
        if constexpr (sparse) {
            // This branch compiled only for sparse T
            // flat_map::find would be a compile error on array — doesn't matter,
            // this branch doesn't exist for dense T
            auto it = entity_to_idx.find(e);
            assert(it != entity_to_idx.end());
            return it->second;
        } else {
            // This branch compiled only for dense T
            return entity_to_idx[e];
        }
    }
};
```

With regular `if`, both branches must compile for all types. With `if constexpr`, each
branch is compiled only for the types where the condition is true — so you can call
`.find()` on a `flat_map` in one branch and `operator[]` on an `array` in the other,
even though neither supports the other's operation.

### Generated Code

For `ComponentArray<Transform2>` where `sparse = false`:

```cpp
// The compiler generates exactly this — the if constexpr is gone:
auto get_idx(Entity e) const -> u32 {
    return entity_to_idx[e];  // direct array index, single instruction
}
```

For `ComponentArray<Lifetime>` where `sparse = true`:

```cpp
// The compiler generates exactly this:
auto get_idx(Entity e) const -> u32 {
    auto it = entity_to_idx.find(e);
    assert(it != entity_to_idx.end());
    return it->second;
}
```

Two completely different implementations, zero runtime branching, selected at compile time.

---

## 8. `std::conditional_t` — Type Selection

### What It Is

`std::conditional_t<condition, TrueType, FalseType>` selects one of two types at compile
time based on a boolean condition. It is the type-level equivalent of the ternary operator.

```cpp
// Ternary for values:
int x = condition ? 42 : 0;

// conditional_t for types:
using T = std::conditional_t<condition, TypeA, TypeB>;
```

### In the ECS — Choosing the Index Container

```cpp
template <ComponentType_t T>
class ComponentArray {
    static constexpr bool sparse = component_traits<T>::is_sparse;

    using IndexMap = std::conditional_t<
        sparse,                          // condition
        std::flat_map<Entity, u32>,      // type if true  (sparse)
        std::array<u32, MAX_ENTITIES>    // type if false (dense)
    >;
    //  IndexMap is now EITHER flat_map OR array
    //  Only one ever exists in any given ComponentArray<T> instantiation

    IndexMap entity_to_idx;  // the member has different types for different T
};
```

### What the Compiler Produces

```
ComponentArray<Transform2>   (sparse = false)
    IndexMap = std::array<u32, MAX_ENTITIES>
    entity_to_idx: [ 0, 1, INVALID, 2, INVALID, ... ]   <- 4KB flat array

ComponentArray<Lifetime>     (sparse = true)
    IndexMap = std::flat_map<Entity, u32>
    entity_to_idx: { 3->0, 7->1, 42->2 }                <- only active entries
```

The same class definition produces structurally different types. No runtime overhead, no
branching, no wasted memory for the unused structure.

---

## 9. Concepts — Constrained Templates

### What They Are

Concepts are named compile-time predicates that constrain what types a template will
accept. They replace SFINAE with readable, composable constraints.

```cpp
// Without concepts — works but error messages are unreadable
template <typename T, typename = std::enable_if_t<std::is_default_constructible_v<T>>>
void foo(T t) { ... }

// With concepts — clear and composable
template <typename T>
concept ComponentType_t = std::default_initializable<T>
                       && std::movable<T>;

template <ComponentType_t T>  // constraint is part of the declaration
class ComponentArray { ... };
```

### Concepts Used in the ECS

```cpp
// A valid component type: can be default-constructed and moved
template <typename T>
concept ComponentType_t = std::default_initializable<T>
                       && std::movable<T>;

// A valid system type: must derive from SystemBase
template <typename T>
concept SystemType_t = std::derived_from<T, SystemBase>;
```

### What Happens on Violation

```cpp
struct BadComp {
    BadComp() = delete;  // not default constructible
};

ComponentArray<BadComp> arr;  // COMPILE ERROR
// error: 'BadComp' does not satisfy 'ComponentType_t'
// note: 'std::default_initializable<BadComp>' evaluated to false
```

The error points directly at the violated concept — not at some internal instantiation
failure deep in the template.

---

## 10. `constexpr` Functions and Variables

### What They Are

`constexpr` tells the compiler: "this can be evaluated at compile time." If all inputs
are compile-time constants, the result is embedded in the binary as a literal.

```cpp
constexpr int square(int x) { return x * x; }

constexpr int nine = square(3);  // computed at compile time, embedded as 9
int runtime_val = 5;
int dynamic = square(runtime_val);  // computed at runtime, regular call
```

### `constexpr` Variables

```cpp
// These are compile-time constants — identical to literals at the call site
template <typename T>
constexpr u8 component_id = type_index<T, Components>::value;

constexpr u8 id = component_id<Gravity2>;  // = 2, a compile-time constant
```

### `constexpr` Signatures

```cpp
// Computed once at compile time, reused everywhere
class PhysicsSystem : public SystemBase {
  public:
    static constexpr Signature required =
        Archetype<Transform2, RigidBody2, Gravity2>::signature();
    //  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //  This is evaluated when the class is compiled.
    //  `required` is a compile-time bitset constant.

    PhysicsSystem(const ComponentManager& cm)
        : SystemBase(required)  // passes the constexpr bitset to base
        , ...
    {}
};
```

### `constexpr` Schedules

```cpp
// Computed once, zero construction cost at runtime
constexpr Schedule PHYSICS_ONLY = make_schedule<PhysicsSystem>();
constexpr Schedule RENDER_ONLY  = make_schedule<RenderSystem>();
constexpr Schedule ALL          = make_schedule<PhysicsSystem, RenderSystem>();

// In the game loop — schedule is already a known bit pattern
// The compiler can even constant-fold the test() calls
world.update(dt, PHYSICS_ONLY);  // PHYSICS_ONLY is a literal, not a variable
```

### Immediately Invoked Constexpr Lambda

Used inside `type_index` to run a loop at compile time:

```cpp
static constexpr u8 value = []() constexpr -> u8 {
//                           ^^ lambda         ^^ must return u8
//                              immediately invoked (the () at the end calls it)
    constexpr bool matches[] = { std::is_same_v<T, Ts>... };
    for (u8 i = 0; i < sizeof...(Ts); ++i)
        if (matches[i]) return i;
    static_assert(false, "Type not in list");
}();
//^^
//  Call the lambda immediately.
//  The result (u8 index) is the value of the constexpr variable.
```

The lambda is called once by the compiler, its result is stored as `value`, and the lambda
itself disappears from the binary entirely.

---

## 11. Putting It All Together — The ECS Type Pipeline

This section shows how all the above connects into the full compile-time ECS pipeline.

### The Full Data Flow

```
Source: ComponentList<Transform2, RigidBody2, Gravity2>
               |
               | type_index<T, Components>::value
               | (pack expansion + constexpr loop)
               v
        component_id<Transform2> = 0   (compile-time constant)
        component_id<RigidBody2> = 1
        component_id<Gravity2>   = 2
               |
               | make_component_manager<Components>::type
               | (partial specialization extracts pack)
               v
        ComponentManagerImpl<Transform2, RigidBody2, Gravity2>
               |
               | std::tuple<ComponentArray<Ts>...>
               | (pack expansion inside tuple)
               v
        tuple< ComponentArray<Transform2>,
               ComponentArray<RigidBody2>,
               ComponentArray<Gravity2>  >
               |
               | std::get<ComponentArray<T>>(arrays)
               | (compile-time slot lookup, zero overhead)
               v
        ComponentArray<Transform2>&   <- direct reference, no pointer, no cast
```

### Signature Construction — End to End

```
Archetype<Transform2, RigidBody2, Gravity2>::signature()
    |
    | (sig.set(component_id<Ts>), ...)    <- fold expression
    | expands to:
    |   sig.set(0),   <- component_id<Transform2>
    |   sig.set(1),   <- component_id<RigidBody2>
    |   sig.set(2)    <- component_id<Gravity2>
    v
Signature{0b00000111}     <- constexpr bitset
    = bits 0, 1, 2 set
    = "entity has Transform2, RigidBody2, Gravity2"
```

### System Update — End to End

```
world.update(dt, PHYSICS_ONLY)
    |
    | PHYSICS_ONLY = constexpr Schedule{0b00000001}  <- bit 0 set (PhysicsSystem)
    |
    v
SystemManagerImpl::update(dt, schedule)
    |
    | std::apply([&](auto&... sys) { ... }, systems)
    | unpacks tuple<PhysicsSystem, RenderSystem> into sys pack
    |
    v
Fold expansion:
    (schedule.test(0) ? physics.update(dt) : void()),   // bit 0 set -> called
    (schedule.test(1) ? render.update(dt)  : void())    // bit 1 clear -> skipped
    |
    | PhysicsSystem::update is a regular (non-virtual) function
    | compiler can inline the entire body here
    v
PhysicsSystem::update(dt) inlined:
    for (u32 i = 0; i < gravities.active_count(); ++i) {
        Entity e       = gravities.entity_at(i);      // sequential
        Gravity2&  g   = gravities.data_at(i);        // sequential
        RigidBody2& rb = rigidbodies.get_data(e);     // one lookup
        Transform2& t  = transforms.get_data(e);      // one lookup
        rb.vy += g.force * dt;
        t.y   += rb.vy * dt;
        t.x   += rb.vx * dt;
    }
```

### What the Compiler Has Eliminated

| Was | Now |
|---|---|
| Runtime ID assignment via static local | Compile-time integer constant |
| `unique_ptr` array of `ICompArr*` | Inline tuple, no heap |
| `static_cast` downcast on every access | `std::get<T>()`, resolved at compile time |
| Virtual `entity_destroyed` dispatch | Fold expression, direct calls, inlinable |
| Virtual `update()` per system | Direct call per system, inlinable |
| Manual `register_component<T>()` calls | Constructor folds over type list automatically |
| `SystemManager::get_system_id<T>()` in `main` | `constexpr Schedule` constant |
| Runtime branch in `update` loop | Compile-time `schedule.test(i)` on a known bitset |

---

## Quick Reference — Syntax Cheatsheet

```cpp
// Parameter pack declaration and expansion
template <typename... Ts>       // declare pack Ts
std::tuple<Ts...>               // expand pack — becomes tuple<T1, T2, ...>
sizeof...(Ts)                   // number of types in pack (no ... needed here)

// Type list
template <typename... Ts>
struct TypeList {};
using MyList = TypeList<A, B, C>;

// Partial specialization to unpack a list
template <typename T, typename... Ts>
struct Helper<T, TypeList<Ts...>> { ... };  // Ts is now the list's contents

// Fold expressions
(expr op ...)           // right fold:  e1 op (e2 op e3)
(... op expr)           // left fold:   (e1 op e2) op e3
(expr, ...)             // comma fold:  e1, e2, e3  (sequences calls)

// std::apply
std::apply([](auto&... args) { ... }, tuple);  // args = each element of tuple

// if constexpr
if constexpr (condition) { /* compiled if true  */ }
else                     { /* compiled if false */ }

// std::conditional_t
using T = std::conditional_t<bool_condition, TypeIfTrue, TypeIfFalse>;

// constexpr variable
template <typename T>
constexpr u8 my_id = some_struct<T>::value;

// Immediately invoked constexpr lambda
static constexpr u8 val = []() constexpr -> u8 {
    return 42;
}();
```
