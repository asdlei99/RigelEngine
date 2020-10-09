/* Copyright (C) 2016, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "entity_factory.hpp"

#include "base/container_utils.hpp"
#include "data/game_traits.hpp"
#include "data/unit_conversions.hpp"
#include "engine/life_time_components.hpp"
#include "engine/physics_system.hpp"
#include "engine/random_number_generator.hpp"
#include "engine/sprite_tools.hpp"
#include "game_logic/actor_tag.hpp"
#include "game_logic/behavior_controller.hpp"
#include "game_logic/collectable_components.hpp"
#include "game_logic/damage_components.hpp"
#include "game_logic/dynamic_geometry_components.hpp"
#include "game_logic/effect_actor_components.hpp"
#include "game_logic/effect_components.hpp"
#include "game_logic/enemies/big_green_cat.hpp"
#include "game_logic/enemies/blue_guard.hpp"
#include "game_logic/enemies/bomber_plane.hpp"
#include "game_logic/enemies/boss_episode_1.hpp"
#include "game_logic/enemies/boss_episode_2.hpp"
#include "game_logic/enemies/boss_episode_3.hpp"
#include "game_logic/enemies/boss_episode_4.hpp"
#include "game_logic/enemies/ceiling_sucker.hpp"
#include "game_logic/enemies/enemy_rocket.hpp"
#include "game_logic/enemies/eyeball_thrower.hpp"
#include "game_logic/enemies/flame_thrower_bot.hpp"
#include "game_logic/enemies/floating_laser_bot.hpp"
#include "game_logic/enemies/grabber_claw.hpp"
#include "game_logic/enemies/green_bird.hpp"
#include "game_logic/enemies/hover_bot.hpp"
#include "game_logic/enemies/laser_turret.hpp"
#include "game_logic/enemies/messenger_drone.hpp"
#include "game_logic/enemies/prisoner.hpp"
#include "game_logic/enemies/red_bird.hpp"
#include "game_logic/enemies/rigelatin_soldier.hpp"
#include "game_logic/enemies/rocket_turret.hpp"
#include "game_logic/enemies/security_camera.hpp"
#include "game_logic/enemies/simple_walker.hpp"
#include "game_logic/enemies/slime_blob.hpp"
#include "game_logic/enemies/small_flying_ship.hpp"
#include "game_logic/enemies/snake.hpp"
#include "game_logic/enemies/spider.hpp"
#include "game_logic/enemies/spike_ball.hpp"
#include "game_logic/enemies/spiked_green_creature.hpp"
#include "game_logic/enemies/unicycle_bot.hpp"
#include "game_logic/enemies/wall_walker.hpp"
#include "game_logic/enemies/watch_bot.hpp"
#include "game_logic/hazards/lava_fountain.hpp"
#include "game_logic/hazards/slime_pipe.hpp"
#include "game_logic/hazards/smash_hammer.hpp"
#include "game_logic/interactive/blowing_fan.hpp"
#include "game_logic/interactive/elevator.hpp"
#include "game_logic/interactive/enemy_radar.hpp"
#include "game_logic/interactive/force_field.hpp"
#include "game_logic/interactive/item_container.hpp"
#include "game_logic/interactive/locked_door.hpp"
#include "game_logic/interactive/missile.hpp"
#include "game_logic/interactive/respawn_checkpoint.hpp"
#include "game_logic/interactive/sliding_door.hpp"
#include "game_logic/interactive/super_force_field.hpp"
#include "game_logic/interactive/tile_burner.hpp"
#include "game_logic/player/ship.hpp"
#include "game_logic/trigger_components.hpp"

#include <tuple>
#include <utility>


namespace ex = entityx;


namespace rigel::game_logic {

using namespace data;
using namespace loader;
using namespace std;

using data::ActorID;

using namespace engine::components;
using namespace game_logic::components;


namespace {

// Assign gravity affected moving body component
template<typename EntityLike>
void addDefaultMovingBody(
  EntityLike& entity,
  const BoundingBox& boundingBox
) {
  using namespace engine::components::parameter_aliases;

  entity.template assign<MovingBody>(
    MovingBody{Velocity{0.0f, 0.0f}, GravityAffected{true}});
  entity.template assign<BoundingBox>(boundingBox);
  entity.template assign<ActivationSettings>(
    ActivationSettings::Policy::AlwaysAfterFirstActivation);
}


auto toPlayerProjectileType(const ProjectileType type) {
  using PType = components::PlayerProjectile::Type;
  static_assert(
    int(PType::Normal) == int(ProjectileType::PlayerRegularShot) &&
    int(PType::Laser) == int(ProjectileType::PlayerLaserShot) &&
    int(PType::Rocket) == int(ProjectileType::PlayerRocketShot) &&
    int(PType::Flame) == int(ProjectileType::PlayerFlameShot) &&
    int(PType::ShipLaser) == int(ProjectileType::PlayerShipLaserShot) &&
    int(PType::ReactorDebris) == int(ProjectileType::ReactorDebris));
  return static_cast<PType>(static_cast<int>(type));
}


const base::Point<float> FLY_RIGHT[] = {
  {3.0f, 0.0f},
  {3.0f, 0.0f},
  {3.0f, 0.0f},
  {2.0f, 0.0f},
  {2.0f, 1.0f},
  {2.0f, 1.0f},
  {2.0f, 2.0f},
  {1.0f, 2.0f},
  {1.0f, 3.0f},
  {1.0f, 3.0f}
};


const base::Point<float> FLY_UPPER_RIGHT[] = {
  {3.0f, -3.0f},
  {2.0f, -2.0f},
  {2.0f, -1.0f},
  {1.0f,  0.0f},
  {1.0f,  0.0f},
  {1.0f,  1.0f},
  {1.0f,  2.0f},
  {1.0f,  2.0f},
  {1.0f,  3.0f},
  {1.0f,  3.0f}
};


const base::Point<float> FLY_UP[] = {
  {0.0f, -3.0f},
  {0.0f, -2.0f},
  {0.0f, -2.0f},
  {0.0f, -1.0f},
  {0.0f, 0.0f},
  {0.0f, 1.0f},
  {0.0f, 1.0f},
  {0.0f, 2.0f},
  {0.0f, 3.0f},
  {0.0f, 3.0f}
};


const base::Point<float> FLY_UPPER_LEFT[] = {
  {-3.0f, -3.0f},
  {-2.0f, -2.0f},
  {-2.0f, -1.0f},
  {-1.0f, 0.0f},
  {-1.0f, 0.0f},
  {-1.0f, 1.0f},
  {-1.0f, 2.0f},
  {-1.0f, 3.0f},
  {-1.0f, 4.0f},
  {-1.0f, 4.0f}
};


const base::Point<float> FLY_LEFT[] = {
  {-3.0f, 0.0f},
  {-3.0f, 0.0f},
  {-3.0f, 0.0f},
  {-2.0f, 0.0f},
  {-2.0f, 1.0f},
  {-2.0f, 1.0f},
  {-2.0f, 2.0f},
  {-1.0f, 3.0f},
  {-1.0f, 3.0f},
  {-1.0f, 3.0f}
};


const base::Point<float> FLY_DOWN[] = {
  {0.0f, 1.0f},
  {0.0f, 2.0f},
  {0.0f, 2.0f},
  {0.0f, 2.0f},
  {0.0f, 3.0f},
  {0.0f, 3.0f},
  {0.0f, 3.0f},
  {0.0f, 3.0f},
  {0.0f, 3.0f},
  {0.0f, 3.0f}
};


const base::Point<float> SWIRL_AROUND[] = {
  {-2.0f, 1.0f},
  {-2.0f, 1.0f},
  {-2.0f, 1.0f},
  {-1.0f, 1.0f},
  {0.0f, 1.0f},
  {1.0f, 1.0f},
  {2.0f, 0.0f},
  {1.0f, -1.0f},
  {-2.0f, -1.0f},
  {-2.0f, 1.0f}
};


const base::ArrayView<base::Point<float>> MOVEMENT_SEQUENCES[] = {
  FLY_RIGHT,
  FLY_UPPER_RIGHT,
  FLY_UP,
  FLY_UPPER_LEFT,
  FLY_LEFT,
  FLY_DOWN,
  SWIRL_AROUND
};


auto createFrameDrawData(
  const loader::ActorData::Frame& frameData,
  renderer::Renderer* pRenderer
) {
  auto texture = renderer::OwningTexture{pRenderer, frameData.mFrameImage};
  return engine::SpriteFrame{std::move(texture), frameData.mDrawOffset};
}


void applyTweaks(
  std::vector<engine::SpriteFrame>& frames,
  const ActorID actorId,
  const std::vector<loader::ActorData>& actorParts,
  renderer::Renderer* pRenderer
) {
  // Some sprites in the game have offsets that would require more complicated
  // code to draw them correctly. To simplify that, we adjust the offsets once
  // at loading time so that no additional adjustment is necessary at run time.

  // Player sprite
  if (actorId == ActorID::Duke_LEFT || actorId == ActorID::Duke_RIGHT) {
    for (int i=0; i<39; ++i) {
      if (i != 35 && i != 36) {
        frames[i].mDrawOffset.x -= 1;
      }
    }
  }

  // Destroyed reactor fire
  if (actorId == ActorID::Reactor_fire_LEFT || actorId == ActorID::Reactor_fire_RIGHT) {
    frames[0].mDrawOffset.x = 0;
  }

  // Radar computer
  if (actorId == ActorID::Radar_computer_terminal) {
    for (auto i = 8u; i < frames.size(); ++i) {
      frames[i].mDrawOffset.x -= 1;
    }
  }

  // Duke's ship
  if (
    actorId == ActorID::Dukes_ship_LEFT ||
    actorId == ActorID::Dukes_ship_RIGHT ||
    actorId == ActorID::Dukes_ship_after_exiting_LEFT ||
    actorId == ActorID::Dukes_ship_after_exiting_RIGHT
  ) {
    // The incoming frame list is based on IDs 87, 88, and 92. The frames
    // are laid out as follows:
    //
    //  0, 1: Duke's ship, facing right
    //  2, 3: Duke's ship, facing left
    //  4, 5: exhaust flames, facing down
    //  6, 7: exhaust flames, facing left
    //  8, 9: exhaust flames, facing right
    //
    // In order to display the down facing exhaust flames correctly when
    // Duke's ship is facing left, we need to apply an additional X offset to
    // frames 4 and 5. But currently, RigelEngine doesn't support changing the
    // X offset temporarily, so we need to first create a copy of those frames,
    // insert them after 8 and 9, and then adjust their offset.
    //
    // After this tweak, the frame layout is as follows:
    //
    //   0,  1: Duke's ship, facing right
    //   2,  3: Duke's ship, facing left
    //   4,  5: exhaust flames, facing down, x-offset for facing left
    //   6,  7: exhaust flames, facing left
    //   8,  9: exhaust flames, facing down, x-offset for facing right
    //  10, 11: exhaust flames, facing right
    frames.insert(
      frames.begin() + 8,
      createFrameDrawData(actorParts[2].mFrames[0], pRenderer));
    frames.insert(
      frames.begin() + 9,
      createFrameDrawData(actorParts[2].mFrames[1], pRenderer));

    frames[8].mDrawOffset.x += 1;
    frames[9].mDrawOffset.x += 1;
  }
}


std::optional<int> orientationOffsetForActor(const ActorID actorId) {
  switch (actorId) {
    case ActorID::Duke_LEFT:
    case ActorID::Duke_RIGHT:
      return 39;

    case ActorID::Snake:
      return 9;

    case ActorID::Eyeball_thrower_LEFT:
      return 10;

    case ActorID::Skeleton:
      return 4;

    case ActorID::Spider:
      return 13;

    case ActorID::Red_box_turkey:
      return 2;

    case ActorID::Rigelatin_soldier:
      return 4;

    case ActorID::Ugly_green_bird:
      return 3;

    case ActorID::Big_green_cat_LEFT:
    case ActorID::Big_green_cat_RIGHT:
      return 3;

    case ActorID::Spiked_green_creature_LEFT:
    case ActorID::Spiked_green_creature_RIGHT:
      return 6;

    case ActorID::Unicycle_bot:
      return 4;

    case ActorID::Dukes_ship_LEFT:
    case ActorID::Dukes_ship_RIGHT:
    case ActorID::Dukes_ship_after_exiting_LEFT:
    case ActorID::Dukes_ship_after_exiting_RIGHT:
      return 6;

    default:
      return std::nullopt;
  }
}


int SPIDER_FRAME_MAP[] = {
  3, 4, 5, 9, 10, 11, 6, 8, 9, 14, 15, 12, 13, // left
  0, 1, 2, 6, 7, 8, 6, 8, 9, 12, 13, 14, 15, // right
};


int UNICYCLE_FRAME_MAP[] = {
  0, 5, 1, 2, // left
  0, 5, 3, 4, // right
};


int DUKES_SHIP_FRAME_MAP[] = {
  0, 1, 10, 11, 8, 9, // left
  2, 3, 6, 7, 4, 5, // right
};


base::ArrayView<int> frameMapForActor(const ActorID actorId) {
  switch (actorId) {
    case ActorID::Spider:
      return base::ArrayView<int>(SPIDER_FRAME_MAP);

    case ActorID::Unicycle_bot:
      return base::ArrayView<int>(UNICYCLE_FRAME_MAP);

    case ActorID::Dukes_ship_LEFT:
    case ActorID::Dukes_ship_RIGHT:
    case ActorID::Dukes_ship_after_exiting_LEFT:
    case ActorID::Dukes_ship_after_exiting_RIGHT:
      return base::ArrayView<int>(DUKES_SHIP_FRAME_MAP);

    default:
      return {};
  }
}

}


#include "entity_configuration.ipp"


SpriteFactory::SpriteFactory(
  renderer::Renderer* pRenderer,
  const ActorImagePackage* pSpritePackage
)
  : mpRenderer(pRenderer)
  , mpSpritePackage(pSpritePackage)
{
}


Sprite SpriteFactory::createSprite(const ActorID mainId) {
  auto iData = mSpriteDataCache.find(mainId);
  if (iData == mSpriteDataCache.end()) {
    engine::SpriteDrawData drawData;

    int lastDrawOrder = 0;
    int lastFrameCount = 0;
    std::vector<int> framesToRender;

    const auto actorPartIds = actorIDListForActor(mainId);
    const auto actorParts = utils::transformed(actorPartIds,
      [&](const ActorID partId) {
        return mpSpritePackage->loadActor(partId);
      });

    for (const auto& actorData : actorParts) {
      lastDrawOrder = actorData.mDrawIndex;

      for (const auto& frameData : actorData.mFrames) {
        drawData.mFrames.emplace_back(
          createFrameDrawData(frameData, mpRenderer));
      }

      framesToRender.push_back(lastFrameCount);
      lastFrameCount = int(actorData.mFrames.size());
    }

    drawData.mOrientationOffset = orientationOffsetForActor(mainId);
    drawData.mVirtualToRealFrameMap = frameMapForActor(mainId);
    drawData.mDrawOrder = adjustedDrawOrder(mainId, lastDrawOrder);

    applyTweaks(drawData.mFrames, mainId, actorParts, mpRenderer);

    iData = mSpriteDataCache.emplace(
      mainId,
      SpriteData{std::move(drawData), std::move(framesToRender)}
    ).first;
  }

  const auto& data = iData->second;
  return {&data.mDrawData, data.mInitialFramesToRender};
}


base::Rect<int> SpriteFactory::actorFrameRect(
  const data::ActorID id,
  const int frame
) const {
  return mpSpritePackage->actorFrameRect(id, frame);
}


EntityFactory::EntityFactory(
  engine::ISpriteFactory* pSpriteFactory,
  ex::EntityManager* pEntityManager,
  engine::RandomNumberGenerator* pRandomGenerator,
  const data::Difficulty difficulty)
  : mpSpriteFactory(pSpriteFactory)
  , mpEntityManager(pEntityManager)
  , mpRandomGenerator(pRandomGenerator)
  , mDifficulty(difficulty)
{
}


Sprite EntityFactory::createSpriteForId(const ActorID actorID) {
  auto sprite = mpSpriteFactory->createSprite(actorID);
  configureSprite(sprite, actorID);
  return sprite;
}


entityx::Entity EntityFactory::createSprite(
  const data::ActorID actorID,
  const bool assignBoundingBox
) {
  auto entity = mpEntityManager->create();
  auto sprite = createSpriteForId(actorID);
  entity.assign<Sprite>(sprite);

  if (assignBoundingBox) {
    entity.assign<BoundingBox>(engine::inferBoundingBox(sprite, entity));
  }
  return entity;
}

entityx::Entity EntityFactory::createSprite(
  const data::ActorID actorID,
  const base::Vector& position,
  const bool assignBoundingBox
) {
  auto entity = createSprite(actorID, assignBoundingBox);
  entity.assign<WorldPosition>(position);
  return entity;
}


entityx::Entity EntityFactory::createProjectile(
  const ProjectileType type,
  const WorldPosition& pos,
  const ProjectileDirection direction
) {
  auto entity = createActor(actorIdForProjectile(type, direction), pos);
  entity.assign<Active>();

  configureProjectile(
    entity,
    type,
    pos,
    direction,
    *entity.component<BoundingBox>());

  return entity;
}


entityx::Entity EntityFactory::createActor(
  const data::ActorID id,
  const base::Vector& position
) {
  auto entity = createSprite(id, position);
  auto& sprite = *entity.component<Sprite>();
  const auto boundingBox = engine::inferBoundingBox(sprite, entity);

  configureEntity(entity, id, boundingBox);

  return entity;
}


void EntityFactory::configureProjectile(
  entityx::Entity entity,
  const ProjectileType type,
  WorldPosition position,
  const ProjectileDirection direction,
  const BoundingBox& boundingBox
) {
  using namespace engine::components::parameter_aliases;
  using namespace game_logic::components::parameter_aliases;

  const auto isGoingLeft = direction == ProjectileDirection::Left;

  // Position adjustment for the flame thrower shot
  if (type == ProjectileType::PlayerFlameShot) {
    if (isHorizontal(direction)) {
      position.y += 1;
    } else {
      position.x -= 1;
    }
  }

  // Position adjustment for left-facing projectiles. We want the incoming
  // position to always represent the projectile's origin, which means we need
  // to adjust the position by the projectile's length to match the left-bottom
  // corner positioning system.
  if (isHorizontal(direction) && isGoingLeft) {
    position.x -= boundingBox.size.width - 1;

    if (type == ProjectileType::PlayerFlameShot) {
      position.x += 3;
    }
  }

  *entity.component<WorldPosition>() = position;

  const auto speed = speedForProjectileType(type);
  const auto damageAmount = damageForProjectileType(type);

  // TODO: The way projectile creation works needs an overhaul, it's quite
  // messy and convoluted right now. Having this weird special case here
  // for rockets is the easiest way to add rockets without doing the full
  // refactoring, which is planned for later.
  //
  // See configureEntity() for the rocket configuration.
  if (
    type == ProjectileType::EnemyRocket ||
    type == ProjectileType::EnemyBossRocket
  ) {
    return;
  }

  entity.assign<MovingBody>(
    Velocity{directionToVector(direction) * speed},
    GravityAffected{false});
  if (isPlayerProjectile(type) || type == ProjectileType::ReactorDebris) {
    // Some player projectiles do have collisions with walls, but that's
    // handled by player::ProjectileSystem.
    entity.component<MovingBody>()->mIgnoreCollisions = true;
    entity.component<MovingBody>()->mIsActive = false;

    entity.assign<DamageInflicting>(damageAmount, DestroyOnContact{false});
    entity.assign<PlayerProjectile>(toPlayerProjectileType(type));

    entity.assign<AutoDestroy>(AutoDestroy{
      AutoDestroy::Condition::OnLeavingActiveRegion});
  } else {
    entity.assign<PlayerDamaging>(damageAmount, false, true);

    entity.assign<AutoDestroy>(AutoDestroy{
      AutoDestroy::Condition::OnWorldCollision,
      AutoDestroy::Condition::OnLeavingActiveRegion});
  }

  // For convenience, the enemy laser shot muzzle flash is created along with
  // the projectile.
  if (type == ProjectileType::EnemyLaserShot) {
    const auto muzzleFlashSpriteId = direction == ProjectileDirection::Left
      ? data::ActorID::Enemy_laser_muzzle_flash_1
      : data::ActorID::Enemy_laser_muzzle_flash_2;
    auto muzzleFlash = createSprite(muzzleFlashSpriteId);
    muzzleFlash.assign<WorldPosition>(position);
    muzzleFlash.assign<AutoDestroy>(AutoDestroy::afterTimeout(1));
  }
}


entityx::Entity EntityFactory::createEntitiesForLevel(
  const data::map::ActorDescriptionList& actors
) {
  entityx::Entity playerEntity;

  for (const auto& actor : actors) {
    // Difficulty/section markers should never appear in the actor descriptions
    // coming from the loader, as they are handled during pre-processing.
    assert(
      actor.mID != ActorID::META_Appear_only_in_med_hard_difficulty && actor.mID != ActorID::META_Appear_only_in_hard_difficulty &&
      actor.mID != ActorID::META_Dynamic_geometry_marker_1 && actor.mID != ActorID::META_Dynamic_geometry_marker_2);

    auto entity = mpEntityManager->create();

    auto position = actor.mPosition;
    if (actor.mAssignedArea) {
      // For dynamic geometry, the original position refers to the top-left
      // corner of the assigned area, but it refers to the bottom-left corner
      // for all other entities. Adjust the position here so that it's also
      // bottom-left.
      position.y += actor.mAssignedArea->size.height - 1;
    }
    entity.assign<WorldPosition>(position);

    BoundingBox boundingBox;
    if (actor.mAssignedArea) {
      const auto mapSectionRect = *actor.mAssignedArea;
      entity.assign<MapGeometryLink>(mapSectionRect);

      boundingBox = mapSectionRect;
      boundingBox.topLeft = {0, 0};
    } else if (hasAssociatedSprite(actor.mID)) {
      const auto sprite = createSpriteForId(actor.mID);
      boundingBox = engine::inferBoundingBox(sprite, entity);
      entity.assign<Sprite>(sprite);
    }

    configureEntity(entity, actor.mID, boundingBox);

    const auto isPlayer = actor.mID == ActorID::Duke_LEFT || actor.mID == ActorID::Duke_RIGHT;
    if (isPlayer) {
      const auto playerOrientation = actor.mID == ActorID::Duke_LEFT
        ? Orientation::Left
        : Orientation::Right;
      assignPlayerComponents(entity, playerOrientation);
      playerEntity = entity;
    }
  }

  return playerEntity;
}


entityx::Entity spawnOneShotSprite(
  IEntityFactory& factory,
  const ActorID id,
  const base::Vector& position
) {
  auto entity = factory.createSprite(id, position, true);
  const auto numAnimationFrames = static_cast<int>(
    entity.component<Sprite>()->mpDrawData->mFrames.size());
  if (numAnimationFrames > 1) {
    engine::startAnimationLoop(entity, 1, 0, std::nullopt);
  }
  entity.assign<AutoDestroy>(AutoDestroy::afterTimeout(numAnimationFrames));
  assignSpecialEffectSpriteProperties(entity, id);
  return entity;
}


entityx::Entity spawnFloatingOneShotSprite(
  IEntityFactory& factory,
  const data::ActorID id,
  const base::Vector& position
) {
  using namespace engine::components::parameter_aliases;

  auto entity = spawnOneShotSprite(factory, id, position);
  entity.assign<MovingBody>(MovingBody{
    Velocity{0, -1.0f},
    GravityAffected{false},
    IgnoreCollisions{true}});
  return entity;
}


entityx::Entity spawnMovingEffectSprite(
  IEntityFactory& factory,
  const ActorID id,
  const SpriteMovement movement,
  const base::Vector& position
) {
  auto entity = factory.createSprite(id, position, true);
  configureMovingEffectSprite(entity, movement);
  if (entity.component<Sprite>()->mpDrawData->mFrames.size() > 1) {
    entity.assign<AnimationLoop>(1);
  }
  assignSpecialEffectSpriteProperties(entity, id);
  return entity;
}


void spawnFloatingScoreNumber(
  IEntityFactory& factory,
  const ScoreNumberType type,
  const base::Vector& position
) {
  using namespace engine::components::parameter_aliases;

  auto entity = factory.createSprite(scoreNumberActor(type), position, true);
  engine::startAnimationSequence(entity, SCORE_NUMBER_ANIMATION_SEQUENCE);
  entity.assign<MovementSequence>(SCORE_NUMBER_MOVE_SEQUENCE);
  entity.assign<MovingBody>(
    Velocity{},
    GravityAffected{false},
    IgnoreCollisions{true});
  entity.assign<AutoDestroy>(AutoDestroy::afterTimeout(SCORE_NUMBER_LIFE_TIME));
  entity.assign<Active>();
}


void spawnFireEffect(
  entityx::EntityManager& entityManager,
  const base::Vector& position,
  const BoundingBox& coveredArea,
  const data::ActorID actorToSpawn
) {
  // TODO: The initial offset should be based on the size of the actor
  // that's to be spawned. Currently, it's hard-coded for actor ID 3
  // (small explosion).
  auto offset = base::Vector{-1, 1};

  auto spawner = entityManager.create();
  SpriteCascadeSpawner spawnerConfig;
  spawnerConfig.mBasePosition = position + offset + coveredArea.topLeft;
  spawnerConfig.mCoveredArea = coveredArea.size;
  spawnerConfig.mActorId = actorToSpawn;
  spawner.assign<SpriteCascadeSpawner>(spawnerConfig);
  spawner.assign<AutoDestroy>(AutoDestroy::afterTimeout(18));
}

}
