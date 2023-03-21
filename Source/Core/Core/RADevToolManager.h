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

  //rc_runtime_t m_runtime{};
  //bool m_is_runtime_initialized = false;
  std::array<char, HASH_LENGTH> m_game_hash{};
  u32 m_game_id = 0;
  //rc_api_fetch_game_data_response_t m_game_data{};
  //bool m_is_game_loaded = false;
  //u64 m_last_ping_time = 0;
  //Common::EventHook m_do_frame_event;

  //Common::WorkQueueThread<std::function<void()>> m_queue;
  //std::recursive_mutex m_lock;
  Core::CPUThreadGuard* m_threadguard;

  std::string m_filename;
  bool m_raintegration_initialized = false;
};  // class RADevToolManager
