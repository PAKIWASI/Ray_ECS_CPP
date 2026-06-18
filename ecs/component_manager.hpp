#pragma once

// With Compile time IDs from component_list.hpp, we can initiate all
// arrays in ComponentManager's constructor by folding over type list
// no manual registration calls and no runtime polymorphism needed

#include "common.hpp"
#include "component_list.hpp"

#include <tuple>
#include <cassert>
#include <vector>
#include <array>


template <ComponentType_t T>
class ComponentArray
{
  private:
    // TODO: this is a proper dense/sparse arr right?
    // we just have an extra vector for reverse map

    // dense arr
    std::vector<T>      data;           // actual data - we have `active_entities` valid slots
    // for swap delete, we want to know which entity occupies last index in data
    std::vector<Entity> idx_to_entity;  // maps data's slot index to what entity owns it
    // sparse arr
    std::array<u32, MAX_ENTITIES> entity_to_idx{}; // maps which entity owns what slot index

  public:

    ComponentArray() {
        data.reserve(PRE_INIT_SIZE);
        idx_to_entity.reserve(PRE_INIT_SIZE);
        entity_to_idx.fill(INVALID);
    }

    void add_data(Entity e, T comp) // simple T comp allows rvalues and lvalues alike
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        assert(entity_to_idx[e] == INVALID && "Entity already has component");

        entity_to_idx[e] = data.size();
        idx_to_entity.emplace_back(e);
        data.emplace_back(std::move(comp));
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

    [[nodiscard]] auto get_data(Entity e) -> T&
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        u32 idx = entity_to_idx[e];
        assert(idx != INVALID && "Entity does not have component");
        return data[idx];
    }

    auto has_data(Entity e) -> bool
    {
        return entity_to_idx[e] != INVALID;
    }

    // add a safe version for the fold expression in entity_destroyed
    // entity_destroyed calls this function on every ComponentArray, even if e doesn't have that component
    // raw remove_data(e) would hit a runtime assert, so we first do a cheap lookup
    void remove_if_present(Entity e) {
        if (has_data(e)) { remove_data(e); }
    }
};

// Helper to construct ComponentManager from a ComponentList
template <typename... Ts>
class ComponentManagerImpl
{
  private:
    // Each ComponentArray<T> lives directly inline in this tuple
    // [ComponentArray<T1> | ComponentArray<T2> | ComponentArray<T3> ...]
    std::tuple<ComponentArray<Ts>...> arrays;
  public:

    // this function is can't be consteval
    // consteval controls whether the expression `cm.get_arr<T>()` is usable
    // in a context demanding compile time execution, which it is not as cm is
    // a runtime variable
    template <typename T>
    auto get_arr() -> ComponentArray<T>&
    {
        // std::get finds an element of a tuple at compile time
        // from it's type if all elements have unique types
        return std::get<ComponentArray<T>>(arrays);
    }

    // this no longer needs virtual dispatch
    // the fold expression explands to one call per array, all inlined
    void entity_destroyed(Entity e)
    {
        std::apply([&](auto&... arr) constexpr -> void {
            // For each arr, its component type is known at compile time.
            // Use remove_if_present instead of
            // manually checking the signature here.
            (arr.remove_if_present(e), ...);
        }, arrays);
    }
};


// Alias using our canonical list
// Unpack ComponentList<Ts...> into ComponentManagerImpl<Ts...>
template <typename List>
struct make_component_manager;

// same partial specialization pattern
template <typename... Ts>
struct make_component_manager<ComponentList<Ts...>> {
    using type = ComponentManagerImpl<Ts...>;
};

using ComponentManager = make_component_manager<Components>::type;

// we can do:
//      ComponentManager cm {};
//      ComponentArray<Transform2> t_arr = cm.get_arr<Transform2>();
//      Transform& t = t_arr.get_data(e);
//  only two direct memory accesses


