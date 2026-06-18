#pragma once

#include "common.hpp"
#include "wasi.hpp"

using namespace wasi;


template <typename... Ts>
struct SystemList {
    static constexpr u8 count = sizeof...(Ts);
};

class PhysicsSystem;

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

        return std::numeric_limits<u8>::max();
    }();
};

template <typename T>
constexpr SystemType system_id = sys_type_index<T, Systems>::value;


static_assert(system_id<PhysicsSystem> == 0, "PhysicsSystem id wrong!");


