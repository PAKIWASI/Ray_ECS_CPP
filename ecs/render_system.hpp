#include "ecs.hpp"
#include "components.hpp"
#include <raylib.h>

using namespace ECS_COMPS_2D;


class RenderSystem : public ISystem
{
  public:
    RenderSystem(const ComponentManager& cm)
        : ISystem{make_signature()}
        , transforms{cm.get_arr<Transform2>()}
    {}


    void update(float dt) override
    {
    }


  private:
    ComponentArray<Transform2>& transforms;

    static auto make_signature() -> Signature
    {
        Signature sig;
        sig.set(ComponentManager::get_component_id<Transform2>());
        return sig;
    }
};
