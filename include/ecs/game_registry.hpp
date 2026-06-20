#pragma once

// game_registry.hpp — THE single file you edit to extend the ECS.
//
// Include order here is the only place that matters. Everything else in the
// library uses comp_type_index<T,List>::value directly and has no include-order
// dependency.
//
// To add a component: add its type to MyComponents (order = permanent ID).
// To add a system:    add its type to MySystems    (order = schedule bit).
//                     Then include its header below.
//
// component_id<T> and system_id<T> are defined HERE — not in any library
// header — because they reference a specific list, and there is no single
// global list anymore. If you instantiate two different Worlds, define
// separate registry files.

// --- 1. Library infrastructure (no game types, no system headers) ---
#include "world.hpp"

// --- 2. Game component headers ---
#include "2d_comps.hpp"
using namespace ECS_COMPS_2D;


// --- 3. Define the component list — ORDER IS PERMANENT ---
using MyComponents = ComponentList<
    Transform2,   // ID 0
    RigidBody2,   // ID 1
    Gravity2      // ID 2
    // Add new components here. Never reorder existing entries.
>;


// --- 4. Convenience alias: component_id<T> ---
// This is the public API for compile-time component ID lookup.
// Delegates to comp_type_index which is defined in component_registry.hpp.
template <typename T>
constexpr ComponentType component_id = comp_type_index<T, MyComponents>::value;

// Sanity checks — caught at compile time if IDs shift
static_assert(component_id<Transform2> == 0, "Transform2 ID changed — check MyComponents order");
static_assert(component_id<RigidBody2> == 1, "RigidBody2 ID changed — check MyComponents order");
static_assert(component_id<Gravity2>   == 2, "Gravity2 ID changed — check MyComponents order");

// --- 5. ComponentManager alias — needed by system headers ---
// Systems are templated on CMgr (the concrete ComponentManagerImpl type).
// We alias it here so system headers can use it by name.
using ComponentManager = make_component_manager<MyComponents>::type;

// --- 6. System headers — included AFTER ComponentManager is defined ---
// Systems are templated on CMgr so they don't need to be included before
// the lists are defined. The template parameter carries the list via CMgr::ListType.
#include "gravity_system.hpp"
#include "movement_system.hpp"
#include "physics_system.hpp"
// Add new system headers here.


// --- 7. Define the system list — ORDER IS PERMANENT (= schedule bit position) ---
using MySystems = SystemList<
    GravitySystem<ComponentManager>,    // bit 0
    MovementSystem<ComponentManager>,   // bit 1
    PhysicsSystem<ComponentManager>     // bit 2
    // Add new systems here. Never reorder existing entries.
>;


// --- 8. Convenience alias: system_id<T> ---
template <typename T>
constexpr SystemType system_id = sys_type_index<T, MySystems>::value;

// Sanity checks
static_assert(system_id<GravitySystem<ComponentManager>>  == 0, "GravitySystem ID changed");
static_assert(system_id<MovementSystem<ComponentManager>> == 1, "MovementSystem ID changed");
static_assert(system_id<PhysicsSystem<ComponentManager>>  == 2, "PhysicsSystem ID changed");

// --- 9. Instantiate World ---
using GameWorld = World<MyComponents, MySystems>;

// --- 10. Named schedules — constexpr, computed once, embedded as literals ---
// Defined here (not in world.hpp) because they reference concrete system types.
constexpr Schedule GRAVITY_AND_MOVEMENT =
    GameWorld::make_schedule<
        GravitySystem<ComponentManager>,
        MovementSystem<ComponentManager>
    >();

constexpr Schedule PHYSICS_ONLY =
    GameWorld::make_schedule<PhysicsSystem<ComponentManager>>();

constexpr Schedule ALL_SYSTEMS =
    GameWorld::make_schedule<
        GravitySystem<ComponentManager>,
        MovementSystem<ComponentManager>,
        PhysicsSystem<ComponentManager>
    >();

// --- 11. Named archetypes ---
using PlayerArchetype = GameWorld::Archetype<Transform2, RigidBody2>;
using BulletArchetype = GameWorld::Archetype<Transform2, RigidBody2, Gravity2>;




