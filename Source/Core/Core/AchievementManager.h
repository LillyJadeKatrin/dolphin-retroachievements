// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// 
// 13 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#pragma once
#include "Core/HW/Memmap.h"

namespace Achievements
{
static bool sett_integration_enabled;
static std::string sett_username;
static std::string sett_api_token;
static bool sett_achievements_enabled;
static bool sett_leaderboards_enabled;
static bool sett_rich_presence_enabled;
static bool sett_hardcore_enabled;
static bool sett_badge_icons_enabled;
static bool sett_test_mode_enabled;
static bool sett_unofficial_enabled;

void Init();
void Login();
void StartSession(Memory::MemoryManager* memmgr);
void FetchData();
void ActivateAM();
void DoFrame();
void Award(unsigned int achievement_id);
void DeactivateAM();
void EndSession();
void Logout();
void Shutdown();
} // namespace Achievements
