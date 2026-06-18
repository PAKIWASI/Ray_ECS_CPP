#pragma once

#include "component_list.hpp"



template <typename... Ts>
struct Archetype {
    // Compute the signature from the component IDs — constexpr, done at compile time
    static consteval auto signature() -> Signature {
        Signature sig{};
        (sig.set(component_id<Ts>), ...);
        return sig;
    }
};

// Named archetypes — define all entity templates here
using PlayerArchetype   = Archetype<Transform2, RigidBody2>;
//...
