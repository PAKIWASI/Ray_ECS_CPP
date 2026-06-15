#pragma once

#include "wasi.hpp"

#include <array>
#include <bitset>
#include <cassert>
#include <concepts>
#include <memory>
#include <vector>


// ===================
// ECS core
// ===================


using namespace wasi;


// Configuration
// ==============

// the number of entities can exist in the world at a time
// this means we have `MAX_ENTITIES` slots in each component array
constexpr u32 MAX_ENTITIES  = 1000;

// total number of components that we can register
// The map in component manager holds `MAX_COMPONENTS` component arrays
constexpr u8 MAX_COMPONENTS = 32;


// Setup
// ======

// an entity is just an id, it's an index into component arrays
// where the position i contains data for entity i in each array
using Entity = u32;

// each entity has a signature of `MAX_COMPONENTS` bits that tells
// which components they have. If the bit at i is set, that means entity
// has the ith component and we can get it's data by doing comp_arr = map[i]
// where i is the ith component array in the component manager then comp_arr[entity_index]
// to get that entitity's data
using Signature = std::bitset<MAX_COMPONENTS>;

// each component is an id, using to access component array in the map in comp manager
// this id is also the bit in Signature that signifies entity has this component
using ComponentType = u8;


// Entity Manager
// ===============

// creates and deletes entities
// gives out ids and takes them back
class EntityManager
{
  private:
    // no startup cost of pre filling container with ids
    Entity next_id = 0;
    // when an entity dies, it's id goes here for reuse
    std::vector<Entity> free_ids;

    std::array<Signature, MAX_ENTITIES> signatures;
  public:

    [[nodiscard]] auto create_entity() -> Entity
    {
        if (!free_ids.empty()) {
            Entity recycled = free_ids.back();
            free_ids.pop_back();
            return recycled;
        }
        assert(next_id < MAX_ENTITIES && "MAX_ENTITIES reached");
        return next_id++;
    }

    void destroy_entity(Entity e)
    {
        // max value of next_id is MAX_ENTITIES
        assert(e < next_id && "Entity out of range");
        // reset the signature for later use
        signatures.at(e).reset();
        free_ids.emplace_back(e);
    }

    void set_component(Entity e, ComponentType c)
    {
        signatures.at(e).set(c);
    }
};


// Concept: what counts as a component
template <typename T>
concept ComponentType_t = std::default_initializable<T> // each comp arr is pre-initialized
                       && std::movable<T>;              // we move data into/outof the arr

// Type Erasure
// =============
// we need to store different ComponentArray<T> in the same container
// we have a interface with type agnostic methods. container will have that type
// when we need type specific stuff, we will cast to the correct type

// The Rule of Five warning fires because you declared a destructor but nothing else
// Since ICompArr is a pure interface not meant to be copied or moved, explicitly delete those
struct ICompArr
{
    ICompArr()                                   = default;
    ICompArr(const ICompArr&)                    = delete;
    ICompArr(ICompArr&&)                         = delete;
    auto operator=(const ICompArr&) -> ICompArr& = delete;
    auto operator=(ICompArr&&) -> ICompArr&      = delete;

    virtual ~ICompArr()                          = default; // one vtable entry
    // the only reason entity_destroyed is virtual is because ComponentManager holds ICompArr*
    // and needs to call something through the base pointer without knowing T
    virtual void entity_destroyed(Entity e)      = 0;       // another vtable entry
};


// Templated subclass, this is the type scpecif ComponentArray<T>
// The concept check happens here for each T
template <ComponentType_t T>
class ComponentArray : ICompArr
{
  private:
    std::array<T, MAX_ENTITIES> data{}; // default initialize
  public:

    void add_data(Entity e, T comp)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        data.at(e) = std::move(comp);
    }

    void remove_data(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        // TODO: do i need to explicitly delete previous slot if it owns memory?
        data.at(e) = T{};  // calls default constructor, correct for any ComponentType_t T
    }

    [[nodiscard]] auto get_data(Entity e) -> T&
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        return data.at(e);
    }

    void entity_destroyed(Entity e) override
    {
        remove_data(e);
    }
};


class ComponentManager
{
  private:
    // we need unique ptr as we can't store an abstract type in an array directly
    // it's not instantiable, and it would slice anyway
    std::array<u_ptr<ICompArr>, MAX_COMPONENTS> comp_arrays{};

    // handing out IDs to each type of component
    // first call to get_component_id<T>() with a new T yields the next id
    // all subsequent calls give the same id (static var)
    // This id determines the index into comp_arrays and also the signature bit position
    inline static ComponentType next_comp_id = 0;


    // for type specific operations, we need to cast down to derived class
    template <ComponentType_t T>
    [[nodiscard]] auto get_arr() -> ComponentArray<T>&
    {
        ComponentType id = get_component_id<T>();
        assert(comp_arrays[id] != nullptr && "Component not registered");
        // TODO: is this the correct cast?
        return std::static_pointer_cast<ComponentArray<T>&>(comp_arrays.at(id));
    }

  public:

    // fill a spot in the component array with a component of type T
    template <ComponentType_t T>
    void register_component()
    {
        ComponentType id = get_component_id<T>();
        assert(comp_arrays.at(id) == nullptr && "Component already registered");
        comp_arrays.at(id) = std::make_unique<ComponentArray<T>>();
    }

    // type specifc operations

    template <ComponentType_t T>
    void add_component(Entity e, T comp)
    {
        get_array<T>().add_data(e, std::move(comp));
    }

    template <ComponentType_t T>
    void remove_component(Entity e)
    {
        get_array<T>().remove_data(e);
    }

    template <ComponentType_t T>
    [[nodiscard]] auto get_component(Entity e) -> T&
    {
        return get_array<T>().get_data(e);
    }

    template <ComponentType_t T>
    [[nodiscard]] static auto get_component_id() -> ComponentType
    {
        // static var inside func initialized only once
        static ComponentType id = next_comp_id++;

        assert(id < MAX_COMPONENTS && "MAX_COMPONENTS reached");

        return id;
    }

    // generic operation, no typecast needed. we do a vtable lookup
    void entity_destroyed(Entity e)
    {
        for (const auto& comparr : comp_arrays)
        {
            if (comparr) {
                comparr->entity_destroyed(e);
            }
        }

    }
};


