#pragma once

#include <algorithm>
#include <bitset>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <limits>
#include <memory>
#include <queue>
#include <typeindex>
#include <utility>



// Configuration
// ==============

// Max number of entities alive at once
inline constexpr uint32_t MAX_ENTITIES = 1000;

// Max distinct component types
inline constexpr uint8_t MAX_COMPONENTS = 64;


// Types
// ======

using Entity                        = uint32_t;
inline constexpr Entity NULL_ENTITY = std::numeric_limits<uint32_t>::max();

// A signature is a bitset where each bit = one component type
using Signature = std::bitset<MAX_COMPONENTS>;

// Component type ID - unique per component struct
using ComponentType = uint8_t;


// Component Type ID Generator
// ============================

// static counter - each call with a new T gets a new ID
inline ComponentType component_type_counter = 0;

template <typename T> auto component_type_id() -> ComponentType
{
    static ComponentType id = component_type_counter++;
    assert(id < MAX_COMPONENTS && "Exceeded MAX_COMPONENTS");
    return id;
}


// Concept
// ========
// What countes as a component
// must be default constructable and movable
template <typename T>
concept ComponentType_t = std::default_initializable<T> && std::movable<T>;


// IComponent Array
// =================
// Type-erased interface
// We need to store different ComponentArray<T> in one container
// We can't do that with templates directly, so we use a
// virtual base class (type erasure via inheritance)
class IComponentArray
{
  public:
    virtual ~IComponentArray() = default;
    // each entity is subscribed to different components
    // so different deletions
    virtual void entity_destroyed(Entity entity) = 0;
};


// TODO: understand
// Component Array
// ================
// Stores one type T of component
// Uses a packed array with bidirectional index maps so we never have holes
// each data[i] contains the component data for the entity i
// eg data[i] for position has the {x, y} coords for entity i
template <ComponentType_t T> class ComponentArray : public IComponentArray
{
  private:
    std::array<T, MAX_ENTITIES>   data{};
    std::flat_map<Entity, size_t> entity_to_index{}; // Entity ID -> array idx
    std::flat_map<size_t, Entity> index_to_entity{}; // array idx -> Entity ID
    size_t                        size = 0;

  public:
    void insert(Entity entity, T component)
    {
        assert(entity_to_index.contains(entity) &&
               "Component already added to this entity");

        // set maps both ways
        entity_to_index[entity] = size;  // curr assignable position is `size`
        index_to_entity[size]   = entity;
        data[size]              = std::move(component);
        size++; // increment
    }

    void remove(Entity entity)
    {
        assert(entity_to_index.contains(entity) &&
               "Removing a component that doesn't exist");

        // Swap delete: move to end of array and then delete
        size_t removed_idx = entity_to_index[entity];
        size_t last_idx    = size - 1;
        data[removed_idx]  = std::move(data[last_idx]);

        // Update maps for moved element
        Entity last_entity           = index_to_entity[last_idx];
        entity_to_index[last_entity] = removed_idx;
        index_to_entity[removed_idx] = last_entity;

        entity_to_index.erase(entity);
        index_to_entity.erase(last_idx);
        size--;
    }

    [[nodiscard]] auto get(Entity entity) -> T&
    {
        assert(entity_to_index.contains(entity) &&
               "Entity does not have this component");

        return data[entity_to_index[entity]];
    }

    [[nodiscard]] auto has(Entity entity) const -> bool { return entity_to_index.contains(entity); }

    // TODO: understand
    // Called with an entity is destroyed - remove it's components if present
    void entity_destroyed(Entity entity) override
    {
        if (entity_to_index.contains(entity)) {
            remove(entity);
        }
    }

    // Iterate over all components (for system processing)
    // Returns a view of valid components
    auto all() -> std::span<T> { return { data.data(), size }; }
};


// Entity Manager
// ===============
// Hands out entity IDs from a free list (queue)
// Also tracks each entity's component signature
class EntityManager
{
  private:
    std::queue<Entity>                  available_entities;
    std::array<Signature, MAX_ENTITIES> signatures{};
    uint32_t                            living_count = 0;

  public:
    EntityManager()
    {
        // Pre-fill the queue with all valid IDs
        for (Entity e = 0; e < MAX_ENTITIES; e++) {
            available_entities.push(e);
        }
    }

    [[nodiscard]] auto create() -> Entity
    {
        assert(living_count < MAX_ENTITIES && "Too many entities!");

        Entity id = available_entities.front();
        available_entities.pop();
        living_count++;
        return id;
    }

    void destroy(Entity entity)
    {
        assert(entity < MAX_ENTITIES && "Entity out of range");

        signatures[entity].reset(); // clear its signature
        available_entities.push(entity);
        living_count--;
    }

    void set_signature(Entity entity, Signature sig)
    {
        assert(entity < MAX_ENTITIES && "Entity out of range");
        signatures[entity] = sig;
    }

    [[nodiscard]] auto get_signature(Entity entity) -> Signature
    {
        assert(entity < MAX_ENTITIES && "Entity out of range");
        return signatures[entity];
    }

    [[nodiscard]] auto count() const -> uint32_t { return living_count; }
};


// Component Manager
// ==================
// Stores one ComponentArray<T> per component type
// The arrays are kept in a map keyed by std::type_index
// (the type-erased key for component type)
class ComponentManager
{
  private:
    std::flat_map<std::type_index, ComponentType>                    component_types{};
    // TODO: why a map? each component should get and id which can be an index in an array
    std::flat_map<std::type_index, std::shared_ptr<IComponentArray>> component_arrays{};

    template<ComponentType_t T>
    auto get_array() -> std::shared_ptr<ComponentArray<T>>
    {
        auto key = std::type_index(typeid(T));
        assert(component_arrays.contains(key) && "Component not registered");
        return std::static_pointer_cast<ComponentArray<T>>(component_arrays[key]);
    }

  public:

    template<ComponentType_t T>
    void register_component()
    {
        auto key = std::type_index(typeid(T));
        assert(!component_arrays.contains(key) && "Already registered");
        component_types[key]  = component_type_id<T>();
        component_arrays[key] = std::make_shared<ComponentArray<T>>();
    }

    template<ComponentType_t T>
    [[nodiscard]] auto get_component_type() -> ComponentType
    {
        auto key = std::type_index(typeid(T));
        assert(component_types.contains(key) && "Component not registered");
        return component_types[key];
    }

    template<ComponentType_t T>
    void add_component(Entity entity, T component)
    {
        get_array<T>()->insert(entity, std::move(component));
    }

    template<ComponentType_t T>
    void remove_component(Entity entity)
    {
        get_array<T>()->remove(entity);
    }

    template<ComponentType_t T>
    [[nodiscard]] auto get_component(Entity entity)
    {
        return get_array<T>()->get(entity);
    }

    template<ComponentType_t T>
    auto has_component(Entity entity) -> bool
    {
        return get_array<T>()->has(entity);
    }

    // Notify all arrays, each removes the entity's component if present
    void entity_destroyed(Entity entity)
    {
        for (const auto& [key, array] : component_arrays) {
            array->entity_destroyed(entity);
        }
    }
};


// System Base Class
// ==================
// A system holds a set of entities that match it's signature
// The world updated this set when entities change
class System
{
  public:
    virtual ~System() = default;
    virtual void update(float dt) = 0;
    [[nodiscard]] virtual auto get_signature() const -> Signature = 0;

    // The set of entities this system acts on
    // TODO: use flat_set?
    std::vector<Entity> entities;
};


// System Manager
// ===============
// Stores all systems and updates their entity sets when an
// entity's signature changes
class SystemManager
{
  private:
    std::flat_map<std::type_index, std::unique_ptr<System>> systems{};
  
  public:
    // TODO: understand
    template<typename S, typename... Args>
    requires std::derived_from<S, System>
    auto register_system(Args&&... args) -> S*
    {
        auto key = std::type_index(typeid(S));
        assert(!systems.contains(key) && "System already registered");
        // TODO: wtf is perfect forwarding ?
        auto system = std::make_unique<S>(std::forward<Args>(args)...);
        S* ptr = system.get();
        systems[key] = std::move(system);
        return ptr;
    }

    void entity_destroyed(Entity entity)
    {
        for (const auto& [key, system] : systems) {
            auto& ents = system->entities;
            ents.erase(std::remove(ents.begin(), ents.end(), entity), ents.end());
        }
    }

    void entity_signature_changed(Entity entity, Signature ent_sig)
    {
        for (const auto& [key, system] : systems) {
            Signature sys_sig = system->get_signature();
            auto& ents = system->entities;
            bool has_entity = std::ranges::contains(ents, entity);
            bool matches    = (ent_sig & sys_sig) == sys_sig;

            if (matches && !has_entity) {
                ents.push_back(entity);
            } else if (!matches && has_entity) {
                ents.erase(std::remove(ents.begin(), ents.end(), entity), ents.end());
            }
        }
    }

    void update_all(float dt)
    {
        for (const auto& [key, system] : systems) {
            system->update(dt);
        }
    }
};


// WORLD — The main ECS facade
// ============================
// This is the only class you interact with in game code
// It ties together Entity, Component, and System management
class World
{
  private:
    std::unique_ptr<EntityManager>    entity_manager;
    std::unique_ptr<ComponentManager> component_manager;
    std::unique_ptr<SystemManager>    system_manager;

  public:

    World()
    {
        entity_manager    = std::make_unique<EntityManager>();
        component_manager = std::make_unique<ComponentManager>();
        system_manager    = std::make_unique<SystemManager>();
    }

    // Entity API

    [[nodiscard]] auto create_entity() -> Entity
    {
        return entity_manager->create();
    }

    void destroy_entity(Entity entity)
    {
        entity_manager->destroy(entity);
        component_manager->entity_destroyed(entity);
        system_manager->entity_destroyed(entity);
    }

    // Component API

    template<ComponentType_t T>
    void register_component() { component_manager->register_component<T>(); }

    template<ComponentType_t T>
    void remove_component(Entity entity)
    {
        component_manager->remove_component<T>(entity);
        auto sig = entity_manager->get_signature(entity);
        sig.reset(component_manager->get_component_type<T>());
        entity_manager->set_signature(entity, sig);
        system_manager->entity_signature_changed(entity, sig);
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
    }

    // System API

    template<typename S, typename... Args>
    requires std::derived_from<S, System>
    auto register_system(Args&&... args) -> S*
    {
        return system_manager->register_system<S>(std::forward<Args>(args)...);
    }

    void update(float dt)
    {
        system_manager->update_all(dt);
    }

    [[nodiscard]] auto entity_count() const -> uint32_t { return entity_manager->count(); }
};



