#pragma once

#include "common.hpp"

#include "physics_system.hpp"


template <SystemType_t... Ts>
struct SystemList {
    static constexpr u8 count = sizeof...(Ts);
};


// single source of truth for all systems resolved at compile time
// the position in array is the id/index
using Systems = SystemList<
    PhysicsSystem  // ID 0
>;


template <typename T, typename List>
struct sys_type_index;


template <typename T, typename... Ts>
struct sys_type_index<T, SystemList<Ts...>>
{
    static constexpr u8 value = []() consteval -> u8 {
        constexpr bool matches[] { std::is_same_v<T, Ts>... };

        for (u8 i = 0; i < sizeof...(Ts); ++i) {
            if (matches[i]) { return i; }
        }
        // static_assert(false, "Type is not in SystemList");
        // work around for static_assert(false) not working
        return std::numeric_limits<u8>::max();
    }();
};

// final alias
template <typename T>
constexpr SystemType system_id = sys_type_index<T, Systems>::value;


static_assert(system_id<PhysicsSystem> == 0, "PhysicsSystem id wrong!");


