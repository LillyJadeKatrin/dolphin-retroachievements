// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef USE_RETRO_ACHIEVEMENTS

#include "Core/AchievementManager.h"

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

#ifdef _WIN32
#include <Windows.h>
#include <cstring>
#include "Common/scmrev.h"
#include "Core.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/ProcessorInterface.h"
#include "RAInterface/RA_Consoles.h"
#include "RAInterface/RA_Interface.h"
#include "System.h"
#endif  // _WIN32

AchievementManager* AchievementManager::GetInstance()
{
  static AchievementManager s_instance;
  return &s_instance;
}

void AchievementManager::Init()
{
  if (!m_is_runtime_initialized && Config::Get(Config::RA_ENABLED))
  {
    rc_runtime_init(&m_runtime);
    m_is_runtime_initialized = true;
    m_queue.Reset("AchievementManagerQueue", [](const std::function<void()>& func) { func(); });
    LoginAsync("", [](ResponseType r_type) {});
  }
}

AchievementManager::ResponseType AchievementManager::Login(const std::string& password)
{
  if (!m_is_runtime_initialized)
    return AchievementManager::ResponseType::MANAGER_NOT_INITIALIZED;
  return VerifyCredentials(password);
}

void AchievementManager::LoginAsync(const std::string& password, const ResponseCallback& callback)
{
  if (!m_is_runtime_initialized)
  {
    callback(AchievementManager::ResponseType::MANAGER_NOT_INITIALIZED);
    return;
  }
  m_queue.EmplaceItem([this, password, callback] { callback(VerifyCredentials(password)); });
}

bool AchievementManager::IsLoggedIn() const
{
  return !Config::Get(Config::RA_API_TOKEN).empty();
}

void AchievementManager::LoadGameByFilenameAsync(const std::string& iso_path,
                                                 const ResponseCallback& callback)
{
  if (!m_is_runtime_initialized)
  {
    callback(AchievementManager::ResponseType::MANAGER_NOT_INITIALIZED);
    return;
  }
  m_filename = iso_path.substr(iso_path.find_last_of('/') + 1);
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
  if (!rc_hash_generate_from_file(m_game_hash.data(), RC_CONSOLE_GAMECUBE, iso_path.c_str()))
    return;
  m_queue.EmplaceItem([this, callback] {
    const auto resolve_hash_response = ResolveHash(this->m_game_hash);
    if (resolve_hash_response != ResponseType::SUCCESS || m_game_id == 0)
    {
      callback(resolve_hash_response);
      return;
    }

    const auto start_session_response = StartRASession();
    if (start_session_response != ResponseType::SUCCESS)
    {
      callback(start_session_response);
      return;
    }

    const auto fetch_game_data_response = FetchGameData();
    m_is_game_loaded = fetch_game_data_response == ResponseType::SUCCESS;

    // Claim the lock, then queue the fetch unlock data calls, then initialize the unlock map in
    // ActivateDeactiveAchievements. This allows the calls to process while initializing the
    // unlock map but then forces them to wait until it's initialized before making modifications to
    // it.
    {
      std::lock_guard lg{m_lock};
      LoadUnlockData([](ResponseType r_type) {});
      ActivateDeactivateAchievements();
    }
    ActivateDeactivateLeaderboards();
    ActivateDeactivateRichPresence();
    m_do_frame_event = AfterFrameEvent::Register([this] { DoFrame(); }, "AchievementManager");

    callback(fetch_game_data_response);
  });
}

void AchievementManager::LoadUnlockData(const ResponseCallback& callback)
{
  m_queue.EmplaceItem([this, callback] {
    const auto hardcore_unlock_response = FetchUnlockData(true);
    if (hardcore_unlock_response != ResponseType::SUCCESS)
    {
      callback(hardcore_unlock_response);
      return;
    }

    callback(FetchUnlockData(false));
  });
}

void AchievementManager::ActivateDeactivateAchievements()
{
  bool enabled = Config::Get(Config::RA_ACHIEVEMENTS_ENABLED);
  bool unofficial = Config::Get(Config::RA_UNOFFICIAL_ENABLED);
  bool encore = Config::Get(Config::RA_ENCORE_ENABLED);
  for (u32 ix = 0; ix < m_game_data.num_achievements; ix++)
  {
    auto iter =
        m_unlock_map.insert({m_game_data.achievements[ix].id, UnlockStatus{.game_data_index = ix}});
    ActivateDeactivateAchievement(iter.first->first, enabled, unofficial, encore);
  }
}

void AchievementManager::ActivateDeactivateLeaderboards()
{
  bool leaderboards_enabled = Config::Get(Config::RA_LEADERBOARDS_ENABLED);
  for (u32 ix = 0; ix < m_game_data.num_leaderboards; ix++)
  {
    auto leaderboard = m_game_data.leaderboards[ix];
    if (m_is_game_loaded && leaderboards_enabled && hardcore_mode_enabled)
    {
      rc_runtime_activate_lboard(&m_runtime, leaderboard.id, leaderboard.definition, nullptr, 0);
    }
    else
    {
      rc_runtime_deactivate_lboard(&m_runtime, m_game_data.leaderboards[ix].id);
    }
  }
}

void AchievementManager::ActivateDeactivateRichPresence()
{
  rc_runtime_activate_richpresence(
      &m_runtime,
      (m_is_game_loaded && Config::Get(Config::RA_RICH_PRESENCE_ENABLED)) ?
          m_game_data.rich_presence_script :
          "",
      nullptr, 0);
}

void AchievementManager::DoFrame()
{
  if (!m_is_game_loaded)
    return;
  m_threadguard = new Core::CPUThreadGuard(Core::System::GetInstance());
  rc_runtime_do_frame(
      &m_runtime,
      [](const rc_runtime_event_t* runtime_event) {
        AchievementManager::GetInstance()->AchievementEventHandler(runtime_event);
      },
      [](unsigned address, unsigned num_bytes, void* ud) {
        return AchievementManager::GetInstance()->MemoryPeeker(address, num_bytes, ud);
      },
      nullptr, nullptr);
  delete m_threadguard;
  u64 current_time = Core::System::GetInstance().GetCoreTiming().GetTicks();
  if (current_time - m_last_ping_time > SystemTimers::GetTicksPerSecond() * 120)
    m_queue.EmplaceItem([this] { PingRichPresence(GenerateRichPresence()); });
}

u32 AchievementManager::MemoryPeeker(u32 address, u32 num_bytes, void* ud)
{
  const rc_memory_regions_t* regions = rc_console_memory_regions(m_game_data.console_id);
  for (u32 ix = 0; ix < regions->num_regions; ix++)
  {
    if (address >= regions->region[ix].start_address && address <= regions->region[ix].end_address)
    {
      address += (regions->region[ix].real_address - regions->region[ix].start_address);
      break;
    }
  }
  switch (num_bytes)
  {
  case 1:
    return Core::System::GetInstance()
        .GetMMU()
        .HostTryReadU8(*m_threadguard, address)
        .value_or(PowerPC::ReadResult<u8>(false, 0u))
        .value;
  case 2:
    return Core::System::GetInstance()
        .GetMMU()
        .HostTryReadU16(*m_threadguard, address)
        .value_or(PowerPC::ReadResult<u16>(false, 0u))
        .value;
  case 4:
    return Core::System::GetInstance()
        .GetMMU()
        .HostTryReadU32(*m_threadguard, address)
        .value_or(PowerPC::ReadResult<u32>(false, 0u))
        .value;
  case 8:
    return Core::System::GetInstance()
        .GetMMU()
        .HostTryReadU64(*m_threadguard, address)
        .value_or(PowerPC::ReadResult<u64>(false, 0u))
        .value;
  default:
    ASSERT(false);
    return 0u;
  }
}

void AchievementManager::AchievementEventHandler(const rc_runtime_event_t* runtime_event)
{
  switch (runtime_event->type)
  {
  case RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED:
    HandleAchievementTriggeredEvent(runtime_event);
    break;
  case RC_RUNTIME_EVENT_LBOARD_TRIGGERED:
    HandleLeaderboardTriggeredEvent(runtime_event);
    break;
  }
}

void AchievementManager::CloseGame()
{
  m_do_frame_event.reset();
  m_is_game_loaded = false;
  m_game_id = 0;
  m_queue.Cancel();
  m_unlock_map.clear();
  ActivateDeactivateAchievements();
  ActivateDeactivateLeaderboards();
  ActivateDeactivateRichPresence();
}

void AchievementManager::Logout()
{
  CloseGame();
  Config::SetBaseOrCurrent(Config::RA_API_TOKEN, "");
}

void AchievementManager::Shutdown()
{
  CloseGame();
  m_is_runtime_initialized = false;
  m_queue.Shutdown();
  // DON'T log out - keep those credentials for next run.
  rc_runtime_destroy(&m_runtime);
}

AchievementManager::ResponseType AchievementManager::VerifyCredentials(const std::string& password)
{
  rc_api_login_response_t login_data{};
  std::string username = Config::Get(Config::RA_USERNAME);
  std::string api_token = Config::Get(Config::RA_API_TOKEN);
  rc_api_login_request_t login_request = {
      .username = username.c_str(), .api_token = api_token.c_str(), .password = password.c_str()};
  ResponseType r_type = Request<rc_api_login_request_t, rc_api_login_response_t>(
      login_request, &login_data, rc_api_init_login_request, rc_api_process_login_response);
  if (r_type == ResponseType::SUCCESS)
    Config::SetBaseOrCurrent(Config::RA_API_TOKEN, login_data.api_token);
  rc_api_destroy_login_response(&login_data);
  return r_type;
}

AchievementManager::ResponseType
AchievementManager::ResolveHash(std::array<char, HASH_LENGTH> game_hash)
{
  rc_api_resolve_hash_response_t hash_data{};
  std::string username = Config::Get(Config::RA_USERNAME);
  std::string api_token = Config::Get(Config::RA_API_TOKEN);
  rc_api_resolve_hash_request_t resolve_hash_request = {
      .username = username.c_str(), .api_token = api_token.c_str(), .game_hash = game_hash.data()};
  ResponseType r_type = Request<rc_api_resolve_hash_request_t, rc_api_resolve_hash_response_t>(
      resolve_hash_request, &hash_data, rc_api_init_resolve_hash_request,
      rc_api_process_resolve_hash_response);
  if (r_type == ResponseType::SUCCESS)
    m_game_id = hash_data.game_id;
  rc_api_destroy_resolve_hash_response(&hash_data);
  return r_type;
}

AchievementManager::ResponseType AchievementManager::StartRASession()
{
  rc_api_start_session_response_t session_data{};
  std::string username = Config::Get(Config::RA_USERNAME);
  std::string api_token = Config::Get(Config::RA_API_TOKEN);
  rc_api_start_session_request_t start_session_request = {
      .username = username.c_str(), .api_token = api_token.c_str(), .game_id = m_game_id};
  ResponseType r_type = Request<rc_api_start_session_request_t, rc_api_start_session_response_t>(
      start_session_request, &session_data, rc_api_init_start_session_request,
      rc_api_process_start_session_response);
  rc_api_destroy_start_session_response(&session_data);
  return r_type;
}

AchievementManager::ResponseType AchievementManager::FetchGameData()
{
  std::string username = Config::Get(Config::RA_USERNAME);
  std::string api_token = Config::Get(Config::RA_API_TOKEN);
  rc_api_fetch_game_data_request_t fetch_data_request = {
      .username = username.c_str(), .api_token = api_token.c_str(), .game_id = m_game_id};
  return Request<rc_api_fetch_game_data_request_t, rc_api_fetch_game_data_response_t>(
      fetch_data_request, &m_game_data, rc_api_init_fetch_game_data_request,
      rc_api_process_fetch_game_data_response);
}

AchievementManager::ResponseType AchievementManager::FetchUnlockData(bool hardcore)
{
  rc_api_fetch_user_unlocks_response_t unlock_data{};
  std::string username = Config::Get(Config::RA_USERNAME);
  std::string api_token = Config::Get(Config::RA_API_TOKEN);
  rc_api_fetch_user_unlocks_request_t fetch_unlocks_request = {.username = username.c_str(),
                                                               .api_token = api_token.c_str(),
                                                               .game_id = m_game_id,
                                                               .hardcore = hardcore};
  ResponseType r_type =
      Request<rc_api_fetch_user_unlocks_request_t, rc_api_fetch_user_unlocks_response_t>(
          fetch_unlocks_request, &unlock_data, rc_api_init_fetch_user_unlocks_request,
          rc_api_process_fetch_user_unlocks_response);
  if (r_type == ResponseType::SUCCESS)
  {
    std::lock_guard lg{m_lock};
    bool enabled = Config::Get(Config::RA_ACHIEVEMENTS_ENABLED);
    bool unofficial = Config::Get(Config::RA_UNOFFICIAL_ENABLED);
    bool encore = Config::Get(Config::RA_ENCORE_ENABLED);
    for (AchievementId ix = 0; ix < unlock_data.num_achievement_ids; ix++)
    {
      auto it = m_unlock_map.find(unlock_data.achievement_ids[ix]);
      if (it == m_unlock_map.end())
        continue;
      it->second.remote_unlock_status =
          hardcore ? UnlockStatus::UnlockType::HARDCORE : UnlockStatus::UnlockType::SOFTCORE;
      ActivateDeactivateAchievement(unlock_data.achievement_ids[ix], enabled, unofficial, encore);
    }
  }
  rc_api_destroy_fetch_user_unlocks_response(&unlock_data);
  return r_type;
}

void AchievementManager::ActivateDeactivateAchievement(AchievementId id, bool enabled,
                                                       bool unofficial, bool encore)
{
  auto it = m_unlock_map.find(id);
  if (it == m_unlock_map.end())
    return;
  const UnlockStatus& status = it->second;
  u32 index = status.game_data_index;
  bool active = (rc_runtime_get_achievement(&m_runtime, id) != nullptr);

  // Deactivate achievements if game is not loaded
  bool activate = m_is_game_loaded;
  // Activate achievements only if achievements are enabled
  if (activate && !enabled)
    activate = false;
  // Deactivate if achievement is unofficial, unless unofficial achievements are enabled
  if (activate && !unofficial &&
      m_game_data.achievements[index].category == RC_ACHIEVEMENT_CATEGORY_UNOFFICIAL)
  {
    activate = false;
  }
  // If encore mode is on, activate/deactivate regardless of current unlock status
  if (activate && !encore)
  {
    // Encore is off, achievement has been unlocked in this session, deactivate
    activate = (status.session_unlock_count == 0);
    // Encore is off, achievement has been hardcore unlocked on site, deactivate
    if (activate && status.remote_unlock_status == UnlockStatus::UnlockType::HARDCORE)
      activate = false;
    // Encore is off, hardcore is off, achievement has been softcore unlocked on site, deactivate
    if (activate && !hardcore_mode_enabled &&
        status.remote_unlock_status == UnlockStatus::UnlockType::SOFTCORE)
    {
      activate = false;
    }
  }

  if (!active && activate)
  {
    rc_runtime_activate_achievement(&m_runtime, id, m_game_data.achievements[index].definition,
                                    nullptr, 0);
  }
  if (active && !activate)
    rc_runtime_deactivate_achievement(&m_runtime, id);
}

RichPresence AchievementManager::GenerateRichPresence()
{
  RichPresence rp_buffer;
  m_threadguard = new Core::CPUThreadGuard(Core::System::GetInstance());
  rc_runtime_get_richpresence(
      &m_runtime, rp_buffer.data(), RP_SIZE,
      [](unsigned address, unsigned num_bytes, void* ud) {
        return AchievementManager::GetInstance()->MemoryPeeker(address, num_bytes, ud);
      },
      nullptr, nullptr);
  delete m_threadguard;
  return rp_buffer;
}

AchievementManager::ResponseType AchievementManager::AwardAchievement(AchievementId achievement_id)
{
  std::string username = Config::Get(Config::RA_USERNAME);
  std::string api_token = Config::Get(Config::RA_API_TOKEN);
  rc_api_award_achievement_request_t award_request = {.username = username.c_str(),
                                                      .api_token = api_token.c_str(),
                                                      .achievement_id = achievement_id,
                                                      .hardcore = hardcore_mode_enabled,
                                                      .game_hash = m_game_hash.data()};
  rc_api_award_achievement_response_t award_response = {};
  ResponseType r_type =
      Request<rc_api_award_achievement_request_t, rc_api_award_achievement_response_t>(
          award_request, &award_response, rc_api_init_award_achievement_request,
          rc_api_process_award_achievement_response);
  rc_api_destroy_award_achievement_response(&award_response);
  return r_type;
}

AchievementManager::ResponseType AchievementManager::SubmitLeaderboard(AchievementId leaderboard_id,
                                                                       int value)
{
  std::string username = Config::Get(Config::RA_USERNAME);
  std::string api_token = Config::Get(Config::RA_API_TOKEN);
  rc_api_submit_lboard_entry_request_t submit_request = {.username = username.c_str(),
                                                         .api_token = api_token.c_str(),
                                                         .leaderboard_id = leaderboard_id,
                                                         .score = value,
                                                         .game_hash = m_game_hash.data()};
  rc_api_submit_lboard_entry_response_t submit_response = {};
  ResponseType r_type =
      Request<rc_api_submit_lboard_entry_request_t, rc_api_submit_lboard_entry_response_t>(
          submit_request, &submit_response, rc_api_init_submit_lboard_entry_request,
          rc_api_process_submit_lboard_entry_response);
  rc_api_destroy_submit_lboard_entry_response(&submit_response);
  return r_type;
}

AchievementManager::ResponseType AchievementManager::PingRichPresence(RichPresence rich_presence)
{
  std::string username = Config::Get(Config::RA_USERNAME);
  std::string api_token = Config::Get(Config::RA_API_TOKEN);
  rc_api_ping_request_t ping_request = {.username = username.c_str(),
                                        .api_token = api_token.c_str(),
                                        .game_id = m_game_id,
                                        .rich_presence = rich_presence.data()};
  rc_api_ping_response_t ping_response = {};
  ResponseType r_type = Request<rc_api_ping_request_t, rc_api_ping_response_t>(
      ping_request, &ping_response, rc_api_init_ping_request, rc_api_process_ping_response);
  rc_api_destroy_ping_response(&ping_response);
  return r_type;
}

void AchievementManager::HandleAchievementTriggeredEvent(const rc_runtime_event_t* runtime_event)
{
  auto it = m_unlock_map.find(runtime_event->id);
  if (it == m_unlock_map.end())
    return;
  it->second.session_unlock_count++;
  m_queue.EmplaceItem([this, runtime_event] { AwardAchievement(runtime_event->id); });
  ActivateDeactivateAchievement(runtime_event->id, Config::Get(Config::RA_ACHIEVEMENTS_ENABLED),
                                Config::Get(Config::RA_UNOFFICIAL_ENABLED),
                                Config::Get(Config::RA_ENCORE_ENABLED));
}

void AchievementManager::HandleLeaderboardTriggeredEvent(const rc_runtime_event_t* runtime_event)
{
  m_queue.EmplaceItem(
      [this, runtime_event] { SubmitLeaderboard(runtime_event->id, runtime_event->value); });
}

// Every RetroAchievements API call, with only a partial exception for fetch_image, follows
// the same design pattern (here, X is the name of the call):
//   Create a specific rc_api_X_request_t struct and populate with the necessary values
//   Call rc_api_init_X_request to convert this into a generic rc_api_request_t struct
//   Perform the HTTP request using the url and post_data in the rc_api_request_t struct
//   Call rc_api_process_X_response to convert the raw string HTTP response into a
//     rc_api_X_response_t struct
//   Use the data in the rc_api_X_response_t struct as needed
//   Call rc_api_destroy_X_response when finished with the response struct to free memory
template <typename RcRequest, typename RcResponse>
AchievementManager::ResponseType AchievementManager::Request(
    RcRequest rc_request, RcResponse* rc_response,
    const std::function<int(rc_api_request_t*, const RcRequest*)>& init_request,
    const std::function<int(RcResponse*, const char*)>& process_response)
{
  return ResponseType::INVALID_CREDENTIALS;
  /* rc_api_request_t api_request;
  Common::HttpRequest http_request;
  init_request(&api_request, &rc_request);
  if (!api_request.post_data)
    return ResponseType::INVALID_CREDENTIALS;
  auto http_response = http_request.Post(api_request.url, api_request.post_data);
  rc_api_destroy_request(&api_request);
  if (http_response.has_value() && http_response->size() > 0)
  {
    const std::string response_str(http_response->begin(), http_response->end());
    process_response(rc_response, response_str.c_str());
    if (rc_response->response.succeeded)
    {
      return ResponseType::SUCCESS;
    }
    else
    {
      Logout();
      return ResponseType::INVALID_CREDENTIALS;
    }
  }
  else
  {
    return ResponseType::CONNECTION_FAILED;
  }*/
}

void AchievementManager::EnableDLL(bool enable)
{
  m_dll_enabled = enable;
}

bool AchievementManager::IsDLLEnabled()
{
  return m_dll_enabled;
}

#ifdef _WIN32
void AchievementManager::InitializeRAIntegration(void* main_window_handle)
{
  if (!m_dll_enabled)
    return;
  RA_InitClient((HWND)main_window_handle, "Dolphin", SCM_DESC_STR);
  RA_SetUserAgentDetail(std::format("Dolphin {} {}", SCM_DESC_STR, SCM_BRANCH_STR).c_str());

  RA_InstallSharedFunctions(
      []() { return AchievementManager::GetInstance()->RACallbackIsActive(); },
      []() { AchievementManager::GetInstance()->RACallbackCauseUnpause(); },
      []() { AchievementManager::GetInstance()->RACallbackCausePause(); },
      []() { AchievementManager::GetInstance()->RACallbackRebuildMenu(); },
      [](char* buf) { AchievementManager::GetInstance()->RACallbackEstimateTitle(buf); },
      []() { AchievementManager::GetInstance()->RACallbackResetEmulator(); },
      [](const char* unused) { AchievementManager::GetInstance()->RACallbackLoadROM(unused); });

  // EE physical memory and scratchpad are currently exposed (matching direct rcheevos
  // implementation).
  ReinstallMemoryBanks();

  m_raintegration_initialized = true;

  RA_AttemptLogin(0);

  // this is pretty lame, but we may as well persist until we exit anyway
  std::atexit(RA_Shutdown);
}

void AchievementManager::ReinstallMemoryBanks()
{
  if (!m_dll_enabled)
    return;
  RA_ClearMemoryBanks();
  int memory_bank_size = 0;
  if (Core::GetState() != Core::State::Uninitialized)
  {
    memory_bank_size = Core::System::GetInstance().GetMemory().GetRamSizeReal();
  }
  RA_InstallMemoryBank(
      0,
      [](unsigned int address) {
        return AchievementManager::GetInstance()->RACallbackReadMemory(address);
      },
      [](unsigned int address, unsigned char value) {
        AchievementManager::GetInstance()->RACallbackWriteMemory(address, value);
      },
      memory_bank_size);
  RA_InstallMemoryBankBlockReader(
      0, [](unsigned int address, unsigned char* buffer, unsigned int bytes) {
        return AchievementManager::GetInstance()->RACallbackReadBlock(address, buffer, bytes);
      });
}

void AchievementManager::MainWindowChanged(void* new_handle)
{
  if (!m_dll_enabled)
    return;
  if (m_raintegration_initialized)
  {
    RA_UpdateHWnd((HWND)new_handle);
    return;
  }

  InitializeRAIntegration(new_handle);
}

void AchievementManager::GameChanged(bool isWii)
{
  if (!m_dll_enabled)
    return;

  m_do_frame_event.reset();
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
    //      AfterFrameEvent::Register([this] { RA_DoAchievementsFrame(); }, "AchievementManager");
  }
}

void AchievementManager::RAIDoFrame()
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

std::vector<std::tuple<int, std::string, bool>> AchievementManager::GetMenuItems()
{
  if (!m_dll_enabled)
    return std::vector<std::tuple<int, std::string, bool>>();
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

void AchievementManager::ActivateMenuItem(int item)
{
  if (!m_dll_enabled)
    return;
  RA_InvokeDialog(item);
}

int AchievementManager::RACallbackIsActive()
{
  return m_game_id;
}

void AchievementManager::RACallbackCauseUnpause()
{
  if (Core::GetState() != Core::State::Uninitialized)
    Core::SetState(Core::State::Running);
}

void AchievementManager::RACallbackCausePause()
{
  if (Core::GetState() != Core::State::Uninitialized)
    Core::SetState(Core::State::Paused);
}

void AchievementManager::RACallbackRebuildMenu()
{
  // unused
}

void AchievementManager::RACallbackEstimateTitle(char* buf)
{
  strcpy(buf, m_filename.c_str());
}

void AchievementManager::RACallbackResetEmulator()
{
  Core::Stop();
}

void AchievementManager::RACallbackLoadROM(const char* unused)
{
  // unused
}

unsigned char AchievementManager::RACallbackReadMemory(unsigned int address)
{
  return Core::System::GetInstance()
      .GetMMU()
      .HostTryReadU8(*m_threadguard, address)
      .value_or(PowerPC::ReadResult<u8>(false, 0u))
      .value;
}

unsigned int AchievementManager::RACallbackReadBlock(unsigned int address, unsigned char* buffer,
                                                     unsigned int bytes)
{
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

void AchievementManager::RACallbackWriteMemory(unsigned int address, unsigned char value)
{
  Core::System::GetInstance().GetMMU().HostTryWriteU8(*m_threadguard, value, address);
}
#endif  // _WIN32
#endif  // USE_RETRO_ACHIEVEMENTS
