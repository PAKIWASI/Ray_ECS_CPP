#pragma once

#include "component_manager.hpp"
#include "entity_manager.hpp"
#include "system_manager.hpp"


class World
{
  private:
    EntityManager    entity_manager;
    ComponentManager component_manager {};
    SystemManager    system_manager {component_manager};

  public:

    [[nodiscard]] auto create_entity(Signature sig = 0) -> Entity
    {
        return entity_manager.create(sig);
    }

    void destroy_entity(Entity e)
    {
        // systems and components must clean up before the id is recycled
        system_manager.    on_entity_destroyed(e);
        component_manager. entity_destroyed(e);
        entity_manager.    destroy(e);
    }

    // TODO:
    // Overload 1: default-constructed components
    // Useful for pooled entities that will have their data set after creation
    template <typename A>
    auto create_from_archetype() -> Entity {
        Entity e = entity_manager.create();
        // fold: calls add_component for each T in A's pack
        // A must expose its Ts... — see ArchetypeImpl below
        // TODO: What is this ??
        A::for_each_type([&]<typename T>() -> void {
            add_component<T>(e, T{});
        });
        return e;
    }

    // TODO:
    // Overload 2: caller supplies initial component values
    // Args must match the archetype's component types IN ORDER
    template <typename A, typename... Args>
    auto create_from_archetype(Args&&... args) -> Entity {
        Entity e = entity_manager.create();
        (add_component(e, std::forward<Args>(args)), ...);
        return e;
    }

    // Component API

    template <ComponentType_t T>
    void add_component(Entity e, T comp)
    {
        component_manager.get_arr<T>().add_data(e, std::move(comp));
    }

    template <ComponentType_t T>
    void remove_component(Entity e)
    {
        component_manager.get_arr<T>().remove_data(e);
    }

    // System API

    void update(float dt, Schedule schedule)
    {
        system_manager.update(dt, schedule);
    }

};
