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

#pragma once

#include "base/spatial_types.hpp"
#include "base/warnings.hpp"
#include "engine/base_components.hpp"
#include "engine/map_renderer.hpp"
#include "engine/timing.hpp"
#include "engine/visual_components.hpp"
#include "renderer/renderer.hpp"
#include "renderer/texture.hpp"

RIGEL_DISABLE_WARNINGS
#include <entityx/entityx.h>
RIGEL_RESTORE_WARNINGS

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>


namespace rigel::data { struct GameOptions; }
namespace rigel::renderer { class TextureAtlas; }


namespace rigel::engine {

/** Animates sprites with an AnimationLoop component
 *
 * Should be called at game-logic rate. Works on all entities that have a
 * Sprite and an AnimationLoop component. Adjusts the sprite's animation frame
 * based on the animation.
 */
void updateAnimatedSprites(entityx::EntityManager& es);


struct SpriteData {
  SpriteData(
    const entityx::Entity entity,
    const components::Sprite* pSprite,
    const bool drawTopMost,
    const components::WorldPosition& position
  )
    : mEntity(entity)
    , mPosition(position)
    , mpSprite(pSprite)
    , mDrawOrder(
        entity.has_component<components::OverrideDrawOrder>()
        ? entity.component<const components::OverrideDrawOrder>()->mDrawOrder
        : pSprite->mpDrawData->mDrawOrder)
    , mDrawTopMost(drawTopMost)
  {
  }

  bool operator<(const SpriteData& rhs) const {
    return
      std::tie(mDrawTopMost, mDrawOrder) <
      std::tie(rhs.mDrawTopMost, rhs.mDrawOrder);
  }

  entityx::Entity mEntity;
  components::WorldPosition mPosition;
  const components::Sprite* mpSprite;
  int mDrawOrder;
  bool mDrawTopMost;
};


class SpriteRenderingSystem {
public:
  SpriteRenderingSystem(
    renderer::Renderer* pRenderer,
    const renderer::TextureAtlas* pTextureAtlas);

  void update(entityx::EntityManager& es, const base::Extents& viewPortSize);

  void renderRegularSprites(const base::Vector& cameraPosition) const;
  void renderForegroundSprites(const base::Vector& cameraPosition) const;

private:
  void renderSprite(
    const SpriteData& sprite,
    const base::Vector& cameraPosition) const;

  std::vector<SpriteData> mSpritesByDrawOrder;
  std::vector<SpriteData>::iterator miForegroundSprites;
  renderer::Renderer* mpRenderer;
  const renderer::TextureAtlas* mpTextureAtlas;
};


/** Renders the map and in-game sprites
 *
 * Works on all entities that have a Sprite and WorldPosition component.
 * Also renders the map using a engine::MapRenderer. Map and sprite rendering
 * are handled by the same system so that draw-order can be done properly
 * (e.g. some sprites are rendered behind certain tiles, others before etc.)
 */
class RenderingSystem {
public:
  RenderingSystem(
    const base::Vector* pCameraPosition,
    renderer::Renderer* pRenderer,
    const data::GameOptions* pOptions,
    const renderer::TextureAtlas* pSpritesTextureAtlas,
    const data::map::Map* pMap,
    MapRenderer::MapRenderData&& mapRenderData);

  /** Update map tile animation state. Should be called at game-logic rate. */
  void updateAnimatedMapTiles() {
    mMapRenderer.updateAnimatedMapTiles();

    ++mWaterAnimStep;
    if (mWaterAnimStep >= 4) {
      mWaterAnimStep = 0;
    }
  }

  /** Render everything. Can be called at full frame rate. */
  void update(
    entityx::EntityManager& es,
    const std::optional<base::Color>& backdropFlashColor,
    const base::Extents& viewPortSize);

  void switchBackdrops() {
    mMapRenderer.switchBackdrops();
  }

  void updateBackdropAutoScrolling(const engine::TimeDelta dt) {
    mMapRenderer.updateBackdropAutoScrolling(dt);
  }

  std::size_t spritesRendered() const {
    return mSpritesRendered;
  }

private:
  renderer::Renderer* mpRenderer;
  const renderer::TextureAtlas* mpTextureAtlas;
  renderer::RenderTargetTexture mRenderTarget;
  MapRenderer mMapRenderer;
  const base::Vector* mpCameraPosition;
  int mWaterAnimStep = 0;
  std::size_t mSpritesRendered = 0;
};

}
