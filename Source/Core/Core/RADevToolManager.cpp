// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/RADevToolManager.h"

#include <fmt/format.h>

#include <rcheevos/include/rc_hash.h>

#include "Common/HttpRequest.h"
#include "Common/Logging/Log.h"
#include "Common/WorkQueueThread.h"
#include "Core/Config/AchievementSettings.h"
#include "Core/Core.h"
#include "Core/HW/CPU.h"
#include "Core/PowerPC/MMU.h"
#include "Core/System.h"
#include "DiscIO/Volume.h"
#include "VideoCommon/VideoEvents.h"

static constexpr bool hardcore_mode_enabled = false;

#include <Windows.h>
#include <cstring>
#include "Common/scmrev.h"
#include "Core.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/ProcessorInterface.h"
#include "RAInterface/RA_Consoles.h"
#include "RAInterface/RA_Interface.h"
#include "System.h"

RADevToolManager* RADevToolManager::GetInstance()
{
  static RADevToolManager s_instance;
  return &s_instance;
}

void RADevToolManager::InitializeRAIntegration(void* main_window_handle)
{
  RA_InitClient((HWND)main_window_handle, "Dolphin", SCM_DESC_STR);
  RA_SetUserAgentDetail(std::format("Dolphin {} {}", SCM_DESC_STR, SCM_BRANCH_STR).c_str());

  RA_InstallSharedFunctions(
      []() { return RADevToolManager::GetInstance()->RACallbackIsActive(); },
      []() { RADevToolManager::GetInstance()->RACallbackCauseUnpause(); },
      []() { RADevToolManager::GetInstance()->RACallbackCausePause(); },
      []() { RADevToolManager::GetInstance()->RACallbackRebuildMenu(); },
      [](char* buf) { RADevToolManager::GetInstance()->RACallbackEstimateTitle(buf); },
      []() { RADevToolManager::GetInstance()->RACallbackResetEmulator(); },
      [](const char* unused) { RADevToolManager::GetInstance()->RACallbackLoadROM(unused); });

  // EE physical memory and scratchpad are currently exposed (matching direct rcheevos
  // implementation).
  ReinstallMemoryBanks();

  m_raintegration_initialized = true;

  RA_AttemptLogin(0);

  // this is pretty lame, but we may as well persist until we exit anyway
  std::atexit(RA_Shutdown);
}

void RADevToolManager::ReinstallMemoryBanks()
{
  RA_ClearMemoryBanks();
  m_modified_addresses.clear();
  int memory_bank_size = 0;
  if (Core::GetState() != Core::State::Uninitialized)
  {
    memory_bank_size = Core::System::GetInstance().GetMemory().GetRamSizeReal();
    m_cloned_memory.resize(memory_bank_size);
  }
  RA_InstallMemoryBank(
      0,
      [](unsigned int address) {
        return RADevToolManager::GetInstance()->RACallbackReadMemory(address);
      },
      [](unsigned int address, unsigned char value) {
        RADevToolManager::GetInstance()->RACallbackWriteMemory(address, value);
      },
      memory_bank_size);
  RA_InstallMemoryBankBlockReader(
      0, [](unsigned int address, unsigned char* buffer, unsigned int bytes) {
        return RADevToolManager::GetInstance()->RACallbackReadBlock(address, buffer, bytes);
      });
}

void RADevToolManager::LoadGame(const std::string& iso_path)
{
  struct FilereaderState
  {
    int64_t position = 0;
    std::unique_ptr<DiscIO::Volume> volume;
  };
  rc_hash_filereader volume_reader{
      .open = [](const char* path_utf8) -> void* {
        auto state = std::make_unique<FilereaderState>();
        state->volume = DiscIO::CreateVolume(path_utf8);
        if (!state->volume)
          return nullptr;
        return state.release();
      },
      .seek =
          [](void* file_handle, int64_t offset, int origin) {
            switch (origin)
            {
            case SEEK_SET:
              reinterpret_cast<FilereaderState*>(file_handle)->position = offset;
              break;
            case SEEK_CUR:
              reinterpret_cast<FilereaderState*>(file_handle)->position += offset;
              break;
            case SEEK_END:
              // Unused
              break;
            }
          },
      .tell =
          [](void* file_handle) {
            return reinterpret_cast<FilereaderState*>(file_handle)->position;
          },
      .read =
          [](void* file_handle, void* buffer, size_t requested_bytes) {
            FilereaderState* filereader_state = reinterpret_cast<FilereaderState*>(file_handle);
            bool success = (filereader_state->volume->Read(
                filereader_state->position, requested_bytes, reinterpret_cast<u8*>(buffer),
                DiscIO::PARTITION_NONE));
            if (success)
            {
              filereader_state->position += requested_bytes;
              return requested_bytes;
            }
            else
            {
              return static_cast<size_t>(0);
            }
          },
      .close = [](void* file_handle) { delete reinterpret_cast<FilereaderState*>(file_handle); }};
  rc_hash_init_custom_filereader(&volume_reader);
  rc_hash_generate_from_file(m_game_hash.data(), RC_CONSOLE_GAMECUBE, iso_path.c_str());
}

void RADevToolManager::MainWindowChanged(void* new_handle)
{
  if (m_raintegration_initialized)
  {
    RA_UpdateHWnd((HWND)new_handle);
    return;
  }

  InitializeRAIntegration(new_handle);
}

void RADevToolManager::GameChanged(bool isWii)
{
  ReinstallMemoryBanks();

  if (Core::GetState() == Core::State::Uninitialized || Core::GetState() == Core::State::Stopping)
  {
    m_game_id = 0;
    return;
  }

  if (m_threadguard == nullptr)
    m_threadguard = new Core::CPUThreadGuard(Core::System::GetInstance());

  //  Must call this before calling RA_IdentifyHash
  RA_SetConsoleID(isWii ? WII : GameCube);

  m_game_id = RA_IdentifyHash(m_game_hash.data());
  if (m_game_id != 0)
  {
    RA_ActivateGame(m_game_id);
    //    m_do_frame_event =
    //      AfterFrameEvent::Register([this] { RA_DoAchievementsFrame(); }, "RADevToolManager");
  }
}

void RADevToolManager::RAIDoFrame()
{
  for (u32 modified_address : m_modified_addresses)
  {
    if (modified_address >= m_cloned_memory.size())
      continue;
    Core::System::GetInstance().GetMemory().CopyToEmu(modified_address,
                                                      m_cloned_memory.data() + modified_address, 1);
  }
  m_modified_addresses.clear();
  Core::System::GetInstance().GetMemory().CopyFromEmu(
      m_cloned_memory.data(), RA2EmuAddress(0), m_cloned_memory.size());
  RA_DoAchievementsFrame();
}

bool WideStringToUTF8String(std::string& dest, const std::wstring_view& str)
{
  int mblen = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), nullptr,
                                  0, nullptr, nullptr);
  if (mblen < 0)
    return false;

  dest.resize(mblen);
  if (mblen > 0 && WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.length()),
                                       dest.data(), mblen, nullptr, nullptr) < 0)
  {
    return false;
  }

  return true;
}

std::string WideStringToUTF8String(const std::wstring_view& str)
{
  std::string ret;
  if (!WideStringToUTF8String(ret, str))
    ret.clear();

  return ret;
}

std::vector<std::tuple<int, std::string, bool>> RADevToolManager::GetMenuItems()
{
  std::array<RA_MenuItem, 64> items;
  const int num_items = RA_GetPopupMenuItems(items.data());

  std::vector<std::tuple<int, std::string, bool>> ret;
  ret.reserve(static_cast<u32>(num_items));

  for (int i = 0; i < num_items; i++)
  {
    const RA_MenuItem& it = items[i];
    if (!it.sLabel)
    {
      // separator
      ret.emplace_back(0, std::string(), false);
    }
    else
    {
      // option, maybe checkable
      //      ret.emplace_back(static_cast<int>(it.nID), it.sLabel, it.bChecked);
      ret.emplace_back(static_cast<int>(it.nID), WideStringToUTF8String(it.sLabel), it.bChecked);
    }
  }

  return ret;
}

void RADevToolManager::ActivateMenuItem(int item)
{
  RA_InvokeDialog(item);
}

void RADevToolManager::SetRefreshMenuCallback(std::function<void(void*)> callback, void* callback_object)
{
  m_rebuild_callback = std::move(callback);
  m_rebuild_callback_object = std::move(callback_object);
}

int RADevToolManager::RACallbackIsActive()
{
  return m_game_id;
}

void RADevToolManager::RACallbackCauseUnpause()
{
  Core::System::GetInstance().GetCPU().Continue();
}

void RADevToolManager::RACallbackCausePause()
{
  Core::System::GetInstance().GetCPU().Break();
}

void RADevToolManager::RACallbackRebuildMenu()
{
  if (m_rebuild_callback_object)
    m_rebuild_callback(m_rebuild_callback_object);
}

void RADevToolManager::RACallbackEstimateTitle(char* buf)
{
  strcpy(buf, m_filename.c_str());
}

void RADevToolManager::RACallbackResetEmulator()
{
  Core::Stop();
}

void RADevToolManager::RACallbackLoadROM(const char* unused)
{
  // unused
}

unsigned char RADevToolManager::RACallbackReadMemory(unsigned int address)
{
  return m_cloned_memory.size() > address ? m_cloned_memory[address] : 0;
}

unsigned int RADevToolManager::RACallbackReadBlock(unsigned int address, unsigned char* buffer,
                                                   unsigned int bytes)
{
  if (m_cloned_memory.size() <= address)
    return 0;
  unsigned int bytes_read = 0;
  for (bytes_read = 0; bytes_read < bytes && bytes_read < m_cloned_memory.size(); bytes_read++)
  {
    buffer[bytes_read] = m_cloned_memory[address + bytes_read];
  }
  return bytes_read;
}

void RADevToolManager::RACallbackWriteMemory(unsigned int address, unsigned char value)
{
  if (m_cloned_memory.size() <= address)
    return;
  m_modified_addresses.insert(address);
  m_cloned_memory[address] = value;
}

u32 RADevToolManager::RA2EmuAddress(unsigned int address)
{
  const rc_memory_regions_t* regions = rc_console_memory_regions(ConsoleID::GameCube);
  for (u32 ix = 0; ix < regions->num_regions; ix++)
  {
    if (address >= regions->region[ix].start_address && address <= regions->region[ix].end_address)
    {
      return (u32)address + (regions->region[ix].real_address - regions->region[ix].start_address);
    }
  }
  return 0;
}
