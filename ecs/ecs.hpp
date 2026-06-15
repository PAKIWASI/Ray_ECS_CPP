#pragma once

#include "wasi.hpp"

#include <array>
#include <bitset>
#include <cassert>
#include <concepts>
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

// hands out IDs to new entities
class EntityManager
{
  private:
    Entity next_id = 0;         // goes from 0 to MAX_ENTITIES
    // when an entity dies, it's id goes here for reuse
    std::vector<Entity> free_ids;
  public:

    [[nodiscard]] auto create_entity() -> Entity
    {
        Entity next = next_id++;
        assert(next < MAX_ENTITIES && "MAX_ENTITIES reached");
        return next;
    }

    void destroy_entity(Entity e)
    {
        // max value of next_id is MAX_ENTITIES
        assert(e < next_id && "Entity out of range");
        free_ids.emplace_back(e);
    }
};


// Assigning IDs to each component type
// =====================================

// global constexpr counter
// each component type is templated with T
// when initing each component type, it will grab and id from here the increment
// TODO: move this to component_manager so we don't have anything global
inline ComponentType comp_id = 0;

// TODO: can i make this run at compile time completely?
//
// when this function is called with different T for the first time, it will
// init static var and return a new id. all subsequent calls with same T return same id
template <typename T>
[[nodiscard]] auto get_component_id(T comp) -> ComponentType
{
    // static var inside func initialized only once
    static ComponentType id = comp_id++;

    assert(id < MAX_COMPONENTS && "max componentypes reached");

    return id;
}

// Concept: what counts as a component
template <typename T>
concept ComponentType_t = std::default_initializable<T> // each comp arr is pre-initialized
                        && std::movable<T>;             // we move data into/outof the arr

// Type Erasure
// =============
// we need to store different ComponentArray<T> in the same container
// we have a interface with type agnostic methods. container will have that type
// when we need type specific stuff, we will cast to the correct type

struct ICompArr
{
    virtual ~ICompArr() = default;          // one vtable entry
    virtual void entity_destroyed() = 0;    // another vtable entry
};


// Templated subclass, this is the type scpecif ComponentArray<T>
// The concept check happens here for each T
template <ComponentType_t T>
class ComponentArray : ICompArr
{
  private:
    std::array<T, MAX_ENTITIES> data{};

  public:


};


