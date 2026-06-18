#pragma once

#include "system_base.hpp"
#include "system_list.hpp"


template <SystemType_t... Systems>
class SystemManagerImpl
{
  private:
    // Direct storage of all systems - no pointers, no indirection
    std::tuple<Systems...> systems;

    // Each system holds a ref to ComponentManager
    // We set it during construction

  public:

    // Constructor takes ComponentManager and initializes all systems
    SystemManagerImpl(ComponentManager& cm)
        : systems(create_systems<Systems...>(cm))
    {}

    // Helper to create all systems with ComponentManager
    template <typename... Sys>
    // TODO: made consteval
    consteval static auto create_systems(ComponentManager& cm)
    {
        // Create each system with the ComponentManager reference
        return std::tuple<Sys...>(Sys(cm)...);
    }

    // Get system by type - compile-time lookup
    template <typename T>
    [[nodiscard]] auto get_system() -> T& { return std::get<T>(systems); }

    // template <typename T>
    // [[nodiscard]] auto get_system() const -> const T& { return std::get<T>(systems); }

    // Handle entity signature changes
    void on_signature_change(Entity e, Signature new_sig)
    {
        std::apply([&](auto&... sys) -> void {
            (check_and_update_entity(sys, e, new_sig), ...);
        }, systems);
    }

    void on_entity_destroyed(Entity e)
    {
        std::apply([&](auto&... sys) -> void {
            ((sys.has_entity(e) ? sys.remove_entity(e) : void()), ...);
        }, systems);
    }

    // Update all systems with schedule
    void update(float dt, Schedule schedule)
    {
        // TODO: how to do better ?
        std::apply([&](auto&... sys) -> void {
            ((schedule.test(
                // decltype expands to the current type in the fold expression expansion
                // remove_cvref removes const and ref
                system_id<std::remove_cvref_t<decltype(sys)>>
            ) ? sys.update(dt) : void()), ...);
        }, systems);
    }

    // Update specific system - direct call
    template <typename T>
    void update_system(float dt) { std::get<T>(systems).update_impl(dt); }

    // Get entity count for a system
    template <typename T>
    [[nodiscard]] auto get_entity_count() const -> u32
    {
        return std::get<T>(systems).get_entity_count();
    }

  private:
    template <typename System>
    void check_and_update_entity(System& sys, Entity e, Signature new_sig)
    {
        bool qualifies = (new_sig & System::get_signature()) == System::get_signature();
        bool has       = sys.has_entity(e);

        if      ( qualifies && !has) { sys.add_entity(e);    }
        else if (!qualifies &&  has) { sys.remove_entity(e); }
    }
};


