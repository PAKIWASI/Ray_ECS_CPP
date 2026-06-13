#pragma once
// ^^^^^^^^^^^
// A non-standard but universally supported compiler directive.
// Tells the compiler: "only include this file once per compilation unit,
// even if #included multiple times." The old way was include guards:
//   #ifndef ECS_HPP
//   #define ECS_HPP
//   ...
//   #endif
// #pragma once is shorter and less error-prone. Use it everywhere.

#include <algorithm>   // std::remove (used to erase from vectors)
#include <bitset>      // std::bitset — a fixed-size array of bits (our component mask)
#include <cassert>     // assert() — runtime checks that crash with a message if false
#include <concepts>    // std::default_initializable, std::movable, std::derived_from
#include <cstddef>     // std::size_t — the type for sizes/indices (usually uint64_t)
#include <cstdint>     // uint8_t, uint32_t etc. — exact-width integer types
#include <flat_map>    // std::flat_map — C++23 sorted map backed by a vector (cache-friendly)
#include <limits>      // std::numeric_limits — lets us get min/max of any numeric type
#include <memory>      // std::unique_ptr, std::shared_ptr, std::make_unique, std::make_shared
#include <queue>       // std::queue — FIFO container (used for available entity IDs)
#include <typeindex>   // std::type_index — a hashable wrapper around std::type_info
#include <utility>     // std::forward, std::move


// ============================================================
//  CONFIGURATION
// ============================================================
//
// These are compile-time constants. 'inline constexpr' means:
//   - constexpr: the value is computed at compile time, usable in
//     array sizes, template arguments, static_asserts, etc.
//   - inline: there is only ONE copy of this variable across all
//     translation units (.cpp files) that include this header.
//     Without inline, each .cpp would get its own copy, causing
//     "multiple definition" linker errors.
//
// Why not #define MAX_ENTITIES 1000?
//   #define is a text substitution. It has no type, no scope, no
//   debugger visibility. constexpr is a real typed variable. Always
//   prefer constexpr over #define for constants in C++.

inline constexpr uint32_t MAX_ENTITIES   = 1000;
//               ^^^^^^^^
//               uint32_t: an unsigned 32-bit integer. Holds 0–4,294,967,295.
//               We use this to store entity IDs, so 1000 max entities is fine.

inline constexpr uint8_t  MAX_COMPONENTS = 64;
//               ^^^^^^^
//               uint8_t: an unsigned 8-bit integer. Holds 0–255.
//               We only have 64 component types, so 8 bits is plenty.
//               This also directly controls the size of our Signature bitset below.


// ============================================================
//  TYPES
// ============================================================
//
// In C, you'd write: typedef uint32_t Entity;
// In C++, 'using' aliases are cleaner and support templates:

using Entity = uint32_t;
// ^^^^^^^^^^^^^^^^^^^^
// An entity is JUST A NUMBER. Nothing more. No data, no methods.
// It's an ID we hand out and use to look up components.
// We call it 'Entity' instead of 'uint32_t' everywhere so the
// code is self-documenting: when you see Entity, you know it's an ID.

inline constexpr Entity NULL_ENTITY = std::numeric_limits<uint32_t>::max();
//                                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//                                    std::numeric_limits<uint32_t>::max() == 4,294,967,295
//                                    We use this as a sentinel "no entity" value,
//                                    like NULL for pointers. An entity with this ID
//                                    is guaranteed to be invalid (we'd run out of memory
//                                    long before creating 4 billion entities).

using Signature = std::bitset<MAX_COMPONENTS>;
//    ^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//    We name this type 'Signature' because it describes the
//    "signature" of an entity (which components it has) or a system
//    (which components it requires).
//
//    std::bitset<64> is a 64-bit mask. Each bit position corresponds
//    to one component type. If bit 3 is set, the entity has component #3.
//
//    Example with 4 components (simplified to 4 bits):
//      Transform = bit 0
//      Velocity  = bit 1
//      Health    = bit 2
//      Sprite    = bit 3
//
//    An entity with Transform + Velocity: 0b0011
//    An entity with all four:             0b1111
//    PhysicsSystem needs Transform+Velocity: requires 0b0011
//
//    Matching check: (entity_sig & system_sig) == system_sig
//    i.e. "does the entity have AT LEAST the bits the system needs?"

using ComponentType = uint8_t;
//    ^^^^^^^^^^^^^
//    The ID assigned to each component type. Just a number 0-63.
//    We use this to SET the right bit in a Signature.
//    e.g. if Transform's ComponentType is 0, we do: signature.set(0)


// ============================================================
//  COMPONENT TYPE ID GENERATOR
// ============================================================
//
// The problem: we need to assign a unique integer ID to each
// component struct (Transform, Velocity, Health...) so we know
// which bit in the Signature each one corresponds to.
//
// We can't use enum because we don't want to manually number them.
// We can't use the type's name as a string — too slow.
//
// Solution: a counter + a template trick. Each distinct type T
// that ever calls component_type_id<T>() gets assigned the next
// available counter value, ONCE, and then returns that same value forever.

inline ComponentType component_type_counter = 0;
//     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//     A global counter. Starts at 0. Each new component type
//     grabs the current value and increments it.
//     'inline' so it exists only once across all included files.

template <typename T>
auto component_type_id() -> ComponentType
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// This is a FUNCTION TEMPLATE. The compiler generates a separate
// version of this function for each type T you call it with.
//
// component_type_id<Transform>() → one function
// component_type_id<Velocity>()  → a completely different function
// component_type_id<Health>()    → yet another function
//
// The return type uses "trailing return type" syntax: auto f() -> Type
// This is equivalent to ComponentType component_type_id().
// Trailing syntax is often cleaner when the return type is complex.
{
    static ComponentType id = component_type_counter++;
    //     ^^^^^^
    //     KEY TRICK: 'static' inside a function means this variable
    //     is initialized EXACTLY ONCE — the first time this specific
    //     function instantiation is called. Every subsequent call
    //     just returns the already-stored value.
    //
    //     Because each T gets its OWN function instantiation, each T
    //     gets its OWN 'static ComponentType id'. They don't share.
    //
    //     First call to component_type_id<Transform>():
    //       id = component_type_counter++ → id=0, counter becomes 1
    //     Second call to component_type_id<Transform>():
    //       id is already 0 (static, not re-initialized) → returns 0
    //     First call to component_type_id<Velocity>():
    //       id = component_type_counter++ → id=1, counter becomes 2
    //
    //     Result: Transform=0, Velocity=1, Health=2, etc. — forever consistent.

    assert(id < MAX_COMPONENTS && "Exceeded MAX_COMPONENTS");
    //     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //     assert() checks a condition at runtime. If false, it prints
    //     the message and crashes with a stack trace. This is a
    //     development-time guard — catches bugs during testing.
    //     In release builds you can define NDEBUG to strip all asserts.

    return id;
}


// ============================================================
//  CONCEPT: ComponentType_t
// ============================================================
//
// A concept is a named, compile-time constraint on a template parameter.
// It answers the question: "what requirements must T satisfy to be used
// as a component?"
//
// WHY do we need this? Without it, you could accidentally call
// world.add_component<SomeRandomNonCopyableType>(entity, val) and get
// a cryptic 3-page template error deep inside ComponentArray.
// With a concept, you get a clear error at the CALL SITE:
//   "constraint 'ComponentType_t<X>' not satisfied"

template <typename T>
concept ComponentType_t = std::default_initializable<T> && std::movable<T>;
//                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//                        Two standard library concepts combined with &&:
//
//                        std::default_initializable<T>:
//                          T{} must compile. We need this because ComponentArray
//                          pre-allocates an array of T with default values.
//                          If T had no default constructor, the array couldn't
//                          be created.
//
//                        std::movable<T>:
//                          std::move(t) must work (move constructor + move assign).
//                          We use std::move() when inserting into the packed array
//                          to avoid unnecessary copies of large component data.
//
//                        Note: we DON'T require std::copyable because components
//                        never need to be copied — they're always moved or accessed
//                        by reference.


// ============================================================
//  IComponentArray — THE TYPE ERASURE INTERFACE
// ============================================================
//
// THE CORE DESIGN PROBLEM:
//   We want to store ComponentArray<Transform>, ComponentArray<Velocity>,
//   ComponentArray<Health> etc. all in ONE map inside ComponentManager.
//
//   But std::flat_map<K, ComponentArray<???>> — what goes in the ???
//   You can't have a map of "ComponentArray of any type". Templates don't
//   work that way. ComponentArray<Transform> and ComponentArray<Velocity>
//   are completely unrelated types to the compiler.
//
// SOLUTION: Type erasure via a virtual base class.
//   We define a non-template base class with the operations that
//   ComponentManager needs to call on ALL arrays regardless of T.
//   The only such operation is entity_destroyed() — called when an
//   entity dies, so we can clean up whichever component T it had.
//
//   The map stores shared_ptr<IComponentArray> — a pointer to the base.
//   When we need type-specific operations (insert, get, remove), we
//   cast back to the concrete ComponentArray<T> using static_pointer_cast.

class IComponentArray
{
  public:
    virtual ~IComponentArray() = default;
    //      ^^^^^^^^^^^^^^^^^^
    //      Virtual destructor: REQUIRED whenever a class has virtual methods
    //      and will be deleted through a base class pointer.
    //
    //      Without this:
    //        IComponentArray* p = new ComponentArray<Transform>();
    //        delete p;  // calls ~IComponentArray(), NOT ~ComponentArray<Transform>()
    //                   // → memory leak / undefined behavior
    //
    //      With virtual destructor:
    //        delete p;  // calls ~ComponentArray<Transform>() → correct cleanup
    //
    //      '= default' means: generate the default (empty) destructor body,
    //      but still make it virtual.

    virtual void entity_destroyed(Entity entity) = 0;
    //      ^^^^^                                  ^^^
    //      'virtual' means: look up the real implementation at runtime
    //      via the vtable (a hidden table of function pointers).
    //      '= 0' means: this is a PURE VIRTUAL function. IComponentArray
    //      cannot be instantiated directly — it's an abstract interface.
    //      Every concrete subclass (ComponentArray<T>) MUST implement this.
    //
    //      Why only THIS method? Because ComponentManager calls entity_destroyed()
    //      on ALL arrays without knowing T. That's the only cross-type operation.
    //      All other operations (insert, get, remove) are type-specific and
    //      go through the cast in get_array<T>().
};


// ============================================================
//  ComponentArray<T> — PACKED ARRAY STORAGE FOR ONE COMPONENT TYPE
// ============================================================
//
// DESIGN GOALS:
//   1. Store components in a PACKED, CONTIGUOUS array (cache-friendly).
//      No gaps. Systems iterate over thousands of components per frame —
//      contiguous memory means the CPU prefetcher can load ahead efficiently.
//
//   2. O(1) insert, remove, lookup.
//
//   3. When an entity is deleted, don't leave a "hole" in the array.
//      Holes mean the system loop would process garbage data.
//
// HOW IT WORKS — THE PACKED ARRAY + INDEX MAPS:
//
//   data[]            entity_to_index{}   index_to_entity{}
//   [0] Transform(A)  Entity 5 → 0        0 → Entity 5
//   [1] Transform(B)  Entity 3 → 1        1 → Entity 3
//   [2] Transform(C)  Entity 9 → 2        2 → Entity 9
//   size = 3
//
//   Remove Entity 3 (at index 1):
//     1. Swap data[1] with data[last] (data[2]):
//        data[] = [Transform(A), Transform(C), Transform(B)]  ← B is now garbage
//     2. Update maps: Entity 9 now at index 1, index 1 → Entity 9
//     3. Erase Entity 3 from entity_to_index, erase old last_idx from index_to_entity
//     4. size-- → size = 2
//
//   Result: no holes, still packed:
//   [0] Transform(A)   Entity 5 → 0    0 → Entity 5
//   [1] Transform(C)   Entity 9 → 1    1 → Entity 9

template <ComponentType_t T>
// ^^^^^^^^^^^^^^^^^^^^^
// 'ComponentType_t T' — constrained template parameter.
// The concept check happens HERE at template instantiation.
// If T doesn't satisfy ComponentType_t, the compiler errors here
// with a clear constraint violation message.
class ComponentArray : public IComponentArray
//                     ^^^^^^^^^^^^^^^^^^^^^^
//                     Inherits IComponentArray. This is what makes it storable
//                     as shared_ptr<IComponentArray> in the ComponentManager map.
//                     Only one virtual function to implement: entity_destroyed().
// TODO: reserach:
// if the only difference b/w ComponentArrays is how they call delete when entity dies,
// but the remove func looks identical for all component types, if they are simple structs
// where is the implementation differing for each type T of component?
{
  private:
    std::array<T, MAX_ENTITIES> data{};
    //           ^^^^^^^^^^^^^
    //           std::array<T, N> is a fixed-size stack array (like T data[N] in C,
    //           but with .size(), bounds-checked .at(), and iterator support).
    //           Size is MAX_ENTITIES = 1000, so we pre-allocate space for 1000
    //           components of type T. No heap allocation per component.
    //           The {} initializes all elements to their default-constructed state.
    //
    //           MEMORY NOTE: One ComponentArray<Transform> holds 1000 Transforms.
    //           If Transform is 16 bytes, that's 16KB. This is fine — it's allocated
    //           once at startup and lives for the program's lifetime.

    std::flat_map<Entity, size_t> entity_to_index{};
    //            ^^^^^^  ^^^^^^
    //            std::flat_map (C++23) is a sorted map backed by two contiguous
    //            vectors (keys vector + values vector), unlike std::map which is
    //            a red-black tree with heap-allocated nodes scattered in memory.
    //
    //            flat_map is faster for small-to-medium sizes because iteration
    //            is cache-friendly (contiguous memory). Lookup is O(log n) binary
    //            search on the sorted keys vector.
    //
    //            entity_to_index: given an Entity ID, what index in data[] holds
    //            its component? Used in get(), remove(), has().

    std::flat_map<size_t, Entity> index_to_entity{};
    //            ^^^^^^  ^^^^^^
    //            The reverse map: given an array index, which entity owns it?
    //            Used ONLY in remove() — when we swap-delete, we need to know
    //            which entity the "last" element belongs to so we can update
    //            its entry in entity_to_index.

    size_t size = 0;
    //     ^^^^
    //     The count of valid components currently stored.
    //     data[0..size-1] are valid. data[size..MAX_ENTITIES-1] is unused space.
    //     NOT the same as data.size() which is always MAX_ENTITIES.

  public:
    void insert(Entity entity, T component)
    //                         ^^^^^^^^^^^
    //   T is passed BY VALUE here (a copy or move into the parameter).
    //   We then std::move() it into the array — no extra copy.
    //   If the caller passes an rvalue (temporary or std::move()), this
    //   is a move into the parameter — cheap. If they pass an lvalue,
    //   it's copied into the parameter — unavoidable without perfect forwarding.
    {
        assert(!entity_to_index.contains(entity) &&
        //     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        //     The intent is: "fail if we try to add a component the entity already has."
               "Component already added to this entity");

        entity_to_index[entity] = size;
        //              ^^^^^^    ^^^^
        //  Record: this entity's component lives at index 'size' in data[].
        //  [] on flat_map: if the key doesn't exist, inserts a default value first.

        index_to_entity[size]   = entity;
        //  Record the reverse: index 'size' belongs to this entity.

        data[size] = std::move(component);
        //           ^^^^^^^^^^^^^^^^^^^^
        //  std::move() casts 'component' to an rvalue reference, enabling
        //  the move constructor/assignment of T instead of a copy.
        //  For large structs, this avoids copying all their data.
        //  After this, 'component' is in a valid but unspecified state (don't use it).

        size++;
        //  Advance the "end of valid data" marker.
    }

    void remove(Entity entity)
    {
        assert(entity_to_index.contains(entity) &&
               "Removing a component that doesn't exist");

        size_t removed_idx = entity_to_index[entity];
        //     ^^^^^^^^^^^
        //  Which slot in data[] does this entity's component live in?

        size_t last_idx = size - 1;
        //     ^^^^^^^^
        //  The index of the last VALID element. This is what we'll move
        //  into the removed slot to fill the gap.

        data[removed_idx] = std::move(data[last_idx]);
        //  SWAP-DELETE: Move the last element into the removed slot.
        //  This fills the gap without any shifting. O(1) operation.
        //  data[last_idx] is now in a moved-from state (garbage), but
        //  we're about to decrement size, so it'll be in the "unused" zone.

        Entity last_entity = index_to_entity[last_idx];
        //     ^^^^^^^^^^^
        //  Who owned the element we just moved? We need to update their
        //  entry in entity_to_index to point to the new slot (removed_idx).

        entity_to_index[last_entity] = removed_idx;
        //  The last entity's component is now at removed_idx. Update its record.

        index_to_entity[removed_idx] = last_entity;
        //  Slot removed_idx now holds last_entity's component. Update reverse map.

        entity_to_index.erase(entity);
        //  Remove the dead entity's entry — it no longer has this component.

        index_to_entity.erase(last_idx);
        //  The old last slot is now beyond 'size' (unused). Remove its record.
        //  (If we left it, it would be a stale entry pointing to moved-from data.)

        size--;
        //  The valid range shrinks by one.
    }

    [[nodiscard]] auto get(Entity entity) -> T&
    //[[nodiscard]]: compiler warns if you call this and throw away the return value.
    //               Catches bugs like: transform.get(entity); // forgot to use it
    //
    // -> T& : returns a REFERENCE to the component inside the array.
    //         This means the caller can modify it in-place:
    //           auto& t = array.get(entity);
    //           t.x = 100; // modifies the actual stored component
    //         If we returned T by value, the caller would get a copy
    //         and modifying it wouldn't affect the stored data.
    {
        assert(entity_to_index.contains(entity) &&
               "Entity does not have this component");

        return data[entity_to_index[entity]];
        //          ^^^^^^^^^^^^^^^^^^^^^^
        //  Look up the entity's index, then return a reference to that slot.
        //  No copying. The reference is only valid as long as the array lives
        //  AND as long as no insert/remove invalidates indices (it doesn't —
        //  insert grows size but doesn't move existing elements, remove uses
        //  the swap-delete which keeps existing indices valid for other entities).
    }

    [[nodiscard]] auto has(Entity entity) const -> bool
    //                                    ^^^^^
    //  'const' after the parameter list means: this method does NOT modify
    //  any member variables. The compiler enforces this. This lets you call
    //  has() on a const ComponentArray& without issues.
    {
        return entity_to_index.contains(entity);
        //     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        //  std::flat_map::contains(key) returns true if the key exists. C++20.
        //  Equivalent to: entity_to_index.find(entity) != entity_to_index.end()
        //  but much cleaner to read.
    }

    void entity_destroyed(Entity entity) override
    //                                   ^^^^^^^^
    //  'override': tells the compiler this is intentionally overriding a
    //  virtual function from IComponentArray. If you typo the signature,
    //  the compiler errors ("doesn't override anything") instead of silently
    //  creating a new non-virtual method. Always use override when you mean it.
    {
        if (entity_to_index.contains(entity)) {
            remove(entity);
            //  If the entity has a component of type T, remove it.
            //  If it doesn't have one, do nothing — that's fine.
            //  This is called for EVERY ComponentArray when any entity dies,
            //  so most calls will hit the early return path.
        }
    }

    auto all() -> std::span<T>
    //           ^^^^^^^^^^^^^
    //  std::span<T> is a NON-OWNING view into contiguous memory.
    //  Like a pointer + size, but safer. It doesn't own the data —
    //  the underlying array does.
    //
    //  This lets systems iterate ALL valid components like:
    //    for (auto& comp : component_array.all()) { ... }
    //  without needing to know about the internal index maps.
    //
    //  We return only data[0..size-1] — the valid region.
    //  data[size..MAX_ENTITIES-1] is unused garbage.
    {
        return { data.data(), size };
        //       ^^^^^^^^^^   ^^^^
        //  data.data() returns a raw pointer to the first element.
        //  'size' tells the span how many elements are valid.
        //  Together: "a view of 'size' elements starting at data[0]."
    }
};


// ============================================================
//  EntityManager
// ============================================================
//
// RESPONSIBILITY: hand out unique Entity IDs and track which
// components each entity has (via its Signature).
//
// DESIGN — THE FREE LIST (QUEUE):
//   Instead of a global counter that always goes up (eventually overflows
//   and wastes IDs), we pre-fill a queue with all valid IDs [0..MAX_ENTITIES-1].
//   When an entity is created, pop an ID from the front.
//   When an entity is destroyed, push its ID back for reuse.
//
//   This means entity IDs are RECYCLED. After creating and destroying
//   1000 entities, the IDs are all available again.
//
//   Queue (FIFO — first in, first out) means IDs are reused in order,
//   which avoids the confusing case where a newly created entity has
//   the same ID as a recently destroyed one (other systems may hold
//   stale copies of the old ID). By cycling through IDs slowly, you
//   reduce the chance of stale-ID confusion.

class EntityManager
{
  private:
    std::queue<Entity> available_entities;
    //  A FIFO queue of entity IDs available for use.
    //  Starts full (all IDs available), empties as entities are created,
    //  refills as entities are destroyed.

    std::array<Signature, MAX_ENTITIES> signatures{};
    //  One Signature per possible entity slot.
    //  signatures[entity_id] tells us which components entity_id has.
    //  {} zero-initializes all bitsets (all bits 0 = no components).
    //
    //  Why a fixed array indexed by entity ID?
    //  Because entity IDs are integers in [0, MAX_ENTITIES), a direct
    //  array lookup is O(1) with zero overhead — no hashing, no tree traversal.
    //  This is the "slot array" pattern, very common in ECS implementations.

    uint32_t living_count = 0;
    //  How many entities are currently alive. Used to enforce MAX_ENTITIES limit
    //  and expose entity_count() to external code.

  public:
    EntityManager()
    {
        for (Entity e = 0; e < MAX_ENTITIES; e++) {
            available_entities.push(e);
            //  Pre-fill the queue with IDs 0, 1, 2, ..., MAX_ENTITIES-1.
            //  After this loop, the queue has MAX_ENTITIES items.
        }
    }

    [[nodiscard]] auto create() -> Entity
    {
        assert(living_count < MAX_ENTITIES && "Too many entities!");

        Entity id = available_entities.front();
        //          ^^^^^^^^^^^^^^^^^^^^^^
        //  Peek at the front of the queue — the next available ID.

        available_entities.pop();
        //  Remove it from the queue. This ID is now "in use."

        living_count++;
        return id;
    }

    void destroy(Entity entity)
    {
        assert(entity < MAX_ENTITIES && "Entity out of range");

        signatures[entity].reset();
        //  Clear ALL bits in this entity's signature.
        //  WHY: if this entity ID is reused for a new entity, the new
        //  entity must start with no components. Without this reset,
        //  the new entity would incorrectly "inherit" the old one's
        //  component flags, and systems would try to process it
        //  before it actually has those components.

        available_entities.push(entity);
        //  Return the ID to the free pool. Someone else can use it.

        living_count--;
    }

    void set_signature(Entity entity, Signature sig)
    {
        assert(entity < MAX_ENTITIES && "Entity out of range");
        signatures[entity] = sig;
        //  Called by World::add_component() and World::remove_component()
        //  after they modify the entity's component set.
    }

    [[nodiscard]] auto get_signature(Entity entity) -> Signature
    {
        assert(entity < MAX_ENTITIES && "Entity out of range");
        return signatures[entity];
        //  Returns a COPY of the Signature (bitset is small — 64 bits — cheap to copy).
        //  Used by World to get the current signature before modifying it.
    }

    [[nodiscard]] auto count() const -> uint32_t { return living_count; }
};


// ============================================================
//  ComponentManager
// ============================================================
//
// RESPONSIBILITY: own all the ComponentArrays and provide a
// type-safe interface to add/remove/get components.
//
// THE TWO MAPS:
//
//   component_types:  type_index → ComponentType (uint8_t)
//     Answers: "what bit number does Transform use in a Signature?"
//     Used by World::add_component() to know which bit to set.
//
//   component_arrays: type_index → shared_ptr<IComponentArray>
//     Answers: "where is the storage for Transform components?"
//     Stores the actual ComponentArray<T> behind the type-erased interface.
//
//   WHY type_index as the key?
//     std::type_index wraps std::type_info (returned by typeid(T)) into
//     a hashable/comparable value. It's the standard way to use types
//     as map keys. typeid(Transform) != typeid(Velocity), so each
//     component type gets its own entry.
//
//   WHY NOT a plain array indexed by ComponentType (0,1,2...)?
//     We COULD: component_arrays[component_type_id<T>()] = ...
//     That would be O(1) lookup vs O(log n) for flat_map.
//     The type_index map is slightly cleaner (no chicken-and-egg ordering
//     concern between type ID assignment and array creation), and
//     with only 64 component types, O(log 64) = ~6 comparisons is negligible.
//     This is a valid TODO in the original code.

class ComponentManager
{
  private:
    std::flat_map<std::type_index, ComponentType>                     component_types{};
    std::flat_map<std::type_index, std::shared_ptr<IComponentArray>>  component_arrays{};
    //                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //  std::shared_ptr: a reference-counted smart pointer.
    //  Multiple shared_ptrs can point to the same object; it's deleted when
    //  the last one is destroyed.
    //
    //  WHY shared_ptr here (instead of unique_ptr)?
    //  get_array<T>() returns a shared_ptr<ComponentArray<T>>.
    //  If we used unique_ptr, get_array() couldn't return a shared_ptr
    //  without transferring ownership. shared_ptr lets the map retain
    //  ownership while get_array() returns a counted reference.
    //  Functionally, unique_ptr would also work if get_array returned a raw pointer.

    template<ComponentType_t T>
    auto get_array() -> std::shared_ptr<ComponentArray<T>>
    //   ^^^^^^^^^
    //  PRIVATE helper. Called internally by add_component, remove_component, etc.
    //
    //  This is the CAST BACK from the type-erased interface.
    //  The map stores shared_ptr<IComponentArray> (base class).
    //  We need shared_ptr<ComponentArray<T>> (derived class).
    //
    //  std::static_pointer_cast<ComponentArray<T>>:
    //    Like static_cast, but for shared_ptr. Converts the pointer's
    //    type while sharing the reference count.
    //    This is SAFE because we always register ComponentArray<T> under
    //    type_index(typeid(T)), so we know the cast is correct.
    //    (dynamic_cast would be safer but costs a runtime RTTI lookup —
    //    not needed when we control the registration.)
    {
        auto key = std::type_index(typeid(T));
        //         ^^^^^^^^^^^^^^^^^^^^^^^^
        //  typeid(T) returns a std::type_info& for type T.
        //  std::type_index wraps it to make it usable as a map key
        //  (type_info itself doesn't support < or ==).

        assert(component_arrays.contains(key) && "Component not registered");

        return std::static_pointer_cast<ComponentArray<T>>(component_arrays[key]);
        //     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        //  Convert shared_ptr<IComponentArray> to shared_ptr<ComponentArray<T>>.
        //  Both point to the same heap object. Reference count is shared.
    }

  public:
    template<ComponentType_t T>
    void register_component()
    //  Called once at startup for each component type.
    //  Creates the ComponentArray<T> and records the type's ID.
    //  MUST be called before any add_component<T>() calls.
    {
        auto key = std::type_index(typeid(T));
        assert(!component_arrays.contains(key) && "Already registered");

        component_types[key]  = component_type_id<T>();
        //  Assign and record the unique integer ID for this component type.
        //  component_type_id<T>() returns the same value every time for the same T.

        component_arrays[key] = std::make_shared<ComponentArray<T>>();
        //                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        //  Allocate a new ComponentArray<T> on the heap, wrapped in a shared_ptr.
        //  std::make_shared allocates the control block and the object together
        //  (one allocation instead of two), which is more efficient than
        //  shared_ptr<ComponentArray<T>>(new ComponentArray<T>()).
    }

    template<ComponentType_t T>
    [[nodiscard]] auto get_component_type() -> ComponentType
    //  Returns the integer ID for component type T.
    //  Used by World to know which bit to set/reset in a Signature.
    {
        auto key = std::type_index(typeid(T));
        assert(component_types.contains(key) && "Component not registered");
        return component_types[key];
    }

    template<ComponentType_t T>
    void add_component(Entity entity, T component)
    {
        get_array<T>()->insert(entity, std::move(component));
        //  -> on a shared_ptr dereferences it (like *ptr).
        //  So this calls ComponentArray<T>::insert().
        //  std::move(component) avoids a redundant copy into insert().
    }

    template<ComponentType_t T>
    void remove_component(Entity entity)
    {
        get_array<T>()->remove(entity);
    }

    template<ComponentType_t T>
    [[nodiscard]] auto get_component(Entity entity)
    //  Note: no explicit return type. 'auto' deduces it from the return statement.
    //  It deduces to T& (a reference), which is what ComponentArray<T>::get() returns.
    {
        return get_array<T>()->get(entity);
    }

    template<ComponentType_t T>
    auto has_component(Entity entity) -> bool
    {
        return get_array<T>()->has(entity);
    }

    void entity_destroyed(Entity entity)
    //  Called by World::destroy_entity(). Notifies EVERY registered ComponentArray
    //  that this entity is gone. Each array removes the component if it has one.
    //  This is the "broadcast to all arrays" pattern — O(num_component_types),
    //  which is at most 64 iterations. Fast enough.
    {
        for (const auto& [key, array] : component_arrays) {
            //               ^^^  ^^^^^
            //  Structured binding (C++17): decomposes the flat_map's key-value
            //  pair into named variables. 'key' is the std::type_index,
            //  'array' is the shared_ptr<IComponentArray>.
            //
            //  'const auto&': we're not modifying the map entries, just reading.
            //  The & avoids copying the shared_ptr (which would increment
            //  the reference count unnecessarily).

            array->entity_destroyed(entity);
            //  Calls the VIRTUAL entity_destroyed() on each array.
            //  The vtable dispatches to the correct ComponentArray<T>::entity_destroyed().
            //  This is the one place where virtual dispatch in the ECS is justified —
            //  it's called once per entity death, not once per frame per entity.
        }
    }
};


// ============================================================
//  System — BASE CLASS FOR ALL GAME SYSTEMS
// ============================================================
//
// A system is just a class that:
//   1. Declares which components it needs (get_signature)
//   2. Processes entities that have those components (update)
//
// The 'entities' vector is maintained by SystemManager.
// The system itself NEVER modifies 'entities' — it just reads it.
// Systems call back into World to read/write component data.
//
// WHY virtual here?
//   SystemManager stores a flat_map<type_index, unique_ptr<System>>.
//   Like IComponentArray, it needs to call update() on any System
//   without knowing the concrete type. Virtual dispatch is the
//   right tool for this "unknown concrete type, known interface" problem.
//
//   The performance cost is ONE vtable lookup per system per frame
//   (for update()), not per entity. With maybe 10-20 systems, this
//   is completely negligible.

class System
{
  public:
    virtual ~System() = default;    // must be virtual (deleted via base ptr in SystemManager)

    virtual void update(float dt) = 0;
    //  Pure virtual: subclasses MUST implement their game logic here.
    //  'dt' is delta time — seconds since last frame. Used to make
    //  movement/timers frame-rate independent (multiply velocities by dt).

    [[nodiscard]] virtual auto get_signature() const -> Signature = 0;
    //  Pure virtual: subclasses declare which components they require.
    //  Called ONCE during system registration (not every frame).
    //  Result is used by SystemManager::entity_signature_changed() to
    //  decide which entities belong to this system's 'entities' list.
    //
    //  This method is 'const' because it has no side effects —
    //  it just constructs and returns a Signature.

    std::vector<Entity> entities;
    //  PUBLIC because SystemManager needs to modify it.
    //  The system itself only READS this (iterates it in update()).
    //
    //  WHY vector and not a set?
    //  - Iteration (reading in update()) is the dominant operation.
    //    std::vector is cache-friendly for iteration.
    //  - Insertion/deletion happens rarely (only when entity components change).
    //  - With MAX_ENTITIES=1000, linear erase is fast enough.
    //  - A flat_set would give O(log n) contains() but worse cache behavior.
    //    The TODO in the original code noting flat_set is a valid consideration.
};


// ============================================================
//  SystemManager
// ============================================================
//
// RESPONSIBILITY:
//   1. Store all registered systems.
//   2. When an entity's components change, update every system's
//      entity list (add or remove the entity from the system's set).
//   3. Call update() on all systems every frame.
//
// THE SIGNATURE MATCHING ALGORITHM (entity_signature_changed):
//   Every time an entity gains or loses a component, World calls
//   entity_signature_changed() with the entity's NEW full signature.
//
//   For each system:
//     Check if (entity_signature & system_signature) == system_signature
//     i.e., "does the entity have AT LEAST the bits the system needs?"
//
//   Example:
//     PhysicsSystem needs bits {0,1} (Transform=0, Velocity=1)
//     Entity 5 has bits {0,1,3} (Transform, Velocity, Health)
//     Match check: 0b1011 & 0b0011 == 0b0011 → true → entity 5 is in PhysicsSystem
//
//     Then entity 5 loses Velocity (bit 1 cleared):
//     Entity 5 now has bits {0,3}: 0b1001
//     Match check: 0b1001 & 0b0011 == 0b0001 ≠ 0b0011 → false → removed from PhysicsSystem

class SystemManager
{
  private:
    std::flat_map<std::type_index, std::unique_ptr<System>> systems{};
    //                             ^^^^^^^^^^^^^^^^^^^^^^
    //  unique_ptr: exclusive ownership. SystemManager IS the owner of all systems.
    //  When SystemManager is destroyed, all systems are automatically deleted.
    //  (Unlike ComponentManager's shared_ptr, there's no reason to share system ownership.)

  public:
    template<typename S, typename... Args>
    //                   ^^^^^^^^^^^^^^
    //  Variadic template: 'Args...' is zero or more types.
    //  This lets register_system forward arbitrary constructor arguments to S.
    //  Example: register_system<PhysicsSystem>(world) passes 'world' to PhysicsSystem's ctor.
    //  Example: register_system<AISystem>(world, navmesh, 5.0f) passes three args.

    requires std::derived_from<S, System>
    //  Concept constraint: S must inherit from System.
    //  Without this, you could call register_system<int>(42) and get
    //  a confusing error deep in make_unique. With it, you get a clear
    //  "constraint not satisfied" at the call site.

    auto register_system(Args&&... args) -> S*
    //                   ^^^^  ^^^
    //  'Args&&...' — FORWARDING REFERENCES (also called universal references).
    //  When '&&' appears in a template parameter context, it doesn't always
    //  mean rvalue reference. It means "preserve the value category":
    //    - If you pass an lvalue, Args deduces to T&, Args&& collapses to T&
    //    - If you pass an rvalue, Args deduces to T, Args&& is T&&
    //  The '...' unpacks the variadic pack.
    //
    //  -> S* : returns a RAW POINTER to the created system.
    //  The system is OWNED by the unique_ptr in the map.
    //  The caller gets a non-owning raw pointer for convenience
    //  (so they can call system-specific methods if needed).
    //  The raw pointer is valid as long as the SystemManager lives.
    {
        auto key = std::type_index(typeid(S));
        assert(!systems.contains(key) && "System already registered");

        auto system = std::make_unique<S>(std::forward<Args>(args)...);
        //                               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        //  std::forward<Args>(args)... — PERFECT FORWARDING.
        //  This passes each argument to S's constructor with its original
        //  value category preserved (lvalues as lvalues, rvalues as rvalues).
        //
        //  WITHOUT std::forward:
        //    auto system = std::make_unique<S>(args...);
        //    Every arg would be passed as an lvalue (because named variables
        //    inside a function are lvalues). If S's constructor expected an
        //    rvalue (movable resource), it would get a copy instead of a move.
        //
        //  WITH std::forward:
        //    If the original caller passed std::move(x), S's constructor
        //    receives it as an rvalue and can move from it — no extra copy.
        //
        //  The '...' unpacks the args pack, passing all of them.

        S* ptr = system.get();
        //  .get() returns the raw pointer that unique_ptr wraps.
        //  We save this BEFORE moving the unique_ptr into the map,
        //  because after the move, 'system' is null.

        systems[key] = std::move(system);
        //  Transfer ownership of the unique_ptr into the map.
        //  After this, 'system' is a null unique_ptr.
        //  'ptr' still points to the valid object (now owned by the map).

        return ptr;
    }

    void entity_destroyed(Entity entity)
    {
        for (const auto& [key, system] : systems) {
            auto& ents = system->entities;
            ents.erase(std::remove(ents.begin(), ents.end(), entity), ents.end());
            //  ERASE-REMOVE IDIOM: the idiomatic C++ way to remove all elements
            //  matching a value from a vector.
            //
            //  std::remove(begin, end, value):
            //    Moves all NON-matching elements to the front of the range.
            //    Returns an iterator to the new logical end.
            //    Does NOT actually shrink the vector — elements past the
            //    new end are in a valid-but-unspecified state.
            //
            //  .erase(new_end, old_end):
            //    Actually removes the "tail" elements, shrinking the vector.
            //
            //  Example: [A, B, C, B, D], remove B
            //    After std::remove: [A, C, D, ?, ?], returns iterator to 4th element
            //    After .erase:      [A, C, D]
            //
            //  In C++20 you could write: std::erase(ents, entity) — cleaner.
        }
    }

    void entity_signature_changed(Entity entity, Signature ent_sig)
    //                                           ^^^^^^^^^^^^^^^^^
    //  ent_sig is the entity's COMPLETE, UP-TO-DATE signature AFTER
    //  the component add/remove. Passed by value (Signature is just 64 bits).
    {
        for (const auto& [key, system] : systems) {
            Signature sys_sig   = system->get_signature();
            //  Ask this system what components it requires.
            //  Called every time any entity changes — a minor inefficiency.
            //  Could be cached per system, but with 64 bits it's trivial.

            auto& ents          = system->entities;
            bool  has_entity    = std::ranges::contains(ents, entity);
            //    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            //    std::ranges::contains (C++23): returns true if 'entity'
            //    is anywhere in 'ents'. O(n) linear scan.
            //    Equivalent to: std::find(ents.begin(), ents.end(), entity) != ents.end()

            bool  matches       = (ent_sig & sys_sig) == sys_sig;
            //    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            //    THE CORE MATCHING CHECK:
            //    ent_sig & sys_sig: bitwise AND — keep only bits that are set in BOTH.
            //    == sys_sig: is the result identical to what the system needs?
            //    True only if every bit in sys_sig is also set in ent_sig.
            //    i.e., "does the entity have all the components this system needs?"

            if (matches && !has_entity) {
                ents.push_back(entity);
                //  Entity now qualifies for this system but wasn't in the list → add it.
            } else if (!matches && has_entity) {
                ents.erase(std::remove(ents.begin(), ents.end(), entity), ents.end());
                //  Entity no longer qualifies (lost a required component) → remove it.
            }
            //  If (matches && has_entity): already in the list → do nothing.
            //  If (!matches && !has_entity): never qualified → do nothing.
        }
    }

    void update_all(float dt)
    {
        for (const auto& [key, system] : systems) {
            system->update(dt);
            //  Virtual dispatch: calls the correct update() for each system type.
            //  Systems run in flat_map ORDER (sorted by type_index, which is
            //  implementation-defined). If order matters between systems, you'd
            //  need to add explicit priority or run them manually.
        }
    }
};


// ============================================================
//  World — THE MAIN ECS FACADE
// ============================================================
//
// WHY a facade?
//   EntityManager, ComponentManager, SystemManager each handle one
//   concern. But they need to COORDINATE:
//     - Adding a component requires: ComponentManager to store it AND
//       EntityManager to update the signature AND
//       SystemManager to update entity lists.
//   The World ties these three operations together in the right order
//   behind a single clean API. Game code only touches World.
//
// OWNERSHIP:
//   World owns all three managers via unique_ptr.
//   When World is destroyed, all managers (and everything they own)
//   are automatically cleaned up. No manual delete anywhere.

class World
{
  private:
    std::unique_ptr<EntityManager>    entity_manager;
    std::unique_ptr<ComponentManager> component_manager;
    std::unique_ptr<SystemManager>    system_manager;
    //  unique_ptr means World has EXCLUSIVE ownership of these managers.
    //  They live on the heap (so World can be moved), but their lifetimes
    //  are tied to World's lifetime.
    //
    //  WHY heap (unique_ptr) instead of direct members?
    //  Each manager contains large arrays (EntityManager::signatures is
    //  MAX_ENTITIES * 64 bits = 8KB). Having them as direct members would
    //  put all that on the stack if World is a local variable, risking
    //  stack overflow. unique_ptr keeps them on the heap where large
    //  allocations belong.

  public:
    World()
    {
        entity_manager    = std::make_unique<EntityManager>();
        component_manager = std::make_unique<ComponentManager>();
        system_manager    = std::make_unique<SystemManager>();
        //  std::make_unique<T>() allocates T on the heap and wraps it in unique_ptr.
        //  Prefer make_unique over 'new' — it's exception-safe and cleaner.
    }

    // ---- Entity API ----

    [[nodiscard]] auto create_entity() -> Entity
    {
        return entity_manager->create();
        //  Delegates to EntityManager. World just provides the unified entry point.
    }

    void destroy_entity(Entity entity)
    {
        entity_manager->destroy(entity);
        //  1. Clears the entity's signature, returns ID to free queue.

        component_manager->entity_destroyed(entity);
        //  2. Tells every ComponentArray to remove this entity's data.
        //     MUST happen AFTER entity_manager->destroy() — though in practice
        //     the order between these two doesn't matter since they're independent.

        system_manager->entity_destroyed(entity);
        //  3. Removes this entity from every system's entity list.
        //
        //  ORDER MATTERS between 2 and 3 only if a system's update() could
        //  somehow run between these calls — which it can't since we're single-threaded.
    }

    // ---- Component API ----

    template<ComponentType_t T>
    void register_component() { component_manager->register_component<T>(); }
    //  Thin passthrough. Game code calls world.register_component<Transform>()
    //  rather than directly accessing component_manager.

    template<ComponentType_t T>
    void add_component(Entity entity, T component)
    //  NOTE: World::add_component is NOT shown in the original file you shared.
    //  It should mirror remove_component but call insert and set the bit.
    //  The pattern is:
    //    1. component_manager->add_component<T>(entity, move(component))
    //    2. Get signature, SET the T bit, set_signature, entity_signature_changed
    {
        component_manager->add_component<T>(entity, std::move(component));

        auto sig = entity_manager->get_signature(entity);
        sig.set(component_manager->get_component_type<T>());
        //  sig.set(bit): sets the bit at position 'bit' to 1.
        //  component_manager->get_component_type<T>() returns the integer (0, 1, 2...)
        //  that was assigned to T during register_component<T>().

        entity_manager->set_signature(entity, sig);
        system_manager->entity_signature_changed(entity, sig);
        //  Notify all systems that this entity's signature changed.
        //  Systems will add this entity to their lists if it now matches them.
    }

    template<ComponentType_t T>
    void remove_component(Entity entity)
    {
        component_manager->remove_component<T>(entity);
        //  1. Remove the component data from the ComponentArray.

        auto sig = entity_manager->get_signature(entity);
        //  2. Get a COPY of the current signature.

        sig.reset(component_manager->get_component_type<T>());
        //  3. Clear the T bit (sig.reset(bit) sets bit to 0).

        entity_manager->set_signature(entity, sig);
        //  4. Store the updated signature back.

        system_manager->entity_signature_changed(entity, sig);
        //  5. Notify systems — they may remove this entity if it no longer
        //     satisfies their requirements.
        //
        //  STEP ORDER IS CRITICAL:
        //  Component data is removed FIRST (step 1), then the signature
        //  is updated (steps 2-4), then systems are notified (step 5).
        //  If systems were notified first, a system could try to process
        //  the entity with the old (stale) signature, but the data is gone.
    }

    template<ComponentType_t T>
    [[nodiscard]] auto get_component(Entity entity) -> T&
    {
        return component_manager->get_component<T>(entity);
    }

    template<ComponentType_t T>
    [[nodiscard]] auto has_component(Entity entity) -> bool
    {
        return component_manager->has_component<T>(entity);
    }

    template<ComponentType_t T>
    [[nodiscard]] auto get_component_type() -> ComponentType
    {
        return component_manager->get_component_type<T>();
        //  Used by systems in their get_signature() implementations.
        //  e.g.: sig.set(world.get_component_type<Transform>())
    }

    // ---- System API ----

    template<typename S, typename... Args>
    requires std::derived_from<S, System>
    auto register_system(Args&&... args) -> S*
    {
        return system_manager->register_system<S>(std::forward<Args>(args)...);
        //  Perfect forwarding again — passes constructor args through unchanged.
    }

    void update(float dt)
    {
        system_manager->update_all(dt);
        //  Calls update(dt) on every registered system in order.
        //  This is the single call you make in your game loop each frame.
    }

    [[nodiscard]] auto entity_count() const -> uint32_t
    {
        return entity_manager->count();
    }
};


// ============================================================
//  HOW EVERYTHING CONNECTS — THE FULL LIFECYCLE
// ============================================================
//
//  STARTUP (main.cpp):
//    World world;
//    world.register_component<Transform>();    // assigns bit 0
//    world.register_component<Velocity>();     // assigns bit 1
//    world.register_component<Health>();       // assigns bit 2
//    world.register_system<PhysicsSystem>(world); // PhysicsSystem::get_signature()
//                                                  // returns bits {0,1}
//
//  CREATING AN ENTITY:
//    Entity e = world.create_entity();         // gets ID from free queue, e.g. ID=5
//    world.add_component(e, Transform{10,20}); // stores in ComponentArray<Transform>[slot]
//                                              // sets bit 0 in signatures[5]
//                                              // notifies systems: entity 5 has {0}
//                                              // PhysicsSystem needs {0,1} → 5 not added yet
//    world.add_component(e, Velocity{1,0});    // stores in ComponentArray<Velocity>[slot]
//                                              // sets bit 1 in signatures[5]
//                                              // notifies systems: entity 5 has {0,1}
//                                              // PhysicsSystem needs {0,1} → entity 5 ADDED ✓
//
//  EACH FRAME (game loop):
//    world.update(dt);                         // calls PhysicsSystem::update(dt)
//                                              // PhysicsSystem iterates its entities list
//                                              // for each entity: get Transform, get Velocity
//                                              // transform.x += velocity.dx * dt
//
//  DESTROYING AN ENTITY:
//    world.destroy_entity(e);                  // ComponentManager removes all components
//                                              // SystemManager removes from all entity lists
//                                              // EntityManager returns ID 5 to free queue
