#include <cstdint>
#include <raylib.h>



auto main() -> int
{
    const uint32_t w = 600;
    const uint32_t h = 400;

    InitWindow(w, h, "hello");

    while (!WindowShouldClose())
    {
        BeginDrawing();

        ClearBackground(RAYWHITE);

        EndDrawing();
    }
    return 0;
}

