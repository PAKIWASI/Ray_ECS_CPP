#pragma once

#include "entity_manager.hpp"
#include "component_manager.hpp"
#include "system_manager.hpp"

class World {
private:
    EntityManager entity_manager{};
    ComponentManager component_manager{};
    SystemManager system_manager{component_manager};  // Passes ComponentManager
    
public:
    World() = default;
    
    // Entity API
    auto create_entity(Signature sig = 0) -> Entity {
        return entity_manager.create(sig);
    }
    
    void destroy_entity(Entity e) {
        system_manager.on_entity_destroyed(e);
        component_manager.entity_destroyed(e);
        entity_manager.destroy(e);
    }
    
    // Component API
    template <ComponentType_t T>
    void add_component(Entity e, T comp) {
        component_manager.get_arr<T>().add_data(e, std::move(comp));
        entity_manager.set_component(e, component_id<T>);
        system_manager.on_signature_change(e, entity_manager.get_signature(e));
    }
    
    template <ComponentType_t T>
    void remove_component(Entity e) {
        component_manager.get_arr<T>().remove_data(e);
        entity_manager.unset_component(e, component_id<T>);
        system_manager.on_signature_change(e, entity_manager.get_signature(e));
    }
    
    template <ComponentType_t T>
    T& get_component(Entity e) {
        return component_manager.get_arr<T>().get_data(e);
    }
    
    // System API
    template <typename T>
    T& get_system() {
        return system_manager.get_system<T>();
    }
    
    void update(float dt, Schedule schedule) {
        system_manager.update(dt, schedule);
    }
    
    template <typename T>
    void update_system(float dt) {
        system_manager.update_system<T>(dt);
    }
};
