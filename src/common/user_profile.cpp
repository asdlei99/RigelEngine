/* Copyright (C) 2019, Nikolai Wuttke. All rights reserved.
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

#include "user_profile.hpp"

#include "base/warnings.hpp"
#include "common/json_utils.hpp"
#include "loader/file_utils.hpp"
#include "loader/user_profile_import.hpp"

RIGEL_DISABLE_WARNINGS
#include <nlohmann/json.hpp>
#include <SDL_filesystem.h>
#include <SDL_keyboard.h>
RIGEL_RESTORE_WARNINGS

#include <iomanip>
#include <iostream>
#include <fstream>
#include <unordered_set>


namespace rigel {

namespace data {

NLOHMANN_JSON_SERIALIZE_ENUM(Difficulty, {
  {Difficulty::Easy, "Easy"},
  {Difficulty::Medium, "Medium"},
  {Difficulty::Hard, "Hard"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(WeaponType, {
  {WeaponType::Normal, "Normal"},
  {WeaponType::Laser, "Laser"},
  {WeaponType::Rocket, "Rocket"},
  {WeaponType::FlameThrower, "FlameThrower"},
})


NLOHMANN_JSON_SERIALIZE_ENUM(TutorialMessageId, {
  {TutorialMessageId::FoundRapidFire, "FoundRapidFire"},
  {TutorialMessageId::FoundHealthMolecule, "FoundHealthMolecule"},
  {TutorialMessageId::FoundRegularWeapon, "FoundRegularWeapon"},
  {TutorialMessageId::FoundLaser, "FoundLaser"},
  {TutorialMessageId::FoundFlameThrower, "FoundFlameThrower"},
  {TutorialMessageId::FoundRocketLauncher, "FoundRocketLauncher"},
  {TutorialMessageId::EarthQuake, "EarthQuake"},
  {TutorialMessageId::FoundBlueKey, "FoundBlueKey"},
  {TutorialMessageId::FoundAccessCard, "FoundAccessCard"},
  {TutorialMessageId::FoundSpaceShip, "FoundSpaceShip"},
  {TutorialMessageId::FoundLetterN, "FoundLetterN"},
  {TutorialMessageId::FoundLetterU, "FoundLetterU"},
  {TutorialMessageId::FoundLetterK, "FoundLetterK"},
  {TutorialMessageId::FoundLetterE, "FoundLetterE"},
  {TutorialMessageId::KeyNeeded, "KeyNeeded"},
  {TutorialMessageId::AccessCardNeeded, "AccessCardNeeded"},
  {TutorialMessageId::CloakNeeded, "CloakNeeded"},
  {TutorialMessageId::RadarsStillFunctional, "RadarsStillFunctional"},
  {TutorialMessageId::HintGlobeNeeded, "HintGlobeNeeded"},
  {TutorialMessageId::FoundTurboLift, "FoundTurboLift"},
  {TutorialMessageId::FoundTeleporter, "FoundTeleporter"},
  {TutorialMessageId::LettersCollectedRightOrder, "LettersCollectedRightOrder"},
  {TutorialMessageId::FoundSoda, "FoundSoda"},
  {TutorialMessageId::FoundForceField, "FoundForceField"},
  {TutorialMessageId::FoundDoor, "FoundDoor"},
})


NLOHMANN_JSON_SERIALIZE_ENUM(WindowMode, {
  {WindowMode::Fullscreen, "Fullscreen"},
  {WindowMode::ExclusiveFullscreen, "ExclusiveFullscreen"},
  {WindowMode::Windowed, "Windowed"},
})

}


namespace {

constexpr auto PREF_PATH_ORG_NAME = "lethal-guitar";
constexpr auto PREF_PATH_APP_NAME = "Rigel Engine";
constexpr auto USER_PROFILE_FILENAME_V1 = "UserProfile.rigel";
constexpr auto OPTIONS_FILENAME = "Options.json";


void removeInvalidKeyBindings(data::GameOptions& options) {
  std::unordered_set<SDL_Keycode> allBindings;

  for (auto pBinding : options.allKeyBindings()) {
    // If the binding already appeared previously, the current one is a
    // duplicate.
    const auto [_, isUnique] = allBindings.insert(*pBinding);

    const auto isValidBinding = data::canBeUsedForKeyBinding(*pBinding);
    if (!isUnique || !isValidBinding) {
      *pBinding = SDLK_UNKNOWN;
    }
  }
}


void importOptions(
  data::GameOptions& options,
  const loader::GameOptions& originalOptions
) {
  options.mSoundOn =
    originalOptions.mSoundBlasterSoundsOn ||
    originalOptions.mAdlibSoundsOn ||
    originalOptions.mPcSpeakersSoundsOn;
  options.mMusicOn = originalOptions.mMusicOn;
}


nlohmann::json serialize(const data::TutorialMessageState& messageState) {
  auto serialized = nlohmann::json::array();

  for (int i = 0; i < data::NUM_TUTORIAL_MESSAGES; ++i) {
    const auto value = static_cast<data::TutorialMessageId>(i);
    if (messageState.hasBeenShown(value)) {
      serialized.push_back(value);
    }
  }

  return serialized;
}


nlohmann::json serialize(const data::SavedGame& savedGame) {
  using json = nlohmann::json;

  json serialized;
  serialized["episode"] = savedGame.mSessionId.mEpisode;
  serialized["level"] = savedGame.mSessionId.mLevel;
  serialized["difficulty"] = savedGame.mSessionId.mDifficulty;
  serialized["tutorialMessagesAlreadySeen"] =
    serialize(savedGame.mTutorialMessagesAlreadySeen);
  serialized["name"] = savedGame.mName;
  serialized["weapon"] = savedGame.mWeapon;
  serialized["ammo"] = savedGame.mAmmo;
  serialized["score"] = savedGame.mScore;
  return serialized;
}


nlohmann::json serialize(const data::SaveSlotArray& saveSlots) {
  auto serialized = nlohmann::json::array();
  for (const auto& slot : saveSlots) {
    if (slot) {
      serialized.push_back(serialize(*slot));
    } else {
      serialized.push_back(nullptr);
    }
  }
  return serialized;
}


nlohmann::json serialize(const data::HighScoreEntry& entry) {
  using json = nlohmann::json;

  json serialized;
  serialized["name"] = entry.mName;
  serialized["score"] = entry.mScore;
  return serialized;
}


nlohmann::json serialize(const data::HighScoreListArray& highScoreLists) {
  using json = nlohmann::json;

  auto serialized = json::array();
  for (const auto& list : highScoreLists) {
    auto serializedList = json::array();
    for (const auto& entry : list) {
      serializedList.push_back(serialize(entry));
    }

    serialized.push_back(serializedList);
  }

  return serialized;
}


nlohmann::ordered_json serialize(const data::GameOptions& options) {
  using json = nlohmann::ordered_json;

  // NOTE: When adding a new member to the data::GameOptions struct, you most
  // likely want to add a corresponding entry here as well. You also need to
  // add the deserialization counterpart to the deserialization function
  // further down in this file, i.e. `deserialize<data::GameOptions>()`.
  json serialized;
  serialized["windowMode"] = options.mWindowMode;
  serialized["windowPosX"] = options.mWindowPosX;
  serialized["windowPosY"] = options.mWindowPosY;
  serialized["windowWidth"] = options.mWindowWidth;
  serialized["windowHeight"] = options.mWindowHeight;
  serialized["enableVsync"] = options.mEnableVsync;
  serialized["enableFpsLimit"] = options.mEnableFpsLimit;
  serialized["maxFps"] = options.mMaxFps;
  serialized["showFpsCounter"] = options.mShowFpsCounter;
  serialized["musicVolume"] = options.mMusicVolume;
  serialized["soundVolume"] = options.mSoundVolume;
  serialized["musicOn"] = options.mMusicOn;
  serialized["soundOn"] = options.mSoundOn;

  serialized["upKeybinding"] = SDL_GetKeyName(options.mUpKeybinding);
  serialized["downKeybinding"] = SDL_GetKeyName(options.mDownKeybinding);
  serialized["leftKeybinding"] = SDL_GetKeyName(options.mLeftKeybinding);
  serialized["rightKeybinding"] = SDL_GetKeyName(options.mRightKeybinding);
  serialized["jumpKeybinding"] = SDL_GetKeyName(options.mJumpKeybinding);
  serialized["fireKeybinding"] = SDL_GetKeyName(options.mFireKeybinding);
  serialized["quickSaveKeybinding"] = SDL_GetKeyName(options.mQuickSaveKeybinding);
  serialized["quickLoadKeybinding"] = SDL_GetKeyName(options.mQuickLoadKeybinding);

  serialized["compatibilityModeOn"] = options.mCompatibilityModeOn;
  serialized["widescreenModeOn"] = options.mWidescreenModeOn;
  serialized["quickSavingEnabled"] = options.mQuickSavingEnabled;
  return serialized;
}


template<typename T>
T deserialize(const nlohmann::json& json);


template<>
data::SavedGame deserialize<data::SavedGame>(const nlohmann::json& json) {
  using namespace data;

  // TODO: Does it make sense to share the clamping/validation code with the
  // user profile importer?
  data::SavedGame result;
  result.mSessionId.mEpisode = std::clamp(
    json.at("episode").get<int>(), 0, NUM_EPISODES - 1);
  result.mSessionId.mLevel = std::clamp(
    json.at("level").get<int>(), 0, NUM_LEVELS_PER_EPISODE - 1);
  result.mSessionId.mDifficulty = json.at("difficulty").get<data::Difficulty>();

  const auto& messageIds = json.at("tutorialMessagesAlreadySeen");
  for (const auto& messageId : messageIds) {
    result.mTutorialMessagesAlreadySeen.markAsShown(
      messageId.get<data::TutorialMessageId>());
  }

  result.mName = json.at("name").get<std::string>();
  result.mWeapon = json.at("weapon").get<data::WeaponType>();

  const auto maxAmmo = result.mWeapon == WeaponType::FlameThrower
    ? MAX_AMMO_FLAME_THROWER
    : MAX_AMMO;
  result.mAmmo = std::clamp(json.at("ammo").get<int>(), 0, maxAmmo);
  result.mScore = std::clamp(json.at("score").get<int>(), 0, MAX_SCORE);
  return result;
}


template<>
data::SaveSlotArray deserialize<data::SaveSlotArray>(
  const nlohmann::json& json
) {
  data::SaveSlotArray result;

  std::size_t i = 0;
  for (const auto& serializedSlot : json) {
    if (!serializedSlot.is_null()) {
      result[i] = deserialize<data::SavedGame>(serializedSlot);
    }
    ++i;
    if (i >= result.size()) {
      break;
    }
  }

  return result;
}


template<>
data::HighScoreEntry deserialize<data::HighScoreEntry>(
  const nlohmann::json& json
) {
  data::HighScoreEntry result;

  result.mName = json.at("name").get<std::string>();
  result.mScore = std::clamp(json.at("score").get<int>(), 0, data::MAX_SCORE);

  return result;
}


template<>
data::HighScoreListArray deserialize<data::HighScoreListArray>(
  const nlohmann::json& json
) {
  using std::begin;
  using std::end;
  using std::sort;

  data::HighScoreListArray result;

  std::size_t i = 0;
  for (const auto& serializedList : json) {
    std::size_t j = 0;
    for (const auto& serializedEntry : serializedList) {
      result[i][j] = deserialize<data::HighScoreEntry>(serializedEntry);

      ++j;
      if (j >= data::NUM_HIGH_SCORE_ENTRIES) {
        break;
      }
    }

    sort(begin(result[i]), end(result[i]));

    ++i;
    if (i >= result.size()) {
      break;
    }
  }

  return result;
}


template <typename TargetType>
bool extractValueIfExists(
  const char* key,
  TargetType& value,
  const nlohmann::json& json
) {
  if (json.contains(key)) {
    try {
      value = json.at(key).get<TargetType>();
      return true;
    } catch (const std::exception&) {
    }
  }

  return false;
}


void extractKeyBindingIfExists(
  const char* key,
  SDL_Keycode& value,
  const nlohmann::json& json
) {
  auto serialized = std::string{};
  if (!extractValueIfExists(key, serialized, json)) {
    return;
  }

  value = SDL_GetKeyFromName(serialized.c_str());
}


template<>
data::GameOptions deserialize<data::GameOptions>(const nlohmann::json& json) {
  data::GameOptions result;

  // NOTE: When adding a new member to the data::GameOptions struct, you most
  // likely want to add a corresponding entry here as well. You also need to
  // add the serialization counterpart to the serialization function further
  // up in this file, i.e. `serialize(const data::GameOptions& options)`.
  extractValueIfExists("windowMode", result.mWindowMode, json);
  extractValueIfExists("windowPosX", result.mWindowPosX, json);
  extractValueIfExists("windowPosY", result.mWindowPosY, json);
  extractValueIfExists("windowWidth", result.mWindowWidth, json);
  extractValueIfExists("windowHeight", result.mWindowHeight, json);
  extractValueIfExists("enableVsync", result.mEnableVsync, json);
  extractValueIfExists("enableFpsLimit", result.mEnableFpsLimit, json);
  extractValueIfExists("maxFps", result.mMaxFps, json);
  extractValueIfExists("showFpsCounter", result.mShowFpsCounter, json);
  extractValueIfExists("musicVolume", result.mMusicVolume, json);
  extractValueIfExists("soundVolume", result.mSoundVolume, json);
  extractValueIfExists("musicOn", result.mMusicOn, json);
  extractValueIfExists("soundOn", result.mSoundOn, json);
  extractKeyBindingIfExists("upKeybinding", result.mUpKeybinding, json);
  extractKeyBindingIfExists("downKeybinding", result.mDownKeybinding, json);
  extractKeyBindingIfExists("leftKeybinding", result.mLeftKeybinding, json);
  extractKeyBindingIfExists("rightKeybinding", result.mRightKeybinding, json);
  extractKeyBindingIfExists("jumpKeybinding", result.mJumpKeybinding, json);
  extractKeyBindingIfExists("fireKeybinding", result.mFireKeybinding, json);
  extractKeyBindingIfExists("quickSaveKeybinding", result.mQuickSaveKeybinding, json);
  extractKeyBindingIfExists("quickLoadKeybinding", result.mQuickLoadKeybinding, json);
  extractValueIfExists("compatibilityModeOn", result.mCompatibilityModeOn, json);
  extractValueIfExists("widescreenModeOn", result.mWidescreenModeOn, json);
  extractValueIfExists("quickSavingEnabled", result.mQuickSavingEnabled, json);

  removeInvalidKeyBindings(result);

  return result;
}


void loadOptionsFileIfPresent(
  const std::filesystem::path& path,
  data::GameOptions& options
) {
  namespace fs = std::filesystem;

  std::error_code ec;
  if (!fs::exists(path, ec) || ec) {
    return;
  }

  try
  {
    std::ifstream optionsFile(path.u8string());

    nlohmann::json serializedOptions;
    optionsFile >> serializedOptions;

    options = deserialize<data::GameOptions>(serializedOptions);
  } catch (const std::exception& ex) {
    std::cerr << "WARNING: Failed to load options\n";
    std::cerr << ex.what() << '\n';
  }
}


UserProfile loadProfile(
  const std::filesystem::path& fileOnDisk,
  const std::filesystem::path& pathForSaving
) {
  namespace fs = std::filesystem;

  try {
    auto buffer = loader::loadFile(fileOnDisk);
    const auto serializedProfile = nlohmann::json::from_msgpack(buffer);

    UserProfile profile{pathForSaving, std::move(buffer)};

    profile.mSaveSlots = deserialize<data::SaveSlotArray>(
      serializedProfile.at("saveSlots"));
    profile.mHighScoreLists = deserialize<data::HighScoreListArray>(
      serializedProfile.at("highScoreLists"));

    // Older versions of RigelEngine stored options in the user profile
    // file. When running a newer version for the first time, we want to
    // import any settings from an earlier version.
    if (serializedProfile.contains("options")) {
      profile.mOptions = deserialize<data::GameOptions>(
        serializedProfile.at("options"));
    }

    if (serializedProfile.contains("gamePath")) {
      const auto gamePathStr =
        serializedProfile.at("gamePath").get<std::string>();
      profile.mGamePath = fs::u8path(gamePathStr);
    }

    {
      auto optionsFile = fileOnDisk;
      optionsFile.replace_filename(OPTIONS_FILENAME);
      loadOptionsFileIfPresent(optionsFile, profile.mOptions);
    }

    return profile;
  } catch (const std::exception& ex) {
    std::cerr << "WARNING: Failed to load user profile\n";
    std::cerr << ex.what() << '\n';
  }

  return UserProfile{pathForSaving};
}


UserProfile loadProfile(const std::filesystem::path& profileFile) {
  return loadProfile(profileFile, profileFile);
}

}


UserProfile::UserProfile(
  const std::filesystem::path& profilePath,
  loader::ByteBuffer originalJson
)
  : mProfilePath(profilePath)
  , mOriginalJson(std::move(originalJson))
{
}


void UserProfile::saveToDisk() {
  if (!mProfilePath) {
    return;
  }

  using json = nlohmann::json;

  json serializedProfile;
  serializedProfile["saveSlots"] = serialize(mSaveSlots);
  serializedProfile["highScoreLists"] = serialize(mHighScoreLists);

  // Starting with RigelEngine v.0.7.0, the options are stored in a separate
  // text file. For compatibility with older versions, the options are also
  // redundantly stored in the user profile, as before. But this is deprecated,
  // and will be removed in a later release at some point.
  const auto options = serialize(mOptions);
  serializedProfile["options"] = options;

  if (mGamePath) {
    serializedProfile["gamePath"] = mGamePath->u8string();
  }

  // This step merges the newly serialized profile into the 'old' profile
  // previously read from disk. The reason this is necessary is compatibility
  // between different versions of RigelEngine. An older version of RigelEngine
  // doesn't know about properties that are added in later versions. If we
  // would write serializedProfile to disk directly, we would therefore lose
  // any properties written by a newer version. Imagine a user has two versions
  // of RigelEngine installed, version A and B. Version B features some
  // additional options that are not present in A. Let's say the user configures
  // these options to their liking while running version B. The settings are
  // written to disk. Now the user launches version A. That version is not
  // aware of the additional settings, so it overwrites the profile on disk
  // and erases the user's settings. When the user launches version B again,
  // all these configuration settings will be reset to their defaults.
  //
  // This would be quite annoying, so we take some measures to prevent it from
  // happening. When reading the profile from disk, we keep the original JSON
  // data in addition to the deserialized C++ objects. When writing back to
  // disk, we merge our serializedProfile into the previously read JSON data.
  // This ensures that any settings present in the profile file are kept,
  // even if they are not part of the serializedProfile we are currently writing.
  if (mOriginalJson.size() > 0) {
    const auto previousProfile = json::from_msgpack(mOriginalJson);
    serializedProfile = merge(previousProfile, serializedProfile);
  }

  // Save user profile
  const auto buffer = json::to_msgpack(serializedProfile);
  try {
    loader::saveToFile(buffer, *mProfilePath);
  } catch (const std::exception&) {
    std::cerr << "WARNING: Failed to store user profile\n";
  }

  // Save options file
  {
    auto path = *mProfilePath;
    path.replace_filename(OPTIONS_FILENAME);

    auto optionsFile = std::ofstream(path.u8string());
    optionsFile << std::setw(4) << options;
  }
}


bool UserProfile::hasProgressData() const
{
  using std::any_of;
  using std::begin;
  using std::end;

  const auto hasSavedGames =
    any_of(begin(mSaveSlots), end(mSaveSlots), [](const auto& slot) {
      return slot.has_value();
    });

  const auto hasHighScores =
    any_of(begin(mHighScoreLists), end(mHighScoreLists), [](const auto& list) {
      return any_of(begin(list), end(list), [](const auto& entry) {
        return entry.mScore > 0;
      });
    });

  return hasSavedGames || hasHighScores;
}


std::optional<std::filesystem::path> createOrGetPreferencesPath() {
  namespace fs = std::filesystem;

  auto deleter = [](char* path) { SDL_free(path); };
  const auto pPreferencesDirName = std::unique_ptr<char, decltype(deleter)>{
    SDL_GetPrefPath(PREF_PATH_ORG_NAME, PREF_PATH_APP_NAME), deleter};

  if (!pPreferencesDirName) {
    return {};
  }

  return fs::u8path(std::string{pPreferencesDirName.get()});
}


UserProfile createEmptyUserProfile()
{
  const auto preferencesPath = createOrGetPreferencesPath();
  if (!preferencesPath) {
    std::cerr << "WARNING: Cannot open user preferences directory\n";
    return {};
  }

  const auto profileFilePath = *preferencesPath /
    (std::string{USER_PROFILE_BASE_NAME} + USER_PROFILE_FILE_EXTENSION);
  return UserProfile{profileFilePath};
}


std::optional<UserProfile> loadUserProfile()
{
  namespace fs = std::filesystem;

  const auto preferencesPath = createOrGetPreferencesPath();
  if (!preferencesPath) {
    std::cerr << "WARNING: Cannot open user preferences directory\n";
    return {};
  }

  const auto profileFilePath = *preferencesPath /
    (std::string{USER_PROFILE_BASE_NAME} + USER_PROFILE_FILE_EXTENSION);
  if (fs::exists(profileFilePath)) {
    return loadProfile(profileFilePath);
  }

  const auto profileFilePath_v1 = *preferencesPath / USER_PROFILE_FILENAME_V1;
  if (fs::exists(profileFilePath_v1)) {
    return loadProfile(profileFilePath_v1, profileFilePath);
  }

  return {};
}


void importOriginalGameProfileData(
  UserProfile& profile,
  const std::string& gamePath)
{
  profile.mSaveSlots = loader::loadSavedGames(gamePath);
  profile.mHighScoreLists = loader::loadHighScoreLists(gamePath);

  if (const auto options = loader::loadOptions(gamePath)) {
    importOptions(profile.mOptions, *options);
  }
}


UserProfile loadOrCreateUserProfile() {
  if (auto profile = loadUserProfile())
  {
    return *profile;
  }

  auto profile = createEmptyUserProfile();
  profile.saveToDisk();
  return profile;
}

}
