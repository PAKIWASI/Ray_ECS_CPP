#pragma once

#include "ecs.hpp"
#include <cstdint>
#include <raylib.h>


namespace ECS_Components {


// Spatial

struct Transform {
    float x        = 0.0F;
    float y        = 0.0F;
    float rotation = 0.0F; // degrees
    float scale    = 1.0F;
};

struct Velocity {
    float dx = 0.0F;
    float dy = 0.0F;
};


// Combat

struct Health {
    float current = 100.0F;
    float maximum = 100.0F;

    [[nodiscard]] auto  is_dead() const -> bool { return current <= 0.0F; }
    [[nodiscard]] auto percentage() const -> float { return current / maximum; }
};

struct Damage {
    float amount = 10.0F;
};


// Rendering

struct Sprite {
    Color color  = WHITE;
    float width  = 32.0F;
    float height = 32.0F;
};

struct CircleShape {
    float radius = 16.0F;
    Color color  = RED;
};


// Gameplay

struct Player {
    float    speed = 200.0F;
    uint32_t score = 0;
};

struct Enemy {
    float  speed        = 80.0F;
    Entity target       = 0; // which entity to chase
    float  attack_timer = 0.0F;
    float  attack_rate  = 1.0F; // attacks per second
};

struct Lifetime {
    float remaining = 5.0F; // seconds
};

struct Collider {
    float radius = 16.0F; // circle collider
};


// Tag components (zero-size, mark entities)

struct TagPlayer {};
struct TagEnemy {};
struct TagBullet {};

} // namespace ECS_Components
