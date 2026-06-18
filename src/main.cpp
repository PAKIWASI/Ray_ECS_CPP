#include "common.hpp"
#include "world.hpp"



auto main() -> int
{
    World world {};

    Entity e = world.create_from_archetype<PlayerArchetype>();


    return 0;
}
