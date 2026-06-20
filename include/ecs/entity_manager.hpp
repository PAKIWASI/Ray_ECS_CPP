#pragma once

#include "common.hpp"

#include <array>
#include <cassert>
#include <vector>


// EntityManager
// ==============
// Issues and recycles entity IDs. Stores per-entity component signatures.
//
// IDs are uint32_t indices into component arrays. Destroyed IDs go into free_ids
// for reuse. Signatures are a bitset of which components the entity has —
// maintained here so SystemManager can check qualification without touching
// component arrays.

class EntityManager
{
  private:
    Entity next_id = 0;
    std::vector<Entity>              free_ids;
    std::array<Signature, MAX_ENTITIES> signatures{};

  public:

    EntityManager() {
        free_ids.reserve(PRE_INIT_SIZE);
    }

    [[nodiscard]] auto create(Signature sig) -> Entity
    {
        Entity chosen{};
        if (!free_ids.empty()) {
            chosen = free_ids.back();
            free_ids.pop_back();
        } else {
            assert(next_id < MAX_ENTITIES && "MAX_ENTITIES reached");
            chosen = next_id++;
        }

        signatures.at(chosen) = sig;
        return chosen;
    }

    void destroy(Entity e)
    {
        // second check prevents double destroy of entities
        // entity is always initilized with atleast one component
        assert(e < next_id && !signatures.at(e).none()
               && "Entity out of range");
        signatures.at(e).reset();
        free_ids.emplace_back(e);
    }

    void set_component(Entity e, ComponentType c)
    {
        assert(e < next_id        && "Entity out of range");
        assert(c < MAX_COMPONENTS && "Component out of range");
        signatures.at(e).set(c);
    }

    void unset_component(Entity e, ComponentType c)
    {
        assert(e < next_id        && "Entity out of range");
        assert(c < MAX_COMPONENTS && "Component out of range");
        signatures.at(e).reset(c);
    }

    [[nodiscard]] auto has_component(Entity e, ComponentType c) const -> bool
    {
        assert(e < next_id        && "Entity out of range");
        assert(c < MAX_COMPONENTS && "Component out of range");
        return signatures.at(e).test(c);
    }

    void set_signature(Entity e, Signature sig)
    {
        assert(e < next_id && "Entity out of range");
        signatures.at(e) = sig;
    }

    [[nodiscard]] auto get_signature(Entity e) const -> Signature
    {
        assert(e < next_id && "Entity out of range");
        return signatures.at(e);
    }
};
