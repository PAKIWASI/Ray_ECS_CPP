#pragma once

// SystemBase<Derived, CMgr, ComponentTypes...>
//
// CRTP base for all systems. No virtual functions, no vtable, no ISystem.
//
// Template parameters:
//   Derived          — the concrete system class (CRTP)
//   CMgr             — the concrete ComponentManagerImpl instantiation.
//                      Carries ListType so we can call comp_type_index without
//                      a separate CList parameter.
//   ComponentTypes...— the components this system requires (used to compute
//                      the system's signature at compile time)
//
// Why template on CMgr instead of the ComponentList directly?
//   SystemBase needs both: the manager (to store a ref and call get_arr<T>())
//   and the list (to compute the signature via comp_type_index).
//   CMgr::ListType gives us the list for free — one template param, not two.
//
// make_signature<CList, Ts...>() is a free function that takes the list
// explicitly, using comp_type_index<T, CList>::value — no global component_id.

#include "common.hpp"
#include "component_manager.hpp"   // ComponentArray, ComponentManagerImpl, make_component_manager

#include <array>
#include <cassert>
#include <vector>


// make_signature — compute a Signature bitset at compile time
// =============================================================================
// Takes the ComponentList type explicitly so it works without a global alias.
// Called inside SystemBase with CMgr::ListType.

template <typename CList, typename... Ts>
consteval auto make_signature() -> Signature
{
    Signature sig{};
    // For each component type Ts, set the bit at its position in CList
    ((sig.set(comp_type_index<Ts, CList>::value)), ...);
    return sig;
}


// SystemBase<Derived, CMgr, ComponentTypes...>
// =============================================================================

template <typename Derived, typename CMgr, typename... ComponentTypes>
class SystemBase
{
  protected:
    // Sparse set — O(1) add, O(1) remove, O(n) sequential iteration
    // -------------------------------------------------------------------------
    // dense[i]   = the i-th active entity (contiguous, iteration is sequential)
    // sparse[e]  = index of entity e in dense (INVALID = not in this system)
    //
    // Invariant: sparse[dense[i]] == i for all i < dense.size()
    //            dense[sparse[e]] == e for all e with sparse[e] != INVALID
    std::vector<Entity>           dense;
    std::array<u32, MAX_ENTITIES> sparse{};

    // Single runtime reference — the only non-constexpr data besides the entity set
    CMgr& comp_manager;

    // Signature computed from ComponentTypes at compile time via CMgr::ListType.
    // make_signature resolves comp_type_index<T, ListType>::value for each T.
    static constexpr Signature signature =
        make_signature<typename CMgr::ListType, ComponentTypes...>();

  public:
    auto operator=(const SystemBase&) -> SystemBase& = delete;
    auto operator=(SystemBase&&)      -> SystemBase& = delete;

    explicit SystemBase(CMgr& cm) : comp_manager(cm)
    {
        dense.reserve(PRE_INIT_SIZE);
        sparse.fill(INVALID);
    }

    // Move constructor — needed because SystemManagerImpl stores systems in a
    // tuple and constructs them with Systems(cm)... which may invoke moves
    SystemBase(SystemBase&& other) noexcept
        : comp_manager(other.comp_manager)
        , dense(std::move(other.dense))
        , sparse(other.sparse)
    {}

    // Copy intentionally deleted — systems own their entity sets
    SystemBase(const SystemBase&) = delete;

    ~SystemBase() = default;


    // Entity management
    // -------------------------------------------------------------------------

    void add_entity(Entity e)
    {
        assert(e < MAX_ENTITIES  && "Entity out of range");
        assert(!has_entity(e)    && "Entity already in system");

        sparse[e] = static_cast<u32>(dense.size());
        dense.emplace_back(e);
    }

    void remove_entity(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        assert(has_entity(e)    && "Entity not in system");

        u32    idx         = sparse[e];
        u32    last_idx    = static_cast<u32>(dense.size()) - 1;
        Entity last_entity = dense[last_idx];

        // Swap with last, update sparse for the moved entity
        dense[idx]          = last_entity;
        sparse[last_entity] = idx;
        dense.pop_back();

        // Clear the removed entity's sparse slot
        sparse[e] = INVALID;
    }

    [[nodiscard]] auto has_entity(Entity e) const -> bool
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        // Triple check: slot not INVALID, index in bounds, cross-reference valid
        // Guards against stale indices after swap-and-pop
        return sparse[e] != INVALID
            && sparse[e] < static_cast<u32>(dense.size())
            && dense[sparse[e]] == e;
    }

    [[nodiscard]] static constexpr auto get_signature() -> Signature
    {
        return signature;
    }

    [[nodiscard]] auto get_entity_count() const -> u32
    {
        return static_cast<u32>(dense.size());
    }

    // CRTP dispatch — SystemManagerImpl calls sys.update(dt) which calls
    // Derived::update_impl(dt) directly. No virtual dispatch.
    void update(float dt)
    {
        static_cast<Derived*>(this)->update_impl(dt);
    }

  protected:
    // Component access helpers
    // -------------------------------------------------------------------------
    // get_component<T>(e) goes: comp_manager.get_arr<T>().get_data(e)
    // This is two direct memory accesses — get_arr<T>() is a compile-time
    // std::get<> on the tuple, get_data(e) indexes into the dense vector.

    template <typename T>
    [[nodiscard]] auto get_component(Entity e) -> T&
    {
        return comp_manager.template get_arr<T>().get_data(e);
    }

    template <typename T>
    [[nodiscard]] auto get_component(Entity e) const -> const T&
    {
        return comp_manager.template get_arr<T>().get_data(e);
    }

    template <typename T>
    [[nodiscard]] auto has_component(Entity e) const -> bool
    {
        return comp_manager.template get_arr<T>().has_data(e);
    }
};
