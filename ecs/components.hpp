#pragma once


namespace ECS_COMPS_2D
{


struct Vec2 {
    float x, y;
};

struct Gravity2 {
    Vec2 force;
};

struct RigidBody2 {
    Vec2 v;
    Vec2 a;
};

struct Transform2 {
    Vec2 pos;
    Vec2 rot;
    Vec2 scale;
};


}
