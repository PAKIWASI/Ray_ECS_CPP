#pragma once

#include "component_registry.hpp"


template <typename T>
concept ArchetypeType_t = requires {
    T::size();
    T::signature();
};


template <typename... Ts>
struct Archetype
{
    // Compute the signature from the component IDs — constexpr, done at compile time
    static consteval auto signature() -> Signature {
        Signature sig{};
        (sig.set(component_id<Ts>), ...);
        return sig;
    }

    // TODO: understand
    // Helper to iterate over all component types in this archetype
    // Calls the provided lambda for each type T
    template <typename F>
    static constexpr void for_each_type(F f)
    {
        (f.template operator()<Ts>(), ...);
    }

    // Helper to check if a type is in this archetype
    template <typename T>
    static constexpr auto contains() -> bool {
        return (std::is_same_v<T, Ts> || ...);
    }

    // Get the count of components in this archetype
    static constexpr auto size() -> u8 { return sizeof...(Ts); }
};

// usage:
using PlayerArchetype   = Archetype<Transform2, RigidBody2>;



