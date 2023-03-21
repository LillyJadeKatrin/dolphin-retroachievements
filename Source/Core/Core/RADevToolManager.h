// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <rcheevos/include/rc_api_runtime.h>
#include <rcheevos/include/rc_api_user.h>
#include <rcheevos/include/rc_runtime.h>

#include "Common/Event.h"
#include "Common/HookableEvent.h"
#include "Common/WorkQueueThread.h"
#include "Core/HW/Memmap.h"

class RADevToolManager
{
public:
  static RADevToolManager* GetInstance();

  void InitializeRAIntegration(void* main_window_handle);
  void ReinstallMemoryBanks();
  void LoadGame(const std::string& iso_path);
  void MainWindowChanged(void* new_handle);
  void GameChanged(bool isWii);
  void RAIDoFrame();
  std::vector<std::tuple<int, std::string, bool>> GetMenuItems();
  void ActivateMenuItem(int item);

private:
  RADevToolManager() = default;

  static constexpr int HASH_LENGTH = 33;

  int RACallbackIsActive();
  void RACallbackCauseUnpause();
  void RACallbackCausePause();
  void RACallbackRebuildMenu();
  void RACallbackEstimateTitle(char* buf);
  void RACallbackResetEmulator();
  void RACallbackLoadROM(const char* unused);
  unsigned char RACallbackReadMemory(unsigned int address);
  unsigned int RACallbackReadBlock(unsigned int address, unsigned char* buffer, unsigned int bytes);
  void RACallbackWriteMemory(unsigned int address, unsigned char value);

  bool m_raintegration_initialized = false;
  std::array<char, HASH_LENGTH> m_game_hash{};
  u32 m_game_id = 0;
  std::string m_filename;
  Core::CPUThreadGuard* m_threadguard;
};  // class RADevToolManager
