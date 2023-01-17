// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// 13 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#include "Core/Config/AchievementSettings.h"

#include <string>

#include "Common/Config/Config.h"
#include "Core/AchievementManager.h"

namespace Config
{
// Configuration Information
const Info<bool> RA_INTEGRATION_ENABLED{{System::Achievements, "General", "IntegrationEnabled"}, false};
const Info<bool> RA_ACHIEVEMENTS_ENABLED{{System::Achievements, "General", "AchievementsEnabled"},
                                         false};
const Info<bool> RA_LEADERBOARDS_ENABLED{{System::Achievements, "General", "LeaderboardsEnabled"},
                                         false};
const Info<bool> RA_RICH_PRESENCE_ENABLED{{System::Achievements, "General", "RichPresenceEnabled"},
                                          false};
const Info<bool> RA_HARDCORE_ENABLED{{System::Achievements, "General", "HardcoreEnabled"}, false};
const Info<bool> RA_BADGE_ICONS_ENABLED{{System::Achievements, "General", "BadgeIconsEnabled"},
                                        false};
const Info<bool> RA_TEST_MODE_ENABLED{{System::Achievements, "General", "TestModeEnabled"}, false};
const Info<bool> RA_UNOFFICIAL_ENABLED{{System::Achievements, "General", "UnofficialEnabled"},
                                       false};
const Info<bool> RA_ENCORE_ENABLED{{System::Achievements, "General", "EncoreEnabled"}, false};

namespace Ach
{
void EnableRAIntegration()
{
  Config::SetBaseOrCurrent(Config::RA_INTEGRATION_ENABLED, true);
  Achievements::Init();
}
void DisableRAIntegration()
{
  Config::SetBaseOrCurrent(Config::RA_INTEGRATION_ENABLED, false);
  Achievements::Shutdown();
}
void EnableAchievements()
{
  Config::SetBaseOrCurrent(Config::RA_ACHIEVEMENTS_ENABLED, true);
  Achievements::ActivateAM();
}
void DisableAchievements()
{
  Config::SetBaseOrCurrent(Config::RA_ACHIEVEMENTS_ENABLED, false);
  Achievements::DeactivateAM();
}
void EnableLeaderboards()
{
  Config::SetBaseOrCurrent(Config::RA_LEADERBOARDS_ENABLED, true);
  //Achievements::ActivateLB();
}
void DisableLeaderboards()
{
  Config::SetBaseOrCurrent(Config::RA_LEADERBOARDS_ENABLED, false);
  //Achievements::DeactivateLB();
}
void EnableRichPresence()
{
  Config::SetBaseOrCurrent(Config::RA_RICH_PRESENCE_ENABLED, true);
  //Achievements::ActivateRP();
}
void DisableRichPresence()
{
  Config::SetBaseOrCurrent(Config::RA_RICH_PRESENCE_ENABLED, false);
  //Achievements::DeactivateRP();
}
void EnableHardcore()
{
  Config::SetBaseOrCurrent(Config::RA_HARDCORE_ENABLED, true);
}
void DisableHardcore()
{
  Config::SetBaseOrCurrent(Config::RA_HARDCORE_ENABLED, false);
}
void EnableBadgeIcons()
{
  Config::SetBaseOrCurrent(Config::RA_BADGE_ICONS_ENABLED, true);
  Achievements::FetchData();
}
void DisableBadgeIcons()
{
  Config::SetBaseOrCurrent(Config::RA_BADGE_ICONS_ENABLED, false);
  // Because it takes a substantial amount of time and bandwidth to
  // reload all those badge icons, Dolphin will retain them in memory
  // as long as the game session is active.
}
void EnableTestMode()
{
  Config::SetBaseOrCurrent(Config::RA_TEST_MODE_ENABLED, true);
}
void DisableTestMode()
{
  Config::SetBaseOrCurrent(Config::RA_TEST_MODE_ENABLED, false);
}
void EnableUnofficial()
{
  Config::SetBaseOrCurrent(Config::RA_UNOFFICIAL_ENABLED, true);
  Achievements::ActivateUnofficialAM();
}
void DisableUnofficial()
{
  Config::SetBaseOrCurrent(Config::RA_UNOFFICIAL_ENABLED, false);
  Achievements::DeactivateUnofficialAM();
}
void EnableEncore()
{
  Config::SetBaseOrCurrent(Config::RA_ENCORE_ENABLED, true);
}
void DisableEncore()
{
  Config::SetBaseOrCurrent(Config::RA_ENCORE_ENABLED, false);
}
}  // namespace Ach
}  // namespace Config
