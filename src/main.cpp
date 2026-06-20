#include "common.hpp"
#include "game_registry.hpp"


auto main() -> int
{
    GameWorld game {};
    Entity player = game.create_from_archetype<PlayerArchetype>();

    game.update(1.0, MOVEMENT_ONLY);

    return 0;
}
