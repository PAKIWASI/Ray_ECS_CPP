#pragma once

#include "wasi.hpp"

#include <bitset>
#include <concepts>

using namespace wasi;


// Configuration
// ==============

// the number of entities can exist in the world at a time
// this means we have `MAX_ENTITIES` slots in each component array
constexpr u32 MAX_ENTITIES  = 1000;

// total number of components that we can register
// The container in component manager holds `MAX_COMPONENTS` component arrays
constexpr u8 MAX_COMPONENTS = 32;

constexpr u8 MAX_SYSTEMS    = 16;

constexpr u32 PRE_INIT_SIZE = 100;


// Setup
// ======

// an entity is just an id, it's an index into component arrays
// where the position i contains data for entity i in each array
using Entity = u32;

// each entity has a signature of `MAX_COMPONENTS` bits that tells
// which components they have. If the bit at i is set, that means entity
// has the ith component and we can get it's data by doing comp_arr = container[i]
// where i is the ith component array in the component manager then comp_arr[entity_index]
// to get that entitity's data
using Signature = std::bitset<MAX_COMPONENTS>;

// each component is an id, using to access component array in the container in comp manager
// this id is also the bit in Signature that signifies entity has this component
using ComponentType = u8;

using SystemType    = u8;

using Schedule      = std::bitset<MAX_SYSTEMS>;

static constexpr u32 INVALID = std::numeric_limits<u32>::max();



// what counts as a component
template <typename T>
concept ComponentType_t = std::default_initializable<T> // each comp arr is pre-initialized
                       && std::movable<T>;              // we move data into the arr

