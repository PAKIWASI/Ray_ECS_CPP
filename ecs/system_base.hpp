#pragma once

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
    { system.add_entity(Entity{}) } -> std::same_as<void>;
    { system.remove_entity(Entity{}) } -> std::same_as<void>;
    { csystem.has_entity(Entity{}) } -> std::same_as<bool>;

    // 4. Must be constructible from a ComponentManager (this is how systems
    //    get "easy access to the components it needs" — via comp_manager
    requires std::constructible_from<T, ComponentManager&>;
};


// Helper to compute system signature from component list
template <typename... Ts>
consteval auto make_signature() -> Signature
{
    Signature sig;
    ((sig.set(component_id<Ts>)), ...);
    return sig;
}


// CRTP base - no virtual functions, vtable, or runtime references
template <typename Derived, typename... ComponentTypes>
class SystemBase
{
  protected:
    // Sparse set implementation
    std::vector<Entity>           dense;     // Contiguous entities for iteration
    std::array<u32, MAX_ENTITIES> sparse{};  // Maps entity -> index in dense

    // Pointer to ComponentManager - single runtime indirection
    // This is the ONLY runtime data we need
    ComponentManager& comp_manager;

    // Computed signature at compile time
    static constexpr Signature signature = make_signature<ComponentTypes...>();

  public:
    // SystemBase(const SystemBase&)                    = delete;
    // SystemBase(SystemBase&&)                         = delete;
    auto operator=(const SystemBase&) -> SystemBase& = delete;
    auto operator=(SystemBase&&) -> SystemBase&      = delete;

    // Constructor with ComponentManager
    SystemBase(ComponentManager& cm) : comp_manager(cm)
    {
        dense.reserve(PRE_INIT_SIZE);
        sparse.fill(INVALID);
    }

    SystemBase(SystemBase& other) noexcept
        : comp_manager(other.comp_manager)
        , dense(std::move(other.dense))
        , sparse(other.sparse)
    {}

    // move constructor
    SystemBase(SystemBase&& other) noexcept
        : comp_manager(other.comp_manager)
        , dense(std::move(other.dense))
        , sparse(other.sparse)
    {}

    // No virtual destructor - zero overhead
    ~SystemBase() = default;


    // Entity management
    void add_entity(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        assert(!has_entity(e) && "Entity already in system");

        size_t count = dense.size();
        sparse[e] = count;
        if (count >= dense.size()) {
            dense.emplace_back(e);      // size++
        } else {
            dense[count] = e;
        }
    }

    void remove_entity(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        assert(has_entity(e) && "Entity not in system");

        u32    idx         = sparse[e];
        u32    last_idx    = dense.size() - 1;
        Entity last_entity = dense[last_idx];

        // Swap with last
        dense[idx]          = last_entity;
        sparse[last_entity] = idx;
        dense.pop_back();           // size--

        // Remove
        sparse[e] = INVALID;
    }

    [[nodiscard]] auto has_entity(Entity e) const -> bool
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        return sparse[e] != INVALID && sparse[e] < dense.size() && dense[sparse[e]] == e;
    }

    [[nodiscard]] static constexpr auto get_signature() -> Signature { return signature; }

    [[nodiscard]] auto get_entity_count() const -> u32 { return dense.size(); }


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
    [[nodiscard]] auto has_component(Entity e) const -> bool
    {
        return comp_manager.get_arr<T>().has_data(e);
    }
};


