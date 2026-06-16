#pragma once

#include "wasi.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <concepts>
#include <flat_set>
#include <memory>
#include <utility>
#include <vector>


// ===================
// ECS core
// ===================


using namespace wasi;


// TODO: research this:
// currently we have ComponentArrays where each is pre inited with MAX_ENTITIES slots
// is this good for large systems or should i do a swap-delete system and maps
// with entity keys and array index values ?


// Configuration
// ==============

// the number of entities can exist in the world at a time
// this means we have `MAX_ENTITIES` slots in each component array
constexpr u32 MAX_ENTITIES  = 1000;

// total number of components that we can register
// The container in component manager holds `MAX_COMPONENTS` component arrays
constexpr u8 MAX_COMPONENTS = 32;

constexpr u8 MAX_SYSTEMS    = 16;


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


// Entity Manager
// ===============

// creates and deletes entities
// gives out ids and takes them back
class EntityManager
{
  private:
    // no startup cost of pre filling container with ids
    Entity next_id = 0;
    // when an entity dies, it's id goes here for reuse
    std::vector<Entity> free_ids;

    std::array<Signature, MAX_ENTITIES> signatures;
  public:

    [[nodiscard]] auto create() -> Entity
    {
        if (!free_ids.empty()) {
            Entity recycled = free_ids.back();
            free_ids.pop_back();
            return recycled;
        }
        assert(next_id < MAX_ENTITIES && "MAX_ENTITIES reached");
        return next_id++;
    }

    void destroy(Entity e)
    {
        // max value of next_id is MAX_ENTITIES
        assert(e < next_id && "Entity out of range");
        // reset the signature for later use
        signatures.at(e).reset();
        free_ids.emplace_back(e);
    }

    // TODO: how will i be passing component types to this?
    // maybe i store a map ComponentArray<T>  -> idx in compmanager?
    // or easier, pass the output of get_component_id, which we already have in compmanager
    void set_component(Entity e, ComponentType c)
    {
        assert(e < next_id && "Entity out of range");
        assert(c < MAX_COMPONENTS && "Component out of range");
        signatures.at(e).set(c);
    }

    auto has_component(Entity e, ComponentType c) -> bool
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


// Type Erasure
// =============
// we need to store different ComponentArray<T> in the same container
// we have a interface with type agnostic methods. container will have that type
// when we need type specific stuff, we will cast to the correct type

// The Rule of Five warning fires because you declared a destructor but nothing else
// Since ICompArr is a pure interface not meant to be copied or moved, explicitly delete those
struct ICompArr
{
    ICompArr()                                   = default;
    ICompArr(const ICompArr&)                    = delete;
    ICompArr(ICompArr&&)                         = delete;
    auto operator=(const ICompArr&) -> ICompArr& = delete;
    auto operator=(ICompArr&&) -> ICompArr&      = delete;

    virtual ~ICompArr()                          = default; // one vtable entry
    // the only reason entity_destroyed is virtual is because ComponentManager holds ICompArr*
    // and needs to call something through the base pointer without knowing T
    virtual void entity_destroyed(Entity e)      = 0;       // another vtable entry
};


// Concept: what counts as a component
template <typename T>
concept ComponentType_t = std::default_initializable<T> // each comp arr is pre-initialized
                       && std::movable<T>;              // we move data into the arr


// Templated subclass, this is the type scpecif ComponentArray<T>
// The concept check happens here for each T
template <ComponentType_t T>
class ComponentArray : public ICompArr  // class does private inheritance by default
{
  private:
    std::array<T, MAX_ENTITIES> data{}; // default initialize
  public:

    void add_data(Entity e, T comp)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        data.at(e) = std::move(comp);
    }

    void remove_data(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        // The old object's destructor runs as part of the assignment operator
        data.at(e) = T{};  // then calls default constructor for T
    }

    [[nodiscard]] auto get_data(Entity e) -> T&
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        return data.at(e);
    }

    void entity_destroyed(Entity e) override
    {
        remove_data(e);
    }
};


class ComponentManager
{
  private:
    // we need unique ptr as we can't store an abstract type in an array directly
    // it's not instantiable, and it would slice anyway
    std::array<u_ptr<ICompArr>, MAX_COMPONENTS> comp_arrays{};  // array of pointers

    // handing out IDs to each type of component
    // first call to get_component_id<T>() with a new T yields the next id
    // all subsequent calls give the same id (static var)
    // This id determines the index into comp_arrays and also the signature bit position
    inline static ComponentType next_comp_id = 0;


    // for type specific operations, we need to cast down to derived class
    template <ComponentType_t T>
    [[nodiscard]] auto get_arr() -> ComponentArray<T>&
    {
        ComponentType id = get_component_id<T>();
        assert(comp_arrays[id] != nullptr && "Component not registered");
        // comp_arrays.at(id) returns a unique_ptr<ICompArr>&
        // * dereferences the unique_ptr giving you ICompArr&, then static_cast casts that down to ComponentArray<T>&.
        return static_cast<ComponentArray<T>&>(*comp_arrays.at(id));
    }

  public:

    // fill a spot in the component array with a component of type T
    template <ComponentType_t T>
    void register_component()
    {
        ComponentType id = get_component_id<T>();
        assert(comp_arrays.at(id) == nullptr && "Component already registered");
        // we store base class pointer but pass a derived class object pointer
        // no explicit cast required
        comp_arrays.at(id) = std::make_unique<ComponentArray<T>>();
    }

    // type specifc operations
    // we assert in get_arr<>()

    // register_component allocates the ComponentArray<T> and add_component puts data into a slot in that array
    template <ComponentType_t T>
    void add_component(Entity e, T comp) // copy or move, then move into array
    {
        get_arr<T>().add_data(e, std::move(comp));
    }

    template <ComponentType_t T>
    void remove_component(Entity e)
    {
        get_arr<T>().remove_data(e);
    }

    template <ComponentType_t T>
    [[nodiscard]] auto get_component(Entity e) -> T&
    {
        return get_arr<T>().get_data(e);
    }

    template <ComponentType_t T>
    [[nodiscard]] static auto get_component_id() -> ComponentType
    {
        // static var inside func initialized only once
        static ComponentType id = next_comp_id++;
        assert(id < MAX_COMPONENTS && "MAX_COMPONENTS reached");
        return id;
    }

    // generic operation, no typecast needed. we do a vtable lookup
    void entity_destroyed(Entity e)
    {
        for (const auto& comparr : comp_arrays)
        {
            if (comparr) {
                comparr->entity_destroyed(e);
            }
        }

    }
};


// System
// =======
// A system is any functionality that iterates upon a list of entities with a certain signature of components
// Each system has a set of entities it acts on and a signature
// Each system inherits from ISystem and implements the interface contract and gets the necessay members
// We store ISystem pointers in SystemManager and call methods through the interface (type erasure)

class ISystem
{
  protected:
    std::flat_set<Entity> entities{};
    const Signature signature;

  public:
    ISystem(const ISystem&)                     = delete;
    ISystem(ISystem&&)                          = delete;
    auto operator=(const ISystem&) -> ISystem&  = delete;
    auto operator=(ISystem&&) -> ISystem&       = delete;

    // we also want system specific init behaviour
    // systems should make a subclass constructor
    // Then they should call base class constructor with signature arg
    // If you don't call it explicitly, the compiler looks for a default constructor on ISystem
    // which doesn't exist as we defined a parameterized one
    // It will fail to compile and force every system to declare its signature.

    virtual ~ISystem() = default;

    // each system has unique update
    virtual void update(float dt) = 0;

    ISystem(Signature sig): signature(sig) {}


    // TODO: the caller to this matches the signature with entity's signature

    void add_entity(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        assert(!entities.contains(e) && "Entity already in System");
        entities.emplace(e);
    }

    void remove_entity(Entity e)
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        assert(entities.contains(e) && "Entity is not in System");
        entities.erase(e);
    }

    auto has_entity(Entity e) -> bool
    {
        assert(e < MAX_ENTITIES && "Entity out of range");
        return entities.contains(e);
    }

    [[nodiscard]] auto get_signature() -> Signature { return signature; }
};


// System concept: It must derive from ISystem
template <typename T>
concept SystemType_t = std::derived_from<T, ISystem>;


class SystemManager
{
  private:
    std::array<u_ptr<ISystem>, MAX_SYSTEMS> systems;    // array of pointers to ISystem

    inline static SystemType next_sys_id = 0;

  public:

    template <SystemType_t T>
    auto get_system_id() -> SystemType
    {
        static SystemType id = next_sys_id++;
        assert(id < MAX_SYSTEMS && "MAX_SYSTEMS reached");
        return id;
    }

    template <SystemType_t T, typename... Args>
    void register_system(Args&&... args)
    {
        SystemType id = get_system_id<T>();
        assert(systems.at(id) == nullptr && "Component already registered");
        // we store base class pointer but pass a derived class object pointer
        systems.at(id) = std::make_unique<T>(std::forward<Args>(args)...);  // preserves rvals and lvals
        // T needs to be default constructable but it' not, we need to pass a signature in the constructor
        // So we will do perfect forwarding
    }
    // There is no data to add, each system has a unique update() and a signature and we will add entities to it


    // When an entity's signature changes, see if we need to remove it from
    // some system by matching the signature again
    void on_signature_change(Entity e, Signature new_sig)
    {
        for (const auto& sys : systems) {
            if (!sys) { continue; }
            Signature curr_sig  = sys->get_signature();
            bool qualifies = (curr_sig & new_sig) == curr_sig;
            bool has       = sys->has_entity(e);

            if (qualifies && !has)      { sys->add_entity(e); }
            else if (!qualifies && has) { sys->remove_entity(e); }
        }
    }

    void update(float dt)
    {
        for (const auto& sys : systems) {
            sys->update(dt);
        }
    }

    // TODO: will we ever need to only update specific systems and not others per frame?
};
