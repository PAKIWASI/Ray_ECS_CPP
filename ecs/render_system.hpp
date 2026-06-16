#include "ecs.hpp"
#include "components.hpp"
#include <raylib.h>

using namespace ECS_COMPS_2D;


class RenderSystem : public ISystem
{
  private:
    static auto make_signature() -> Signature
    {
        Signature sig;
        sig.set(ComponentManager::get_component_id<Transform2>());
        return sig;
    }

  public:
    RenderSystem(const ComponentManager& cm)
        : ISystem(make_signature(), cm) {}

    void update(float dt) override
    {
        for (Entity e : entities)
        {
            auto& transform = component_manager.get_component<Transform2>(e);
            DrawCircle(
                (int)transform.pos.x,
                (int)transform.pos.y,
                10.0F,
                RED
            );
        }
    }
};
