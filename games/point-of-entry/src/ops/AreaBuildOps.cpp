#include "ops/AreaBuildOps.h"

#include "AreaLoader.h"
#include "SpriteDefLoader.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/SpawnUtils.h"

namespace area_build
{

void registerAll()
{
    // The rest spot, exactly as the generated floor plants it: a pale ring of
    // floor under everything that walks.
    area::registerBuilder("rest_spot",
                          [](EntityManager& em, const area::Object& o)
                          {
                              const entt::entity spot =
                                  spawn::box(em, o.x, o.y + 8.0f, 22.0f, 0.30f, 0.42f, 0.40f);
                              em.registry().emplace<RestSpot>(spot, RestSpot{40.0f});
                              em.registry().get<Sprite>(spot).layer = 1;
                          });

    // A prop: sprite art when the file names some, a sized box when it does
    // not, and a foot collider when it declares itself solid.
    area::registerBuilder("prop",
                          [](EntityManager& em, const area::Object& o)
                          {
                              const auto size = o.props.value("size", 24.0f);
                              const entt::entity e =
                                  spawn::box(em, o.x, o.y, size, 0.55f, 0.45f, 0.35f);
                              if (const sprite_def::Def def =
                                      sprite_def::load(o.props.value("sprite", std::string{}));
                                  def.ok)
                              {
                                  auto& spr = em.registry().get<Sprite>(e);
                                  spr.texture_path = def.sheet;
                                  spr.src_w = def.frame_w;
                                  spr.src_h = def.frame_h;
                                  em.registry().remove<SolidColor>(e);
                              }
                              if (o.props.value("solid", false))
                                  em.registry().emplace<Collider>(e, Collider{size, size * 0.5f});
                          });
}

} // namespace area_build
