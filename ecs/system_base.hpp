#pragma once

#include "common.hpp"
#include "component_manager.hpp"
#include <bitset>
#include <cassert>
#include <vector>


// what counts as a system
// each system MUST have:
//  - an update(dt) method
//  - an array of entities it operates on
//  - a signature computed at compile time
//  - easy access to the components it needs
// If w can enforce these requirements at compile time, then we dont need
// an oop hierarchy and inheritance
template <typename T>
concept SystemType_t = requires(T& system, const T& csystem, float dt) {
    // 1. Must have update_impl(dt)
    { system.update_impl(dt) } -> std::same_as<void>;

    // 2. Must expose a static, compile-time signature
    { T::get_signature() } -> std::same_as<Signature>;

    // 3. Must expose its entity set for iteration
    //    (matches SystemBase's add_entity/remove_entity/has_entity contract,
    //     not a raw container type, since the dense/sparse layout is an
    //     implementation detail systems shouldn't be required to expose)
    { system.add_entity(Entity{}) } -> std::same_as<void>;
    { system.remove_entity(Entity{}) } -> std::same_as<void>;
    { csystem.has_entity(Entity{}) } -> std::same_as<bool>;

    // 4. Must be constructible from a ComponentManager (this is how systems
    //    get "easy access to the components it needs" — via comp_manager,
    //    not via a default constructor, which is deliberately NOT required)
    requires std::constructible_from<T, const ComponentManager&>;
};

// Helper to compute system signature from component list
template <typename... Ts>
consteval auto make_signature() -> Signature
{
    Signature sig;
    ((sig.set(component_id<Ts>)), ...);
    return sig;
}


// CRTP base - no virtual functions, no vtable, no runtime references
template <typename Derived, typename... ComponentTypes>
class SystemBase
{
  protected:
    // Sparse set implementation
    std::vector<Entity>           dense;     // Contiguous entities for iteration
    std::array<u32, MAX_ENTITIES> sparse{};  // Maps entity -> index in dense
    u32                           count = 0; // Number of entities in system

    // Pointer to ComponentManager - single runtime indirection
    // This is the ONLY runtime data we need
    const ComponentManager& comp_manager;

    // Computed signature at compile time
    static constexpr Signature signature = make_signature<ComponentTypes...>();

  public:
    SystemBase(const SystemBase&)                    = delete;
    SystemBase(SystemBase&&)                         = delete;
    auto operator=(const SystemBase&) -> SystemBase& = delete;
    auto operator=(SystemBase&&) -> SystemBase&      = delete;

    // Constructor with ComponentManager
    SystemBase(const ComponentManager& cm) : comp_manager(cm)
    {
        dense.reserve(PRE_INIT_SIZE);
        sparse.fill(INVALID);
    }

    // No virtual destructor - zero overhead
    ~SystemBase() = default;


    // Entity management
    void add_entity(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        assert(!has_entity(e) && "Entity already in system");

        sparse[e] = count;
        if (count >= dense.size()) {
            dense.emplace_back(e);
        } else {
            dense[count] = e;
        }
        count++;
    }

    void remove_entity(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        assert(has_entity(e) && "Entity not in system");

        u32    idx         = sparse[e];
        u32    last_idx    = count - 1;
        Entity last_entity = dense[last_idx];

        // Swap with last
        dense[idx]          = last_entity;
        sparse[last_entity] = idx;

        // Remove
        sparse[e] = INVALID;
        count--;
    }

    [[nodiscard]] auto has_entity(Entity e) const -> bool
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        return sparse[e] != INVALID && sparse[e] < count && dense[sparse[e]] == e;
    }

    [[nodiscard]] static constexpr auto get_signature() -> Signature { return signature; }

    [[nodiscard]] auto get_entity_count() const -> u32 { return count; }


    // CRTP: Derived must implement update_impl
    void update(float dt) { static_cast<Derived*>(this)->update_impl(dt); }

  protected:
    // Helper to get component data - comp_manager is the only runtime indirection
    template <typename T>
    [[nodiscard]] auto get_component(Entity e) -> T&
    {
        // This is a compile-time access - component_id<T> is constexpr
        // The compiler resolves this to a direct memory access
        return comp_manager.get_arr<T>().get_data(e);
    }

    template <typename T>
    [[nodiscard]] auto get_component(Entity e) const -> const T&
    {
        return comp_manager.get_arr<T>().get_data(e);
    }

    template <typename T>
    [[nodiscard]] auto has_component(Entity e) const -> bool
    {
        return comp_manager.get_arr<T>().has_data(e);
    }
};


