#pragma once

#include "common.hpp"
#include "component_manager.hpp"
#include "component_registry.hpp"
#include "entity_manager.hpp"
#include "system_manager.hpp"
#include "archetype.hpp"

// TODO: how to incorporate into world:
// 1. archtypes
// 2. compile time make_schedule
// 3. other stuff


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
        // order matters here
        // systems and components must clean up before the id is recycled
        system_manager.    on_entity_destroyed(e);
        component_manager. entity_destroyed(e);
        entity_manager.    destroy(e);
    }

    // TODO: understand archtype init

    // Overload 1: default-constructed
    template <typename A>   // TODO: define concept for archetype, we dont have lsp support
    [[nodiscard]] auto create_from_archetype() -> Entity
    {
        Entity e = entity_manager.create();
        A::for_each_type([&]<typename T>() -> void {
            component_manager.get_arr<T>().add_data(e, T{});
            entity_manager.set_component(e, component_id<T>);
        });
        system_manager.on_signature_change(e, entity_manager.get_signature(e));
        return e;
    }

    // Overload 2: caller supplied values
    template <typename A, typename... Args>  // TODO: why not by refernece ?
    [[nodiscard]] auto create_from_archetype(Args&&... args) -> Entity
    {
        Entity e = entity_manager.create();
        (add_component(e, std::forward<Args>(args)), ...);
        return e;
    }

    // Component API

    template <ComponentType_t T>
    void add_component(Entity e, T comp)
    {
        component_manager.get_arr<T>().add_data(e, std::move(comp));
        entity_manager.set_component(e, component_id<T>);
        system_manager.on_signature_change(e, entity_manager.get_signature(e));
    }

    template <ComponentType_t T>
    void remove_component(Entity e)
    {
        component_manager.get_arr<T>().remove_data(e);
        entity_manager.unset_component(e, component_id<T>);
        system_manager.on_signature_change(e, entity_manager.get_signature(e));
    }

    // System API

    void update(float dt, Schedule schedule)
    {
        system_manager.update(dt, schedule);
    }
};


// make a compile time schedule your systems
template <typename... Ts>
static consteval auto make_schedule() -> Schedule {
    Schedule s{};
    (s.set(system_id<Ts>), ...);
    return s;
}

// Usage:
constexpr Schedule PHYSICS_ONLY = make_schedule<PhysicsSystem>();

