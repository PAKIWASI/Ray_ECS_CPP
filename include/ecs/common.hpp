#pragma once

#include "wasi.hpp"

#include <bitset>
#include <concepts>
#include <limits>

using namespace wasi;


// Configuration
// =============================================================================

constexpr u32 MAX_ENTITIES  = 1000;
constexpr u8  MAX_COMPONENTS = 32;
constexpr u8  MAX_SYSTEMS    = 16;
constexpr u32 PRE_INIT_SIZE  = 100;


// Core type aliases
// =============================================================================

using Entity        = u32;
using Signature     = std::bitset<MAX_COMPONENTS>;
using ComponentType = u8;
using SystemType    = u8;
using Schedule      = std::bitset<MAX_SYSTEMS>;

static constexpr u32 INVALID = std::numeric_limits<u32>::max();


// Concepts
// =============================================================================

// What counts as a component: must be default-constructible and movable.
// Default-constructible so ComponentArray can pre-allocate slots.
// Movable so we can move data into the array without copying.
template <typename T>
concept ComponentType_t = std::default_initializable<T>
                       && std::movable<T>;


// What counts as a system.
//
// Deliberately does NOT require constructible_from<T, ComponentManager&> here
// because ComponentManager is not a concrete global type — it depends on which
// ComponentList the user chose. That constraint is enforced structurally by
// SystemBase's constructor, not here.
//
// This concept lives in common.hpp (not system_base.hpp) so that
// system_registry.hpp can constrain SystemList<Ts...> without pulling in the
// entire system_base -> component_manager -> component_registry chain, which
// would create a circular dependency.
template <typename T>
concept SystemType_t = requires(T& sys, const T& csys, float dt) {
    // Must implement the update body (called by SystemBase::update via CRTP)
    { sys.update_impl(dt) } -> std::same_as<void>;

    // Must expose a static constexpr signature so SystemManagerImpl can match
    // entities to systems without any runtime lookup
    { T::get_signature() } -> std::same_as<Signature>;

    // Must support the sparse-set entity management interface from SystemBase
    { sys.add_entity(Entity{})    } -> std::same_as<void>;
    { sys.remove_entity(Entity{}) } -> std::same_as<void>;
    { csys.has_entity(Entity{})   } -> std::same_as<bool>;
};
// TODO: add/rem/has is already in systembase, so is get_signature
// each system just needs to inherit from sytembase (most imp constraint), 
// have a update_impl(float dt) function

