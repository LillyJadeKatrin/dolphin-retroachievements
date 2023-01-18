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
void Init();
void Login();
std::string Login(std::string password);
void StartSession(Memory::MemoryManager* memmgr);
void FetchData();
void ActivateAM();
void ActivateUnofficialAM();
void DoFrame();
void Award(unsigned int achievement_id);
void DeactivateAM();
void DeactivateUnofficialAM();
void EndSession();
void Logout();
void Shutdown();
} // namespace Achievements
