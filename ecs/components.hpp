#pragma once


namespace ECS_COMPS
{


struct Vec2 {
    float x, y;
};

struct Gravity {
    Vec2 force;
};

struct RigidBody {
    Vec2 v;
    Vec2 a;
};

struct Transform {
    Vec2 pos;
    Vec2 rot;
    Vec2 scale;
};


}
