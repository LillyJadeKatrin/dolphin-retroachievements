// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// 
// 13 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#pragma once
#include "Core/HW/Memmap.h"

#include "rcheevos/include/rc_runtime.h"
#include <rcheevos/include/rc_api_runtime.h>
#include <rcheevos/include/rc_api_user.h>

struct rc_api_achievement_definition_t;

namespace Achievements
{
void Init();
void Login();
std::string Login(std::string password);
void StartSession(Memory::MemoryManager* memmgr);
void FetchData();
void ActivateAM();
void ActivateUnofficialAM();

void DoFrame();
void DoState(PointerWrap& p);
void Award(unsigned int achievement_id);

rc_api_fetch_game_data_response_t* GameData();

void DeactivateAM();
void DeactivateUnofficialAM();
void EndSession();
void Logout();
void Shutdown();
} // namespace Achievements

// #ifdef ENABLE_RAINTEGRATION
namespace Achievements::RAIntegration
{
void ReinstallMemoryBanks();
void MainWindowChanged(void* new_handle);
void GameChanged(bool isWii);
std::vector<std::tuple<int, std::string, bool>> GetMenuItems();
void ActivateMenuItem(int item);
}  // namespace Achievements::RAIntegration
// #endif
