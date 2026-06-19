#pragma once

// ComponentList and comp_type_index — pure library infrastructure.
//
// This file deliberately does NOT include any game component headers (2d_comps.hpp etc).
// It only needs common.hpp for ComponentType_t and ComponentType.
// Game components are coupled to the user's game_registry.hpp, not here.

#include "common.hpp"


// ComponentList — ordered type list, position = permanent component ID
// =============================================================================

// Wraps a parameter pack of component types.
// The index of each type in the pack is its component ID.
// Once defined in game_registry.hpp, NEVER reorder.
template <ComponentType_t... Ts>
struct ComponentList {
    static constexpr u8 count = sizeof...(Ts);
};


// comp_type_index — compile-time ID lookup
// =============================================================================
// "What is the ID of type T in ComponentList List?"
// Returns a constexpr u8. Hard error at compile time if T is not in List.

// Primary template — intentionally undefined. Instantiating with anything
// other than a ComponentList triggers an incomplete type error.
template <typename T, typename List>
struct comp_type_index;

// Partial specialization: fires when List = ComponentList<Ts...>
// Unpacks the list and searches for T using a consteval lambda.
template <typename T, typename... Ts>
struct comp_type_index<T, ComponentList<Ts...>>
{
    static constexpr u8 value = []() consteval -> u8
    {
        // Expand the pack into a bool array: matches[i] = (T == Ts[i])
        constexpr bool matches[] { std::is_same_v<T, Ts>... };

        for (u8 i = 0; i < sizeof...(Ts); ++i) {
            if (matches[i]) { return i; }
        }

        // T is not in this ComponentList.
        // The returned max value will be caught by static_asserts in
        // game_registry.hpp. We cannot static_assert(false) here because
        // that fires before instantiation in C++ pre-C++26.
        return std::numeric_limits<u8>::max();
    }();
};



