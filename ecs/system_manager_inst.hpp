#pragma once

#include "system_manager.hpp"
#include "system_list.hpp"  // Now includes forward declarations
#include "physics_system.hpp"  // Full definitions



// Now we have all the full definitions
template <typename List>
struct make_system_manager;

template <SystemType_t... Ts>
struct make_system_manager<SystemList<Ts...>> {
    using type = SystemManagerImpl<Ts...>;
};

using SystemManager = make_system_manager<Systems>::type;

// Static asserts now work because PhysicsSystem and RenderSystem are fully defined
static_assert(SystemType_t<PhysicsSystem>, "PhysicsSystem does not satisfy SystemType_t");
// static_assert(SystemType_t<RenderSystem>,  "RenderSystem does not satisfy SystemType_t");

