#pragma once


// With Compile time IDs from component_list.hpp, we can initiate all
// arrays in ComponentManager's constructor by folding over type list
// no manual registration calls and no runtime polymorphism needed



#include "common.hpp"
#include <cassert>
#include <vector>
template <ComponentType_t T>
class ComponentArray {
  private:
    // sentinal value indicating ith entity has no data in this array
    static constexpr u32 INVALID = std::numeric_limits<u32>::max();

    std::vector<T>      data;           // actual data - we have `active_entities` valid slots
    std::vector<Entity> idx_to_entity;  // maps data's slot index to what entity owns it
    std::array<u32, MAX_ENTITIES> entity_to_idx; // maps which entity owns what slot index

  public:

    auto has_data(Entity e) -> bool
    {
        return entity_to_idx[e] != INVALID;
    }

    void remove_data(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        u32 idx = entity_to_idx[e];
        assert(idx != INVALID && "Entity does not have component");

        u32 last_idx           = data.size() - 1;
        Entity last_entity     = idx_to_entity[last_idx];

        // swap
        data[idx]              = std::move(data[last_idx]);
        idx_to_entity[idx]     = last_entity;
        entity_to_idx[last_entity] = idx;

        // delete
        entity_to_idx[e]       = INVALID;
        data.pop_back();
        idx_to_entity.pop_back();
    }

    // add a safe version for the fold expression in entity_destroyed
    void remove_if_present(Entity e) {
        if (has_data(e)) { remove_data(e); }
    }
};

// Helper to construct ComponentManager from a ComponentList
#include <tuple>
template <typename... Ts>
class ComponentManagerImpl
{
  private:
    // Each ComponentArray<T> lives directly inline in this tuple
    // [ComponentArray<T1> | ComponentArray<T2> | ComponentArray<T3> ...]
    std::tuple<ComponentArray<Ts>...> arrays;
  public:

    template <typename T>
    consteval auto get_arr() -> ComponentArray<T>& 
    {
        // std::get finds an element of a tuple at compile time
        // from it's type if all elements have unique types
        return std::get<ComponentArray<T>>(arrays);
    }

    // this no longer needs virtual dispatch
    // the fold expression explands to one call per array, all inlined
    void entity_destroyed(Entity e)
    {
        // std::apply applies a function to each element of a tuple
        //  auto&... is a pack of references to each tuple element
        //  each arr is a different type (ComponentArray<T1>, ComponentArray<T2> etc.)
        std::apply([&](auto&... arr) consteval -> void {
            // this fold expression is called on each arr unpacked
            (arr.remove_if_present(e), ...);    // right fold on comma
            // if tuple = {ComponentArray<T1>, ComponentArray<T2>}
            // then args passed to function: func(ComponentArray<T1>&, ComponentArray<T2>&)
            // expands to:
            //      ComponentArray<T1>.remove_if_present(e);
            //      ComponentArray<T2>.remove_if_present(e);
        }, arrays);
    }
};
