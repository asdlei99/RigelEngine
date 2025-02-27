/* Copyright (C) 2020, Nikolai Wuttke. All rights reserved.
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

#include "ingame_menu.hpp"

#include "assets/resource_loader.hpp"
#include "base/match.hpp"
#include "data/unit_conversions.hpp"
#include "frontend/game_service_provider.hpp"
#include "frontend/user_profile.hpp"
#include "game_logic_common/igame_world.hpp"
#include "renderer/upscaling.hpp"
#include "renderer/viewport_utils.hpp"
#include "ui/utils.hpp"

#include <sstream>


namespace rigel::ui
{

namespace
{

constexpr auto MENU_TITLE_POS_X = 3;
constexpr auto MENU_TITLE_POS_Y = 2;
constexpr auto MENU_TITLE_MAX_LENGTH = 34;

constexpr auto MENU_START_POS_X = 11;
constexpr auto MENU_START_POS_Y = 6;
constexpr auto MENU_ITEM_HEIGHT = 2;
constexpr auto MENU_SELECTION_INDICATOR_POS_X = 8;
constexpr auto MENU_ITEM_COLOR = 2;
constexpr auto MENU_ITEM_COLOR_SELECTED = 3;

constexpr auto SAVE_SLOT_NAME_ENTRY_POS_X = 14;
constexpr auto SAVE_SLOT_NAME_ENTRY_START_POS_Y = MENU_START_POS_Y;
constexpr auto SAVE_SLOT_NAME_HEIGHT = MENU_ITEM_HEIGHT;
constexpr auto MAX_SAVE_SLOT_NAME_LENGTH = 18;

constexpr auto TOP_LEVEL_MENU_ITEMS = std::array{
  "Save Game",
  "Quick Save",
  "Restore Game",
  "Restore Quick Save",
  "Options",
  "Help",
  "Quit Game"};

constexpr int itemIndex(const std::string_view item)
{
  int result = 0;
  for (const auto candidate : TOP_LEVEL_MENU_ITEMS)
  {
    if (item == candidate)
    {
      return result;
    }

    ++result;
  }

  return -1;
}


auto createSavedGame(
  const data::GameSessionId& sessionId,
  const data::PlayerModel& playerModel)
{
  return data::SavedGame{
    sessionId,
    playerModel.tutorialMessages(),
    "", // will be filled in on saving
    playerModel.weapon(),
    playerModel.ammo(),
    playerModel.score()};
}


enum class SessionIdStringType
{
  Short,
  Long
};


std::string sessionIdString(
  const data::GameSessionId& sessionId,
  const SessionIdStringType type)
{
  const auto useShortForm = type == SessionIdStringType::Short;
  const auto episodeWord = useShortForm ? "Ep " : "Episode ";
  const auto levelWord = useShortForm ? "Lv " : "Level ";

  std::stringstream stream;
  stream << episodeWord << sessionId.mEpisode + 1 << ", " << levelWord
         << sessionId.mLevel + 1 << ", ";

  switch (sessionId.mDifficulty)
  {
    case data::Difficulty::Easy:
      stream << "Easy";
      break;
    case data::Difficulty::Medium:
      stream << "Medium";
      break;
    case data::Difficulty::Hard:
      stream << "Hard";
      break;
  }

  return stream.str();
}


std::string makePrefillName(const data::GameSessionId& sessionId)
{
  return sessionIdString(sessionId, SessionIdStringType::Short);
}

} // namespace


IngameMenu::TopLevelMenu::TopLevelMenu(
  GameMode::Context context,
  const data::GameSessionId& sessionId,
  const bool canQuickLoad)
  : mContext(context)
  , mPalette(context.mpResources->loadPaletteFromFullScreenImage("MESSAGE.MNI"))
  , mUiSpriteSheet(
      makeUiSpriteSheet(context.mpRenderer, *context.mpResources, mPalette))
  , mMenuElementRenderer(
      &mUiSpriteSheet,
      context.mpRenderer,
      *context.mpResources)
  , mMenuBackground(fullScreenImageAsTexture(
      context.mpRenderer,
      *context.mpResources,
      "MESSAGE.MNI"))
  , mTitleText(sessionIdString(sessionId, SessionIdStringType::Long))
  , mItems{
      itemIndex("Save Game"),
      itemIndex("Restore Game"),
      itemIndex("Options"),
      itemIndex("Help"),
      itemIndex("Quit Game")}
{
  using std::begin;
  using std::end;
  using std::find;
  using std::next;

  auto insertItem = [&](const int newItemIndex, const int precedingItemIndex) {
    const auto iPrecedingItem =
      find(begin(mItems), end(mItems), precedingItemIndex);
    assert(iPrecedingItem != end(mItems));

    mItems.insert(next(iPrecedingItem), newItemIndex);
  };

  if (context.mpUserProfile->mOptions.mQuickSavingEnabled)
  {
    insertItem(itemIndex("Quick Save"), itemIndex("Save Game"));
  }

  if (canQuickLoad)
  {
    insertItem(itemIndex("Restore Quick Save"), itemIndex("Restore Game"));
  }
}


void IngameMenu::TopLevelMenu::handleEvent(const SDL_Event& event)
{
  auto selectNext = [this]() {
    ++mSelectedIndex;
    if (mSelectedIndex >= int(mItems.size()))
    {
      mSelectedIndex = 0;
    }

    mContext.mpServiceProvider->playSound(data::SoundId::MenuSelect);
  };

  auto selectPrevious = [this]() {
    --mSelectedIndex;
    if (mSelectedIndex < 0)
    {
      mSelectedIndex = int(mItems.size()) - 1;
    }

    mContext.mpServiceProvider->playSound(data::SoundId::MenuSelect);
  };


  switch (mNavigationHelper.convert(event))
  {
    case NavigationEvent::NavigateUp:
      selectPrevious();
      break;

    case NavigationEvent::NavigateDown:
      selectNext();
      break;

    default:
      break;
  }
}


void IngameMenu::TopLevelMenu::updateAndRender(const engine::TimeDelta dt)
{
  mContext.mpRenderer->clear();
  mMenuBackground.render(0, 0);

  mMenuElementRenderer.drawBigText(
    MENU_TITLE_POS_X + (MENU_TITLE_MAX_LENGTH - int(mTitleText.size())) / 2,
    MENU_TITLE_POS_Y,
    mTitleText,
    mPalette[6]);

  auto index = 0;
  for (const auto item : mItems)
  {
    const auto colorIndex =
      index == mSelectedIndex ? MENU_ITEM_COLOR_SELECTED : MENU_ITEM_COLOR;
    mMenuElementRenderer.drawBigText(
      MENU_START_POS_X,
      MENU_START_POS_Y + index * MENU_ITEM_HEIGHT,
      TOP_LEVEL_MENU_ITEMS[item],
      mPalette[colorIndex]);
    ++index;
  }

  mElapsedTime += dt;
  mMenuElementRenderer.drawSelectionIndicator(
    MENU_SELECTION_INDICATOR_POS_X,
    MENU_START_POS_Y + mSelectedIndex * MENU_ITEM_HEIGHT,
    mElapsedTime);
}


void IngameMenu::TopLevelMenu::selectItem(const int index)
{
  const auto iItem = std::find(mItems.begin(), mItems.end(), index);
  if (iItem != mItems.end())
  {
    mSelectedIndex = static_cast<int>(std::distance(mItems.begin(), iItem));
  }
}


void IngameMenu::ScriptedMenu::handleEvent(const SDL_Event& event)
{
  if (!mEventHook(event))
  {
    mpScriptRunner->handleEvent(event);
  }
}


void IngameMenu::ScriptedMenu::updateAndRender(const engine::TimeDelta dt)
{
  mpScriptRunner->updateAndRender(dt);

  if (mpScriptRunner->hasFinishedExecution())
  {
    mScriptFinishedHook(*mpScriptRunner->result());
  }
}


IngameMenu::SavedGameNameEntry::SavedGameNameEntry(
  GameMode::Context context,
  const int slotIndex,
  const std::string_view initialName)
  : mTextEntryWidget(
      context.mpUiRenderer,
      SAVE_SLOT_NAME_ENTRY_POS_X,
      SAVE_SLOT_NAME_ENTRY_START_POS_Y + slotIndex * SAVE_SLOT_NAME_HEIGHT,
      MAX_SAVE_SLOT_NAME_LENGTH,
      ui::TextEntryWidget::Style::BigText,
      initialName)
  , mSlotIndex(slotIndex)
{
}


IngameMenu::IngameMenu(
  GameMode::Context context,
  const data::PlayerModel* pPlayerModel,
  game_logic::IGameWorld* pGameWorld,
  const data::GameSessionId& sessionId)
  : mContext(context)
  , mSavedGame(createSavedGame(sessionId, *pPlayerModel))
  , mSessionId(sessionId)
  , mpGameWorld(pGameWorld)
{
}


bool IngameMenu::isTransparent() const
{
  if (mStateStack.empty())
  {
    return true;
  }

  if (mpTopLevelMenu)
  {
    return false;
  }

  return base::match(
    mStateStack.top(),
    [](const ScriptedMenu& state) { return state.mIsTransparent; },
    [](const ui::OptionsMenu&) { return true; },
    [](const auto&) { return false; });
}


void IngameMenu::handleEvent(const SDL_Event& event)
{
  if (mQuitRequested || mRequestedGameToLoad)
  {
    return;
  }

  if (!isActive())
  {
    handleMenuEnterEvent(event);
    handleCheatCodes();
  }
  else
  {
    // We want to process menu navigation and similar events in updateAndRender,
    // so we only add them to a queue here.
    mEventQueue.push_back(event);
  }
}


auto IngameMenu::updateAndRender(engine::TimeDelta dt) -> UpdateResult
{
  if (mMenuToEnter)
  {
    enterMenu(*mMenuToEnter);
    mMenuToEnter.reset();
  }

  mFadeoutNeeded = false;

  handleMenuActiveEvents();

  if (mpTopLevelMenu && mStateStack.size() > 1)
  {
    mpTopLevelMenu->updateAndRender(0.0);
  }

  if (!mStateStack.empty())
  {
    base::match(
      mStateStack.top(),
      [dt, this](SavedGameNameEntry& state) {
        mContext.mpScriptRunner->updateAndRender(dt);
        state.updateAndRender(dt);
      },

      [dt](std::unique_ptr<TopLevelMenu>& pState) {
        pState->updateAndRender(dt);
      },

      [this, dt](ScriptedMenu& state) {
        const auto& options = mContext.mpUserProfile->mOptions;
        if (
          state.mIsTransparent && options.widescreenModeActive() &&
          renderer::canUseWidescreenMode(mContext.mpRenderer))
        {
          // When showing a message box while in-game, the corresponding
          // scripts always feature a SHIFTWIN instruction, which causes
          // the box to be offset to the left by 3 tiles. This is done
          // because normally, the right-hand side of the HUD takes away
          // some screen real estate, and thus the box needs to be shifted
          // in order to still appear centered within the in-game content.
          // But when using one of the alternative HUD styles that RigelEngine
          // offers, there is no right-hand side HUD anymore, thus we need
          // to negate this shift again in order for message boxes to still
          // appear centered.
          //
          // TODO: Introduce a constant for this value?
          const auto offsetForAlternativeHudStyles =
            options.mWidescreenHudStyle != data::WidescreenHudStyle::Classic
            ? data::tilesToPixels(3)
            : 0;

          auto saved = renderer::saveState(mContext.mpRenderer);
          mContext.mpRenderer->setClipRect({});
          mContext.mpRenderer->setGlobalTranslation(
            renderer::scaleVec(
              {offsetForAlternativeHudStyles, 0},
              mContext.mpRenderer->globalScale()) +
            renderer::offsetTo4by3WithinWidescreen(
              mContext.mpRenderer, options));
          state.updateAndRender(dt);
        }
        else
        {
          state.updateAndRender(dt);
        }
      },

      [dt](auto& state) { state.updateAndRender(dt); });
  }

  if (mStateStack.empty())
  {
    return mFadeoutNeeded ? UpdateResult::FinishedNeedsFadeout
                          : UpdateResult::Finished;
  }
  else
  {
    return UpdateResult::StillActive;
  }
}


void IngameMenu::onRestoreGameMenuFinished(const ExecutionResult& result)
{
  auto showErrorMessageScript = [this](const char* scriptName) {
    // When selecting a slot that can't be loaded, we show a message and
    // then return to the save slot selection menu.  The latter stays on the
    // stack, we push another menu state on top of the stack for showing the
    // message.
    enterScriptedMenu(
      scriptName,
      [this](const auto&) {
        leaveMenu();
        runScript(mContext, "Restore_Game");
      },
      noopEventHook,
      false, // isTransparent
      false); // shouldClearScriptCanvas
  };


  using STT = ui::DukeScriptRunner::ScriptTerminationType;

  if (result.mTerminationType == STT::AbortedByUser)
  {
    leaveMenu();
    fadeout();
  }
  else
  {
    const auto slotIndex = result.mSelectedPage;
    const auto& slot = mContext.mpUserProfile->mSaveSlots[*slotIndex];
    if (slot)
    {
      if (
        mContext.mpServiceProvider->isSharewareVersion() &&
        slot->mSessionId.needsRegisteredVersion())
      {
        showErrorMessageScript("No_Can_Order");
      }
      else
      {
        mRequestedGameToLoad = *slot;
      }
    }
    else
    {
      showErrorMessageScript("No_Game_Restore");
    }
  }
}


void IngameMenu::saveGame(const int slotIndex, std::string_view name)
{
  auto savedGame = mSavedGame;
  savedGame.mName = name;

  mContext.mpUserProfile->mSaveSlots[slotIndex] = savedGame;
  mContext.mpUserProfile->saveToDisk();
}


void IngameMenu::handleMenuEnterEvent(const SDL_Event& event)
{
  if (
    event.type == SDL_CONTROLLERBUTTONDOWN &&
    event.cbutton.button == SDL_CONTROLLER_BUTTON_START)
  {
    mMenuToEnter = MenuType::TopLevel;
    return;
  }

  if (!isNonRepeatKeyDown(event))
  {
    return;
  }

  switch (event.key.keysym.sym)
  {
    case SDLK_q:
      mMenuToEnter = MenuType::ConfirmQuitInGame;
      break;

    case SDLK_ESCAPE:
      mMenuToEnter = MenuType::TopLevel;
      break;

    case SDLK_F1:
      mMenuToEnter = MenuType::Options;
      break;

    case SDLK_F2:
      mMenuToEnter = MenuType::SaveGame;
      break;

    case SDLK_F3:
      mMenuToEnter = MenuType::LoadGame;
      break;

    case SDLK_h:
      mMenuToEnter = MenuType::Help;
      break;

    case SDLK_p:
      mMenuToEnter = MenuType::Pause;
      break;

    default:
      break;
  }
}


void IngameMenu::handleCheatCodes()
{
  if (isActive())
  {
    return;
  }

  const auto pKeyboard = SDL_GetKeyboardState(nullptr);
  auto keysPressed = [&](auto... keys) {
    return (pKeyboard[SDL_GetScancodeFromKey(keys)] && ...);
  };

  if (mContext.mpServiceProvider->isSharewareVersion())
  {
    if (keysPressed(SDLK_g, SDLK_o, SDLK_d))
    {
      mMenuToEnter = MenuType::CheatMessagePrayingWontHelp;
    }
  }
  else
  {
    // In the original, the "praying won't help you" pseudo-cheat (it's not
    // actually a cheat, just a message telling you to buy the registered
    // version) still works in the registered version. But in my opinion, it
    // doesn't make much sense to mention buying the registered version to
    // someone already owning it. So, contrary to the original, we don't check
    // for g, o, d being pressed here, only in the shareware version.
    if (keysPressed(SDLK_e, SDLK_a, SDLK_t))
    {
      mpGameWorld->activateFullHealthCheat();
      mMenuToEnter = MenuType::CheatMessageHealthRestored;
    }
    else if (keysPressed(SDLK_n, SDLK_u, SDLK_k))
    {
      mMenuToEnter = MenuType::CheatMessageItemsGiven;
      // The cheat is activated after entering the menu, in order to avoid
      // inventory items appearing before the message is visible.
    }
  }
}


void IngameMenu::enterMenu(const MenuType type)
{
  auto leaveMenuHook = [this](const ExecutionResult&) {
    leaveMenu();
  };

  auto leaveMenuWithFadeHook = [this](const ExecutionResult&) {
    leaveMenu();
    fadeout();
  };

  auto quitConfirmEventHook = [this](const SDL_Event& ev) {
    // The user needs to press Y in order to confirm quitting the game, but we
    // want the confirmation to happen when the key is released, not when it's
    // pressed. This is because the "a new high score" screen may appear after
    // quitting the game, and if we were to quit on key down, it's very likely
    // for the key to still be pressed while the new screen appears. This in
    // turn would lead to an undesired letter Y being entered into the high
    // score name entry field, because the text input system would see the key
    // being released and treated as an input.
    //
    // Therefore, we quit on key up. Nevertheless, we still need to prevent the
    // key down event from reaching the script runner, as it would cancel out
    // the quit confirmation dialog otherwise.
    if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_y)
    {
      return true;
    }
    if (
      (ev.type == SDL_KEYUP && ev.key.keysym.sym == SDLK_y) ||
      (ev.type == SDL_CONTROLLERBUTTONDOWN &&
       ev.cbutton.button == SDL_CONTROLLER_BUTTON_A))
    {
      mQuitRequested = true;
      return true;
    }

    return false;
  };

  auto saveSlotSelectionEventHook = [this](const SDL_Event& event) {
    if (isMenuConfirmButton(event))
    {
      const auto enteredViaGamepad = event.type == SDL_CONTROLLERBUTTONDOWN;

      const auto slotIndex = *mContext.mpScriptRunner->currentPageIndex();
      SDL_StartTextInput();
      mStateStack.push(SavedGameNameEntry{
        mContext,
        slotIndex,
        enteredViaGamepad ? makePrefillName(mSavedGame.mSessionId) : ""});
      return true;
    }

    return false;
  };

  auto onSaveSlotSelectionFinished = [this](const ExecutionResult& result) {
    using STT = ui::DukeScriptRunner::ScriptTerminationType;
    if (result.mTerminationType == STT::AbortedByUser)
    {
      leaveMenu();
      fadeout();
    }
  };


  switch (type)
  {
    case MenuType::ConfirmQuitInGame:
      enterScriptedMenu(
        "2Quit_Select", leaveMenuHook, quitConfirmEventHook, true);
      break;

    case MenuType::ConfirmQuit:
      enterScriptedMenu("Quit_Select", leaveMenuHook, quitConfirmEventHook);
      break;

    case MenuType::Options:
      mStateStack.push(ui::OptionsMenu{
        mContext.mpUserProfile,
        mContext.mpServiceProvider,
        mContext.mpRenderer,
        ui::OptionsMenu::Type::InGame});
      break;

    case MenuType::SaveGame:
      enterScriptedMenu(
        "Save_Game", onSaveSlotSelectionFinished, saveSlotSelectionEventHook);
      break;

    case MenuType::LoadGame:
      enterScriptedMenu("Restore_Game", [this](const auto& result) {
        onRestoreGameMenuFinished(result);
      });
      break;

    case MenuType::Help:
      enterScriptedMenu("&Instructions", leaveMenuWithFadeHook);
      break;

    case MenuType::Pause:
      enterScriptedMenu("Paused", leaveMenuHook, noopEventHook, true);
      break;

    case MenuType::CheatMessagePrayingWontHelp:
      enterScriptedMenu("The_Prey", leaveMenuHook, noopEventHook, true);
      break;

    case MenuType::CheatMessageHealthRestored:
      enterScriptedMenu("Full_Health", leaveMenuHook, noopEventHook, true);
      // The original game incorrectly does a fadeout after the message is
      // closed, but we don't replicate it here.
      break;

    case MenuType::CheatMessageItemsGiven:
      enterScriptedMenu("Now_Ch", leaveMenuWithFadeHook);
      mpGameWorld->activateGiveItemsCheat();
      break;

    case MenuType::TopLevel:
      {
        auto pMenu = std::make_unique<TopLevelMenu>(
          mContext, mSessionId, mpGameWorld->canQuickLoad());
        mContext.mpServiceProvider->fadeOutScreen();
        pMenu->updateAndRender(0.0);
        mContext.mpServiceProvider->fadeInScreen();

        mpTopLevelMenu = pMenu.get();
        mStateStack.push(std::move(pMenu));
      }
      break;
  }
}


void IngameMenu::handleMenuActiveEvents()
{
  auto handleSavedGameNameEntryEvent =
    [this](SavedGameNameEntry& state, const SDL_Event& event) {
      auto leaveSaveGameMenu = [&, this]() {
        SDL_StopTextInput();

        // Render one last time so we have something to fade out from
        mContext.mpScriptRunner->updateAndRender(0.0);
        state.updateAndRender(0.0);

        mStateStack.pop();
        mStateStack.pop();
      };

      if (isConfirmButton(event))
      {
        saveGame(state.mSlotIndex, state.mTextEntryWidget.text());
        leaveSaveGameMenu();
        if (mpTopLevelMenu)
        {
          mpTopLevelMenu = nullptr;
          mStateStack.pop();
        }

        fadeout();
      }
      else if (isCancelButton(event))
      {
        SDL_StopTextInput();
        mStateStack.pop();
      }
      else
      {
        state.mTextEntryWidget.handleEvent(event);
      }
    };

  auto leaveTopLevelMenu = [this](TopLevelMenu& state) {
    state.updateAndRender(0.0);
    mpTopLevelMenu = nullptr;
    mStateStack.pop();
    fadeout();
  };


  for (const auto& event : mEventQueue)
  {
    if (mStateStack.empty())
    {
      break;
    }

    base::match(
      mStateStack.top(),
      [&](std::unique_ptr<TopLevelMenu>& pState) {
        auto& state = *pState;
        if (isConfirmButton(event))
        {
          switch (state.mItems[state.mSelectedIndex])
          {
            case itemIndex("Save Game"):
              enterMenu(MenuType::SaveGame);
              break;

            case itemIndex("Quick Save"):
              mpGameWorld->quickSave();
              leaveTopLevelMenu(state);
              break;

            case itemIndex("Restore Game"):
              enterMenu(MenuType::LoadGame);
              break;

            case itemIndex("Restore Quick Save"):
              mpGameWorld->quickLoad();
              leaveTopLevelMenu(state);
              break;

            case itemIndex("Options"):
              enterMenu(MenuType::Options);
              break;

            case itemIndex("Help"):
              enterMenu(MenuType::Help);
              break;

            case itemIndex("Quit Game"):
              enterMenu(MenuType::ConfirmQuit);
              break;
          }
        }
        else if (isCancelButton(event))
        {
          leaveTopLevelMenu(state);
        }
        else
        {
          state.handleEvent(event);
        }
      },

      [&, this](SavedGameNameEntry& state) {
        handleSavedGameNameEntryEvent(state, event);
      },

      [&event](auto& state) { state.handleEvent(event); });
  }

  mEventQueue.clear();

  // Handle options menu being closed
  if (
    !mStateStack.empty() &&
    std::holds_alternative<ui::OptionsMenu>(mStateStack.top()) &&
    std::get<ui::OptionsMenu>(mStateStack.top()).isFinished())
  {
    mStateStack.pop();

    // If the options menu was entered via the top-level menu, we need to
    // update the list of available menu items. This is because the
    // "quick save" and "quick load" entries are only shown when quick saving
    // is enabled. And because the options menu was open, it's possible that
    // that setting (quick save enabled) has now changed.
    if (
      !mStateStack.empty() &&
      std::holds_alternative<std::unique_ptr<TopLevelMenu>>(mStateStack.top()))
    {
      auto& pTopLevelMenu =
        std::get<std::unique_ptr<TopLevelMenu>>(mStateStack.top());

      // Create a new TopLevelMenu, as that's the easiest way to rebuild
      // the list of visible menu items. That resets the selection to the top
      // item though. So we select the "Options" entry again afterwards to
      // keep it selected.
      pTopLevelMenu = std::make_unique<TopLevelMenu>(
        mContext, mSessionId, mpGameWorld->canQuickLoad());
      pTopLevelMenu->selectItem(itemIndex("Options"));
      mpTopLevelMenu = pTopLevelMenu.get();
    }
  }
}


template <typename ScriptEndHook, typename EventHook>
void IngameMenu::enterScriptedMenu(
  const char* scriptName,
  ScriptEndHook&& scriptEndedHook,
  EventHook&& eventHook,
  const bool isTransparent,
  const bool shouldClearScriptCanvas)
{
  if (shouldClearScriptCanvas)
  {
    mContext.mpScriptRunner->clearCanvas();
  }

  runScript(mContext, scriptName);
  mStateStack.push(ScriptedMenu{
    mContext.mpScriptRunner,
    std::forward<ScriptEndHook>(scriptEndedHook),
    std::forward<EventHook>(eventHook),
    isTransparent});
}


void IngameMenu::leaveMenu()
{
  mStateStack.pop();
}


void IngameMenu::fadeout()
{
  if (mpTopLevelMenu)
  {
    mContext.mpServiceProvider->fadeOutScreen();
    mpTopLevelMenu->updateAndRender(0.0);
    mContext.mpServiceProvider->fadeInScreen();
  }
  else
  {
    mFadeoutNeeded = true;
  }
}

} // namespace rigel::ui
