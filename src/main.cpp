#include <print>
#include <raylib.h>
#include "ecs.hpp"




auto main() -> int
{
    World world{};

    Entity e = world.create_entity();

    std::println("{}", e);

    world.update(1);

    world.register_component<Transform>();

    std::println("{}", World::get_component_id<Transform>());

    return 0;
};

