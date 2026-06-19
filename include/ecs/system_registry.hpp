#pragma once

// SystemList and sys_type_index — pure library infrastructure.
//
// DELIBERATELY has zero includes of any system headers (physics_system.hpp etc).
// Including system headers here caused the original circular dependency:
//   system_registry -> physics_system -> system_base -> component_manager
//                   -> component_registry -> (back to system_registry via system_manager)
//
// SystemType_t concept is in common.hpp so we can constrain SystemList<Ts...>
// without pulling in the system_base -> component_manager chain.
// System headers are included in game_registry.hpp AFTER the lists are defined.

#include "common.hpp"


// SystemList — ordered type list, position = permanent system ID
// =============================================================================

template <SystemType_t... Ts>
struct SystemList {
    static constexpr u8 count = sizeof...(Ts);
};


// sys_type_index — compile-time system ID lookup
// =============================================================================
// "What is the ID of type T in SystemList List?"
// Same pattern as comp_type_index in component_registry.hpp.

template <typename T, typename List>
struct sys_type_index;

template <typename T, typename... Ts>
struct sys_type_index<T, SystemList<Ts...>>
{
    static constexpr u8 value = []() consteval -> u8
    {
        constexpr bool matches[] { std::is_same_v<T, Ts>... };

        for (u8 i = 0; i < sizeof...(Ts); ++i) {
            if (matches[i]) { return i; }
        }

        // T not in list — caught by static_asserts in game_registry.hpp
        return std::numeric_limits<u8>::max();
    }();
};
