// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/RADevToolManager.h"

#include <rcheevos/include/rc_hash.h>

#include "Common/HttpRequest.h"
#include "Common/WorkQueueThread.h"
#include "Core/Config/AchievementSettings.h"
#include "Core/Core.h"
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
  //unsigned onegb = 0x10000000;
  RA_ClearMemoryBanks();
  int memory_bank_size = 0;
  if (Core::GetState() != Core::State::Uninitialized)
  {
    memory_bank_size = Core::System::GetInstance().GetMemory().GetRamSizeReal();
      //    memory_bank_size = onegb;
  }
  for (unsigned ix = 0; ix < 7; ix++)
  {
    RA_InstallMemoryBank(
        ix,
        [](unsigned int address) {
          return RADevToolManager::GetInstance()->RACallbackReadMemory(address);
        },
        [](unsigned int address, unsigned char value) {
          RADevToolManager::GetInstance()->RACallbackWriteMemory(address, value);
        },
        memory_bank_size);
    RA_InstallMemoryBankBlockReader(
        ix, [](unsigned int address, unsigned char* buffer, unsigned int bytes) {
          return RADevToolManager::GetInstance()->RACallbackReadBlock(address, buffer, bytes);
        });
  }
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

  if (m_threadguard)
    delete m_threadguard;

  if (Core::GetState() == Core::State::Uninitialized)
  {
    m_game_id = 0;
    return;
  }
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

int RADevToolManager::RACallbackIsActive()
{
  return m_game_id;
}

void RADevToolManager::RACallbackCauseUnpause()
{
  if (Core::GetState() != Core::State::Uninitialized)
    Core::SetState(Core::State::Running);
}

void RADevToolManager::RACallbackCausePause()
{
  if (Core::GetState() != Core::State::Uninitialized)
    Core::SetState(Core::State::Paused);
}

void RADevToolManager::RACallbackRebuildMenu()
{
  // unused
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
  const rc_memory_regions_t* regions = rc_console_memory_regions(ConsoleID::GameCube);
  for (u32 ix = 0; ix < regions->num_regions; ix++)
  {
    if (address >= regions->region[ix].start_address && address <= regions->region[ix].end_address)
    {
      address += (regions->region[ix].real_address - regions->region[ix].start_address);
      break;
    }
  }
  return Core::System::GetInstance()
      .GetMMU()
      .HostTryReadU8(*m_threadguard, address)
      .value_or(PowerPC::ReadResult<u8>(false, 0u))
      .value;
}

unsigned int RADevToolManager::RACallbackReadBlock(unsigned int address, unsigned char* buffer,
                                                     unsigned int bytes)
{
  const rc_memory_regions_t* regions = rc_console_memory_regions(ConsoleID::GameCube);
  for (u32 ix = 0; ix < regions->num_regions; ix++)
  {
    if (address >= regions->region[ix].start_address && address <= regions->region[ix].end_address)
    {
      address += (regions->region[ix].real_address - regions->region[ix].start_address);
      break;
    }
  }
  unsigned int bytes_read = 0;
  for (bytes_read = 0; bytes_read < bytes; bytes_read++)
  {
    buffer[bytes_read] = Core::System::GetInstance()
                             .GetMMU()
                             .HostTryReadU8(*m_threadguard, address + bytes_read)
                             .value_or(PowerPC::ReadResult<u8>(false, 0u))
                             .value;
  }
  return bytes_read;
}

void RADevToolManager::RACallbackWriteMemory(unsigned int address, unsigned char value)
{
  const rc_memory_regions_t* regions = rc_console_memory_regions(ConsoleID::GameCube);
  for (u32 ix = 0; ix < regions->num_regions; ix++)
  {
    if (address >= regions->region[ix].start_address && address <= regions->region[ix].end_address)
    {
      address += (regions->region[ix].real_address - regions->region[ix].start_address);
      break;
    }
  }
  Core::System::GetInstance().GetMMU().HostTryWriteU8(*m_threadguard, value, address);
}
