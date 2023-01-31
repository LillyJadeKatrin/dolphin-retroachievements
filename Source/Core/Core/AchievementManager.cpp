// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// 13 JAN 2023 - Lilly Jade Katrin - lilly.kitty.1988@gmail.com
// Thanks to Stenzek and the PCSX2 project for inspiration, assistance and examples,
// and to TheFetishMachine and Infernum for encouragement and cheerleading

#include "AchievementManager.h"
#include "rcheevos/include/rc_runtime.h"
#include <rcheevos/include/rc_api_runtime.h>
#include <rcheevos/include/rc_api_user.h>

#include "Common/ChunkFile.h"
#include "Core/HW/Memmap.h"
#include "VideoCommon/OnScreenDisplay.h"
#include <iostream>
#include <Common/HttpRequest.h>
#include <VideoCommon/TextureConfig.h>
#include <VideoBackends/Software/SWTexture.h>
#include "Config/AchievementSettings.h"

namespace Achievements
{
static rc_runtime_t runtime{};
static bool is_runtime_initialized = false;
static rc_api_login_response_t login_data{};
static rc_api_start_session_response_t session_data{};
static rc_api_fetch_game_data_response_t game_data{};
static rc_api_fetch_user_unlocks_response_t hardcore_unlock_data{};
static rc_api_fetch_user_unlocks_response_t softcore_unlock_data{};

static Memory::MemoryManager* memory_manager = nullptr;

static std::multiset<unsigned int> hardcore_unlocks;
static std::multiset<unsigned int> softcore_unlocks;
static std::map<unsigned int, std::vector<u8>> unlocked_icons;
static std::map<unsigned int, std::vector<u8>> locked_icons;
std::vector<u8> game_icon;
std::vector<u8> user_icon;

// TODO lillyjade: Temporary hardcoded test data - CLEAN BEFORE PUSHING
static unsigned int game_id = 3417;
static const char* game_hash = "ada3c364c783021884b066a4ad7ee49c";
static unsigned int partial_list_limit = 3;

namespace  // Hide from use outside this file
{
template <typename RcRequest, typename RcResponse>
void Request(RcRequest rc_request, RcResponse* rc_response,
             int (*init_request)(rc_api_request_t* request, const RcRequest* api_params),
             int (*process_response)(RcResponse* response, const char* server_response))
{
  rc_api_request_t api_request;
  Common::HttpRequest http_request;
  init_request(&api_request, &rc_request);
  auto http_response = http_request.Post(api_request.url, api_request.post_data);
  if (http_response.has_value() && http_response.value().size() > 0)
  {
    char* response_str = (char*)calloc(http_response.value().size() + 1, 1);
    for (int ix = 0; ix < http_response.value().size(); ix++)
    {
      response_str[ix] = (char)http_response.value()[ix];
    }
    process_response(rc_response, (const char*)response_str);
    free(response_str);
  }
}

void IconRequest(rc_api_fetch_image_request_t rc_request, std::vector<u8> &icon_buff)
{
  icon_buff.clear();
  rc_api_request_t api_request;
  Common::HttpRequest http_request;
  rc_api_init_fetch_image_request(&api_request, &rc_request);
  auto http_response = http_request.Get(api_request.url);
  if (http_response.has_value() && http_response.value().size() > 0)
    icon_buff = http_response.value();
}

unsigned MemoryPeeker(unsigned address, unsigned num_bytes, void* ud)
{
  // TODO lillyjade: Hack to spoof SA2B as SA2
  if (address == 0x24ee60) // state
  //  address = 0x3BD81B;
    address = 0x803AD81B;
  if (address == 0x24ee64) // level ID
  //  address = 0x3BD821;
  //  address = 0x803AD821;
    return 0x0d;
  if (address == 0x26a1fe)
    return 1;
  if (address == 0x26adb8) // score
  //  address = 0x1F8263;
  //  address = 0x801E8263;
    address = 0x801EEC07;
  switch (num_bytes)
  {
  case 1:
    return memory_manager->Read_U8(address);
  case 2:
    return memory_manager->Read_U16(address);
  case 4:
    return memory_manager->Read_U32(address);
  case 8:
    return memory_manager->Read_U64(address);
  default:
    return 0u;
  }
}

void DisplayUnlocked(unsigned int achievement_id)
{
  for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
  {
    if (game_data.achievements[ix].id == achievement_id)
    {
      OSD::AddMessage(std::format("Unlocked: {} ({})", game_data.achievements[ix].title,
                                  game_data.achievements[ix].points),
                      OSD::Duration::VERY_LONG, OSD::Color::GREEN,
                      (Config::Get(Config::RA_BADGE_ICONS_ENABLED))
                          ?(&(*unlocked_icons[game_data.achievements[ix].id].begin()))
                          :(nullptr));
    }
  }
}

void AchievementEventHandler(const rc_runtime_event_t* runtime_event)
{
  std::cout << std::endl;
  switch (runtime_event->type)
  {
  case RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED:
    if (Config::Get(Config::RA_HARDCORE_ENABLED))
    {
      hardcore_unlocks.insert(runtime_event->id);
    }
    else
    {
      softcore_unlocks.insert(runtime_event->id);
    }
    Award(runtime_event->id);
    DisplayUnlocked(runtime_event->id);
    break;
  }
}
} // empty (private) namespace

void Init()
{
  if (!is_runtime_initialized && Config::Get(Config::RA_INTEGRATION_ENABLED))
  {
    rc_runtime_init(&runtime);
    is_runtime_initialized = true;
  }
}

void Login()
{
  if (!Config::Get(Config::RA_INTEGRATION_ENABLED) || !is_runtime_initialized ||
      Config::Get(Config::RA_USERNAME).empty() || Config::Get(Config::RA_API_TOKEN).empty())
    return;
  std::string username = Config::Get(Config::RA_USERNAME);
  std::string api_token = Config::Get(Config::RA_API_TOKEN);
  rc_api_login_request_t login_request = {.username = username.c_str(),
                                          .api_token = api_token.c_str()};
  Request<rc_api_login_request_t, rc_api_login_response_t>(
      login_request, &login_data, rc_api_init_login_request, rc_api_process_login_response);
  rc_api_fetch_image_request_t icon_request = {.image_name = login_data.username,
                                               .image_type = RC_IMAGE_TYPE_USER};
  if (Config::Get(Config::RA_BADGE_ICONS_ENABLED) && login_data.response.succeeded)
    IconRequest(icon_request, user_icon);
}

std::string Login(std::string password)
{
  if (!Config::Get(Config::RA_INTEGRATION_ENABLED) || !is_runtime_initialized ||
      Config::Get(Config::RA_USERNAME).empty() || password.empty())
    return "";
  std::string username = Config::Get(Config::RA_USERNAME);
  rc_api_login_request_t login_request = {.username = username.c_str(),
                                          .password = password.c_str()};
  Request<rc_api_login_request_t, rc_api_login_response_t>(
      login_request, &login_data, rc_api_init_login_request, rc_api_process_login_response);
  rc_api_fetch_image_request_t icon_request = {.image_name = login_data.username,
                                               .image_type = RC_IMAGE_TYPE_USER};
  if (!login_data.response.succeeded)
    return "";
  if (Config::Get(Config::RA_BADGE_ICONS_ENABLED))
    IconRequest(icon_request, user_icon);
  return std::string(login_data.api_token);
}

void StartSession(Memory::MemoryManager* memmgr)
{
  if (!Config::Get(Config::RA_INTEGRATION_ENABLED) || !is_runtime_initialized || !login_data.response.succeeded)
    return;
  rc_api_start_session_request_t start_session_request = {
      .username = login_data.username, .api_token = login_data.api_token, .game_id = game_id};
  Request<rc_api_start_session_request_t, rc_api_start_session_response_t>(
      start_session_request, &session_data,
      rc_api_init_start_session_request, rc_api_process_start_session_response);
  memory_manager = memmgr;
}

void FetchData()
{
  if (!Config::Get(Config::RA_INTEGRATION_ENABLED) || !is_runtime_initialized ||
      !login_data.response.succeeded || !session_data.response.succeeded)
    return;
  if (!game_data.response.succeeded)
  {
    rc_api_fetch_game_data_request_t fetch_data_request = {
        .username = login_data.username, .api_token = login_data.api_token, .game_id = game_id};
    Request<rc_api_fetch_game_data_request_t, rc_api_fetch_game_data_response_t>(
        fetch_data_request, &game_data, rc_api_init_fetch_game_data_request,
        rc_api_process_fetch_game_data_response);
    rc_api_fetch_user_unlocks_request_t fetch_unlocks_request = {.username = login_data.username,
                                                                 .api_token = login_data.api_token,
                                                                 .game_id = game_id,
                                                                 .hardcore = true};
    Request<rc_api_fetch_user_unlocks_request_t, rc_api_fetch_user_unlocks_response_t>(
        fetch_unlocks_request, &hardcore_unlock_data, rc_api_init_fetch_user_unlocks_request,
        rc_api_process_fetch_user_unlocks_response);
    fetch_unlocks_request.hardcore = false;
    Request<rc_api_fetch_user_unlocks_request_t, rc_api_fetch_user_unlocks_response_t>(
        fetch_unlocks_request, &softcore_unlock_data, rc_api_init_fetch_user_unlocks_request,
        rc_api_process_fetch_user_unlocks_response);
  }
  rc_api_fetch_image_request_t icon_request = {.image_name = game_data.image_name,
                                               .image_type = RC_IMAGE_TYPE_GAME};
  if (Config::Get(Config::RA_BADGE_ICONS_ENABLED))
  {
    IconRequest(icon_request, game_icon);
    for (unsigned int ix = 0; ix < partial_list_limit; ix++)
    //      for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
    {
      rc_api_fetch_image_request_t badge_request = {.image_name =
                                                        game_data.achievements[ix].badge_name,
                                                    .image_type = RC_IMAGE_TYPE_ACHIEVEMENT};
      if (unlocked_icons[game_data.achievements[ix].id].empty())
        IconRequest(badge_request, unlocked_icons[game_data.achievements[ix].id]);
      badge_request.image_type = RC_IMAGE_TYPE_ACHIEVEMENT_LOCKED;
      if (locked_icons[game_data.achievements[ix].id].empty())
        IconRequest(badge_request, locked_icons[game_data.achievements[ix].id]);
    }
  }
}

void ActivateAM()
{
  if (!Config::Get(Config::RA_INTEGRATION_ENABLED) || !is_runtime_initialized ||
      !login_data.response.succeeded || !session_data.response.succeeded ||
      !game_data.response.succeeded || !Config::Get(Config::RA_ACHIEVEMENTS_ENABLED))
    return;
  // TODO lillyjade: only loading the first cheevo for testing purposes
  // for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
  for (unsigned int ix = 0; ix < partial_list_limit; ix++)
  {
    if (game_data.achievements[ix].category == RC_ACHIEVEMENT_CATEGORY_CORE ||
      Config::Get(Config::RA_UNOFFICIAL_ENABLED))
      rc_runtime_activate_achievement(&runtime, game_data.achievements[ix].id,
                                      game_data.achievements[ix].definition, nullptr, 0);
  }
}

void ActivateUnofficialAM()
{
  if (!Config::Get(Config::RA_INTEGRATION_ENABLED) || !is_runtime_initialized ||
      !login_data.response.succeeded || !session_data.response.succeeded ||
      !game_data.response.succeeded || !Config::Get(Config::RA_ACHIEVEMENTS_ENABLED)
      || !Config::Get(Config::RA_UNOFFICIAL_ENABLED))
    return;
  // TODO lillyjade: only loading the first cheevo for testing purposes
  // for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
  for (unsigned int ix = 0; ix < partial_list_limit; ix++)
  {
    if (game_data.achievements[ix].category == RC_ACHIEVEMENT_CATEGORY_UNOFFICIAL)
      rc_runtime_activate_achievement(&runtime, game_data.achievements[ix].id,
                                      game_data.achievements[ix].definition, nullptr, 0);
  }
}

void DoFrame()
{
  if (!Config::Get(Config::RA_INTEGRATION_ENABLED) || !is_runtime_initialized ||
      !login_data.response.succeeded ||
      !session_data.response.succeeded || !game_data.response.succeeded)
    return;
  rc_runtime_do_frame(&runtime, &AchievementEventHandler, &MemoryPeeker, nullptr, nullptr);
}

void DoState(PointerWrap& p)
{
  p.Do(runtime.triggers);
  p.Do(runtime.trigger_count);
  p.Do(runtime.trigger_capacity);
  p.Do(runtime.lboards);
  p.Do(runtime.lboard_count);
  p.Do(runtime.lboard_capacity);
  p.Do(runtime.richpresence);
  p.Do(runtime.memrefs);
  p.Do(runtime.next_memref);
  p.Do(runtime.variables);
  p.Do(runtime.next_variable);
}

void Award(unsigned int achievement_id)
{
  if (!Config::Get(Config::RA_INTEGRATION_ENABLED) || !is_runtime_initialized ||
      !login_data.response.succeeded || !session_data.response.succeeded ||
      !game_data.response.succeeded || !Config::Get(Config::RA_ACHIEVEMENTS_ENABLED))
    return;
  rc_api_award_achievement_request_t award_request = {
      .username = login_data.username,
      .api_token = login_data.api_token,
      .achievement_id = achievement_id,
      .hardcore = 0,
      .game_hash = game_hash};
  rc_api_award_achievement_response_t award_response = {};
  Request<rc_api_award_achievement_request_t, rc_api_award_achievement_response_t>(
      award_request, &award_response, rc_api_init_award_achievement_request,
      rc_api_process_award_achievement_response);
}

rc_api_login_response_t* GetUserStatus()
{
  return &login_data;
}

const std::vector<u8>* GetUserIcon()
{
  return &user_icon;
}

rc_api_fetch_user_unlocks_response_t* GetHardcoreGameProgress()
{
  return &hardcore_unlock_data;
}

rc_api_fetch_user_unlocks_response_t* GetSoftcoreGameProgress()
{
  return &softcore_unlock_data;
}

const std::vector<u8>* GetGameIcon()
{
  return &game_icon;
}

rc_api_fetch_game_data_response_t* GetGameData()
{
  return &game_data;
}

int GetHardcoreAchievementStatus(unsigned int id)
{
  return (int)hardcore_unlocks.count(id);
}

int GetSoftcoreAchievementStatus(unsigned int id)
{
  return (int)softcore_unlocks.count(id);
}

const std::vector<u8>* GetAchievementBadge(unsigned int id, bool locked)
{
  return (locked) ? (&locked_icons[id]) : (&unlocked_icons[id]);
}

void GetAchievementProgress(unsigned int id, unsigned* value, unsigned* target)
{
  rc_runtime_get_achievement_measured(&runtime, id, value, target);
}

void DeactivateAM()
{
  for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
  {
    rc_runtime_deactivate_achievement(&runtime, game_data.achievements[ix].id);
  }
}

void DeactivateUnofficialAM()
{
  for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
  {
    if (game_data.achievements[ix].category == RC_ACHIEVEMENT_CATEGORY_UNOFFICIAL)
      rc_runtime_deactivate_achievement(&runtime, game_data.achievements[ix].id);
  }
}

void EndSession()
{
  for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
  {
    unlocked_icons[game_data.achievements[ix].id].clear();
    locked_icons[game_data.achievements[ix].id].clear();
  }
  unlocked_icons.clear();
  locked_icons.clear();
  game_icon.clear();
  if (softcore_unlock_data.response.succeeded)
  {
    rc_api_destroy_fetch_user_unlocks_response(&softcore_unlock_data);
  }
  if (hardcore_unlock_data.response.succeeded)
  {
    rc_api_destroy_fetch_user_unlocks_response(&hardcore_unlock_data);
  }
  if (game_data.response.succeeded)
  {
    rc_api_destroy_fetch_game_data_response(&game_data);
  }
  if (session_data.response.succeeded)
  {
    rc_api_destroy_start_session_response(&session_data);
  }
}

void Logout()
{
  EndSession();
  user_icon.clear();
  if (login_data.response.succeeded)
  {
    rc_api_destroy_login_response(&login_data);
  }
}

void Shutdown()
{
  Logout();
  if (is_runtime_initialized)
  {
    rc_runtime_destroy(&runtime);
  }
}

}; // namespace Achievements

//#ifdef ENABLE_RAINTEGRATION
// RA_Interface ends up including windows.h, with its silly macros.
#include <Windows.h>
//#include "common/RedtapeWindows.h"
#include "RA_Interface.h"
#include "RA_Consoles.h"
#include "Core.h"
#include "System.h"
#include "Core/HW/ProcessorInterface.h"
#include <cstring>
#include "Common/scmrev.h"

namespace Achievements::RAIntegration
{
static void InitializeRAIntegration(void* main_window_handle);

static int RACallbackIsActive();
static void RACallbackCauseUnpause();
static void RACallbackCausePause();
static void RACallbackRebuildMenu();
static void RACallbackEstimateTitle(char* buf);
static void RACallbackResetEmulator();
static void RACallbackLoadROM(const char* unused);
static unsigned char RACallbackReadMemory(unsigned int address);
static unsigned int RACallbackReadBlock(unsigned int address, unsigned char* buffer,
                                        unsigned int bytes);
static void RACallbackWriteMemory(unsigned int address, unsigned char value);

static bool s_raintegration_initialized = false;
}  // namespace Achievements::RAIntegration

/* void Achievements::SwitchToRAIntegration()
{
  s_using_raintegration = true;
  s_active = true;

  // Not strictly the case, but just in case we gate anything by IsLoggedIn().
  s_logged_in = true;
}*/

void Achievements::RAIntegration::InitializeRAIntegration(void* main_window_handle)
{
  RA_InitClient((HWND)main_window_handle, "Dolphin", SCM_DESC_STR);
  RA_SetUserAgentDetail(std::format("Dolphin {} {}", SCM_DESC_STR, SCM_BRANCH_STR).c_str());

  RA_InstallSharedFunctions(RACallbackIsActive, RACallbackCauseUnpause, RACallbackCausePause,
                            RACallbackRebuildMenu, RACallbackEstimateTitle, RACallbackResetEmulator,
                            RACallbackLoadROM);

  // EE physical memory and scratchpad are currently exposed (matching direct rcheevos
  // implementation).
  ReinstallMemoryBanks();

  // Fire off a login anyway. Saves going into the menu and doing it.
  RA_AttemptLogin(0);

  s_raintegration_initialized = true;

  // this is pretty lame, but we may as well persist until we exit anyway
  std::atexit(RA_Shutdown);
}

void Achievements::RAIntegration::ReinstallMemoryBanks()
{
  RA_ClearMemoryBanks();
  int memory_bank_size = 0;
  if (Core::GetState() != Core::State::Uninitialized)
  {
    memory_bank_size = memory_manager->GetExRamSizeReal();
  }
  RA_InstallMemoryBank(0, RACallbackReadMemory, RACallbackWriteMemory, memory_bank_size);
  RA_InstallMemoryBankBlockReader(0, RACallbackReadBlock);

}

void Achievements::RAIntegration::MainWindowChanged(void* new_handle)
{
  if (s_raintegration_initialized)
  {
    RA_UpdateHWnd((HWND)new_handle);
    return;
  }

  InitializeRAIntegration(new_handle);
}

void Achievements::RAIntegration::GameChanged(bool isWii)
{
  ReinstallMemoryBanks();
  if (game_data.response.succeeded)
  {
    RA_SetConsoleID(isWii ? WII : GameCube);
    RA_ActivateGame(game_data.id);
  }
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

std::vector<std::tuple<int, std::string, bool>> Achievements::RAIntegration::GetMenuItems()
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
      ret.emplace_back(static_cast<int>(it.nID), WideStringToUTF8String(it.sLabel), it.bChecked);
    }
  }

  return ret;
}

void Achievements::RAIntegration::ActivateMenuItem(int item)
{
  RA_InvokeDialog(item);
}

int Achievements::RAIntegration::RACallbackIsActive()
{
  return (game_data.response.succeeded)?(game_data.id):0;
}

void Achievements::RAIntegration::RACallbackCauseUnpause()
{
  if (Core::GetState() != Core::State::Uninitialized)
    Core::SetState(Core::State::Running);
}

void Achievements::RAIntegration::RACallbackCausePause()
{
  if (Core::GetState() != Core::State::Uninitialized)
    Core::SetState(Core::State::Paused);
}

void Achievements::RAIntegration::RACallbackRebuildMenu()
{
  // unused, we build the menu on demand
}

void Achievements::RAIntegration::RACallbackEstimateTitle(char* buf)
{
  strcpy(buf, game_data.title);
}

void Achievements::RAIntegration::RACallbackResetEmulator()
{
  auto& system = Core::System::GetInstance();
  system.GetProcessorInterface().ResetButton_Tap();
}

void Achievements::RAIntegration::RACallbackLoadROM(const char* unused)
{
  // unused
//  UNREFERENCED_PARAMETER(unused);
}

unsigned char Achievements::RAIntegration::RACallbackReadMemory(unsigned int address)
{
  return memory_manager->Read_U8(address);
}

unsigned int Achievements::RAIntegration::RACallbackReadBlock(unsigned int address,
                                                              unsigned char* buffer,
                                                              unsigned int bytes)
{
  memory_manager->CopyFromEmu(buffer, address, bytes);
  return bytes;
}

void Achievements::RAIntegration::RACallbackWriteMemory(unsigned int address, unsigned char value)
{
  memory_manager->Write_U8(value, address);
}

//#endif
