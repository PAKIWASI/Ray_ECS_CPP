#pragma once

#include "common.hpp"
#include "component_manager.hpp"
#include "system_list.hpp"
#include <bitset>
#include <cassert>
#include <vector>


// Helper to compute system signature from component list
template <typename... Ts>
consteval auto make_signature() -> Signature {
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
    std::vector<Entity> dense;              // Contiguous entities for iteration
    std::array<u32, MAX_ENTITIES> sparse {}; // Maps entity -> index in dense
    u32 count = 0;                          // Number of entities in system

    // Pointer to ComponentManager - single runtime indirection
    // This is the ONLY runtime data we need
    const ComponentManager& comp_manager;

    // Computed signature at compile time
    static constexpr Signature signature = make_signature<ComponentTypes...>();

public:
    // Default constructor - systems can be default constructed
    SystemBase() = default;

    SystemBase(const SystemBase&)            = delete;
    SystemBase(SystemBase&&)                 = delete;
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

        u32 idx = sparse[e];
        u32 last_idx = count - 1;
        Entity last_entity = dense[last_idx];

        // Swap with last
        dense[idx] = last_entity;
        sparse[last_entity] = idx;

        // Remove
        sparse[e] = INVALID;
        count--;
    }

    [[nodiscard]] auto has_entity(Entity e) const -> bool {
        assert(e < MAX_ENTITIES && "Entity out of range");
        return sparse[e] != INVALID && sparse[e] < count && dense[sparse[e]] == e;
    }

    [[nodiscard]] static constexpr auto get_signature() -> Signature {
        return signature;
    }

    // CRTP: Derived must implement update_impl
    void update(float dt) {
        static_cast<Derived*>(this)->update_impl(dt);
    }

protected:
    // Helper to get component data - comp_manager is the only runtime indirection
    template <typename T>
    [[nodiscard]] auto get_component(Entity e) -> T& {
        // This is a compile-time access - component_id<T> is constexpr
        // The compiler resolves this to a direct memory access
        return comp_manager.get_arr<T>().get_data(e);
    }

    template <typename T>
    [[nodiscard]] auto get_component(Entity e) const -> const T& {
        return comp_manager.get_arr<T>().get_data(e);
    }

    template <typename T>
    [[nodiscard]] auto has_component(Entity e) const -> bool {
        return comp_manager.get_arr<T>().has_data(e);
    }
};



template <typename... Systems>
class SystemManagerImpl {
private:
    // Direct storage of all systems - no pointers, no indirection
    std::tuple<Systems...> systems;
    
    // Each system holds a ref to ComponentManager
    // We set it during construction
    
public:
    // Constructor takes ComponentManager and initializes all systems
    explicit SystemManagerImpl(const ComponentManager& cm)
        : systems(create_systems<Systems...>(cm))
    {}
    
    // Helper to create all systems with ComponentManager
    template <typename... Sys>
    static auto create_systems(ComponentManager& cm) {
        // Create each system with the ComponentManager reference
        return std::tuple<Sys...>(Sys(cm)...);
    }
    
    // Get system by type - compile-time lookup
    template <typename T>
    [[nodiscard]] auto get_system() -> T& {
        return std::get<T>(systems);
    }
    
    template <typename T>
    [[nodiscard]] auto get_system() const -> const T& {
        return std::get<T>(systems);
    }
    
    // Handle entity signature changes
    void on_signature_change(Entity e, Signature new_sig) {
        std::apply([&](auto&... sys) -> void {
            (check_and_add_entity(sys, e, new_sig), ...);
        }, systems);
    }
    
    void on_entity_destroyed(Entity e, Signature sig) {
        std::apply([&](auto&... sys) -> void {
            u8 idx = 0;
            ((sig.test(idx++)? sys.remove_entity(e) : void()), ...);    // TODO: calls remove on all systems but remove will assert that entity not present in set
        }, systems);
    }
    
    // Update all systems with schedule
    void update(float dt, Schedule schedule) {
        std::apply([&](auto&... sys) -> void {
            u8 idx = 0;
            ((schedule.test(idx++) ? sys.update(dt) : void()), ...);
        }, systems);
    }
    
    // Update specific system - direct call
    template <typename T>
    void update_system(float dt) {
        std::get<T>(systems).update(dt);
    }
    
    // Get entity count for a system
    template <typename T>
    [[nodiscard]] auto get_entity_count() const -> u32 {
        return std::get<T>(systems).get_entities().size();
    }

private:
    template <typename System>
    void check_and_add_entity(System& sys, Entity e, Signature new_sig) {
        if ((new_sig & System::get_signature()) == System::get_signature()) {
            sys.add_entity(e);
        }
    }
};


// Alias
template <typename List>
struct make_system_manager;

template <typename... Ts>
struct make_system_manager<SystemList<Ts...>> {
    using type = SystemManagerImpl<Ts...>;
};

using SystemManager = make_system_manager<Systems>::type;



