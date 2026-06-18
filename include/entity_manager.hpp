#pragma once

#include "common.hpp"

#include <cassert>
#include <vector>
#include <array>


class EntityManager
{
  private:
    // no startup cost of pre filling container with ids
    Entity next_id = 0;
    // when an entity dies, it's id goes here for reuse
    std::vector<Entity> free_ids;

    std::array<Signature, MAX_ENTITIES> signatures{};
  public:

    EntityManager() {
        free_ids.reserve(PRE_INIT_SIZE);
    }

    [[nodiscard]] auto create(Signature sig = 0) -> Entity
    {
        Entity chosen {};
        if (!free_ids.empty()) {
            chosen = free_ids.back();
            free_ids.pop_back();
        } else {
            assert(next_id < MAX_ENTITIES && "MAX_ENTITIES reached");
            chosen = next_id++;
        }

        if (sig != 0) {
            signatures.at(chosen) = sig;
        }

        return chosen;
    }

    void destroy(Entity e)
    {
        // max value of next_id is MAX_ENTITIES
        assert(e < next_id && "Entity out of range");
        // reset the signature for later use
        signatures.at(e).reset();
        free_ids.emplace_back(e);
    }

    void set_component(Entity e, ComponentType c)
    {
        assert(e < next_id && "Entity out of range");
        assert(c < MAX_COMPONENTS && "Component out of range");
        signatures.at(e).set(c);
    }

    void unset_component(Entity e, ComponentType c)
    {
        assert(e < next_id && "Entity out of range");
        assert(c < MAX_COMPONENTS && "Component out of range");
        signatures.at(e).reset(c);
    }

    [[nodiscard]] auto has_component(Entity e, ComponentType c) const -> bool
    {
        assert(e < next_id && "Entity out of range");
        assert(c < MAX_COMPONENTS && "Component out of range");
        return signatures.at(e).test(c);
    }

    void set_signature(Entity e, Signature sig)
    {
        assert(e < next_id && "Entity out of range");
        signatures.at(e) = sig;
    }

    [[nodiscard]] auto get_signature(Entity e) -> Signature
    {
        assert(e < next_id && "Entity out of range");
        return signatures.at(e);
    }

};


