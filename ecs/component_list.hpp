#pragma once

// The single source of truth for all component IDs
// The position of each type in this list is it's permanent ID

#include "2d_comps.hpp"
#include "wasi.hpp"
#include "common.hpp"
#include <limits>

using namespace ECS_COMPS_2D;
using namespace wasi;


// Takes list of Component types as a pack and saves a compile time
// count, it also has pack info embedded, which we will extract
template <typename... Ts>
struct ComponentList {
    static constexpr u8 count = sizeof...(Ts);
};


// Add new Components here - order determines ID
using Components = ComponentList<
    Transform2,     // ID 0
    RigidBody2,     // ID 1
    Gravity2        // ID 2
>;


// Type Index: Compile time ID lookup
// "What is the ID of type T in list List"
// Result is a constexpr u8, computed entirely at compile time

// first we do forward declaration without defining struct
template <typename T, typename List>
struct comp_type_index;

// partial specilization: this def is used when we pass ComponentList<Ts...> as List
template <typename T, typename... Ts>       // whatever passed, first is T and rest are Ts...
struct comp_type_index<T, ComponentList<Ts...>>  // we passed this as List
{
    // static constexpr member var, computed at compile time
    // using a constexpr lambda, enabling us to use loops at compile time
    static constexpr u8 value = []() consteval -> u8 {
        // is_same_v<T, U> returns compile time bool if T is same type as U
        // is_same_v<T,Ts>... replaces Ts with each T in the pack and we get an array of bools
        constexpr bool matches[] { std::is_same_v<T, Ts>... };

        // loop to find the index which is true, should be exactly one
        for (u8 i = 0; i < sizeof...(Ts); ++i) {
            if (matches[i]) { return i; }
        }
        // if T is not in the list, hard error at compile time
        // static assert inside lambda is C++23
        // static_assert(false, "Component type is not registered in ComponentList");
        // []<bool flag = false>() -> auto { static_assert(flag, "Component not in list"); }();
        return std::numeric_limits<u8>::max();  // backup for now
    }();
};

// Convenient alias
template  <typename T>
constexpr ComponentType component_id = comp_type_index<T, Components>::value;

// final test: have to do this 'cause the stupid fucking static_assert(false) is not working
static_assert(component_id<Transform2> == 0, "Transform2 id wrong!");
static_assert(component_id<RigidBody2> == 1, "RigidBody2 id wrong!");
static_assert(component_id<Gravity2>   == 2, "Gravity2 id wrong!");



