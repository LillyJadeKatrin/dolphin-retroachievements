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

#include "Core/HW/Memmap.h"
#include "VideoCommon/OnScreenDisplay.h"
#include <iostream>
#include <Common/HttpRequest.h>
#include <VideoCommon/TextureConfig.h>
#include <VideoBackends/Software/SWTexture.h>

namespace Achievements
{
static rc_runtime_t runtime{};
static bool is_runtime_initialized = false;
static rc_api_login_response_t login_data{};
static rc_api_start_session_response_t session_data{};
static rc_api_fetch_game_data_response_t game_data{};

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

static Memory::MemoryManager* memory_manager = nullptr;

static std::map<unsigned int, std::vector<u8>> unlocked_icons;
static std::map<unsigned int, std::vector<u8>> locked_icons;
std::vector<u8> game_icon;
std::vector<u8> user_icon;

// TODO lillyjade: Temporary hardcoded test data - CLEAN BEFORE PUSHING
static const char* username = "";
static const char* password = "";
static unsigned int game_id = 3417;
static const char* game_hash = "ada3c364c783021884b066a4ad7ee49c";
static unsigned int partial_list_limit = 1;

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
  rc_api_request_t api_request;
  Common::HttpRequest http_request;
  rc_api_init_fetch_image_request(&api_request, &rc_request);
  auto http_response = http_request.Get(api_request.url);
  if (http_response.has_value() && http_response.value().size() > 0)
    icon_buff = http_response.value();
  else
    icon_buff.clear();
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
                      (sett_badge_icons_enabled)
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
    Award(runtime_event->id);
    DisplayUnlocked(runtime_event->id);
    break;
  }
}
} // empty (private) namespace

void Init()
{
  if (!is_runtime_initialized && sett_integration_enabled)
  {
    rc_runtime_init(&runtime);
  }
}

void Login()
{
  if (!sett_integration_enabled || !is_runtime_initialized)
    return;
  rc_api_login_request_t login_request = {.username = username, .password = password};
  Request<rc_api_login_request_t, rc_api_login_response_t>(
      login_request, &login_data, rc_api_init_login_request, rc_api_process_login_response);
  rc_api_fetch_image_request_t icon_request = {.image_name = login_data.username,
                                               .image_type = RC_IMAGE_TYPE_USER};
  if (sett_badge_icons_enabled)
    IconRequest(icon_request, user_icon);
}

void StartSession(Memory::MemoryManager* memmgr)
{
  if (!sett_integration_enabled || !is_runtime_initialized || !login_data.response.succeeded)
    return;
  rc_api_start_session_request_t start_session_request = {
      .username = username, .api_token = login_data.api_token, .game_id = game_id};
  Request<rc_api_start_session_request_t, rc_api_start_session_response_t>(
      start_session_request, &session_data,
      rc_api_init_start_session_request, rc_api_process_start_session_response);
  memory_manager = memmgr;
}

void FetchData()
{
  if (!sett_integration_enabled || !is_runtime_initialized || !login_data.response.succeeded || !session_data.response.succeeded)
    return;
  rc_api_fetch_game_data_request_t fetch_data_request = {
      .username = username, .api_token = login_data.api_token, .game_id = game_id};
  Request<rc_api_fetch_game_data_request_t, rc_api_fetch_game_data_response_t>(
      fetch_data_request, &game_data, rc_api_init_fetch_game_data_request,
      rc_api_process_fetch_game_data_response);
  rc_api_fetch_image_request_t icon_request = {.image_name = game_data.image_name,
                                               .image_type = RC_IMAGE_TYPE_GAME};
  if (sett_badge_icons_enabled)
  {
    IconRequest(icon_request, user_icon);
    for (unsigned int ix = 0; ix < partial_list_limit; ix++)
    //      for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
    {
      icon_request.image_name = game_data.achievements[ix].badge_name;
      icon_request.image_type = RC_IMAGE_TYPE_ACHIEVEMENT;
      IconRequest(icon_request, unlocked_icons[game_data.achievements[ix].id]);
      icon_request.image_type = RC_IMAGE_TYPE_ACHIEVEMENT_LOCKED;
      IconRequest(icon_request, unlocked_icons[game_data.achievements[ix].id]);
    }
  }
}

void ActivateAM()
{
  if (!sett_integration_enabled || !is_runtime_initialized || !login_data.response.succeeded ||
      !session_data.response.succeeded || !game_data.response.succeeded || !sett_achievements_enabled)
    return;
  // TODO lillyjade: only loading the first cheevo for testing purposes
  // for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
  for (unsigned int ix = 0; ix < partial_list_limit; ix++)
  {
    rc_runtime_activate_achievement(&runtime, game_data.achievements[ix].id,
                                    game_data.achievements[ix].definition, nullptr, 0);
  }
}

void DoFrame()
{
  if (!sett_integration_enabled || !is_runtime_initialized || !login_data.response.succeeded ||
      !session_data.response.succeeded || !game_data.response.succeeded)
    return;
  rc_runtime_do_frame(&runtime, &AchievementEventHandler, &MemoryPeeker, nullptr, nullptr);
}

void Award(unsigned int achievement_id)
{
  if (!sett_integration_enabled || !is_runtime_initialized || !login_data.response.succeeded ||
      !session_data.response.succeeded || !game_data.response.succeeded || !sett_achievements_enabled)
    return;
  rc_api_award_achievement_request_t award_request = {
      .username = username,
      .api_token = login_data.api_token,
      .achievement_id = achievement_id,
      .hardcore = 0,
      .game_hash = game_hash};
  rc_api_award_achievement_response_t award_response = {};
  Request<rc_api_award_achievement_request_t, rc_api_award_achievement_response_t>(
      award_request, &award_response, rc_api_init_award_achievement_request,
      rc_api_process_award_achievement_response);
}

void DeactivateAM()
{
  for (unsigned int ix = 0; ix < game_data.num_achievements; ix++)
  {
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
