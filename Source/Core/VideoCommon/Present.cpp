// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/Present.h"

#include "Common/ChunkFile.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/HW/VideoInterface.h"
#include "Core/Host.h"
#include "Core/System.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include "Present.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/FrameDumper.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/OnScreenUI.h"
#include "VideoCommon/PostProcessing.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoEvents.h"
#include "VideoCommon/Widescreen.h"

std::unique_ptr<VideoCommon::Presenter> g_presenter;

// The video encoder needs the image to be a multiple of x samples.
static constexpr int VIDEO_ENCODER_LCM = 4;

namespace VideoCommon
{
// Stretches the native/internal analog resolution aspect ratio from ~4:3 to ~16:9
static float SourceAspectRatioToWidescreen(float source_aspect)
{
  return source_aspect * ((16.0f / 9.0f) / (4.0f / 3.0f));
}

static std::tuple<int, int> FindClosestIntegerResolution(float width, float height,
                                                         float aspect_ratio)
{
  // We can't round both the x and y resolution as that might generate an aspect ratio
  // further away from the target one, we also can't either ceil or floor both sides,
  // so we find the combination or flooring and ceiling that is closest to the target ar.
  const int ceiled_width = static_cast<int>(std::ceil(width));
  const int ceiled_height = static_cast<int>(std::ceil(height));
  const int floored_width = static_cast<int>(std::floor(width));
  const int floored_height = static_cast<int>(std::floor(height));

  int int_width = floored_width;
  int int_height = floored_height;

  float min_aspect_ratio_distance = std::numeric_limits<float>::max();
  for (const int new_width : std::array<int, 2>{ceiled_width, floored_width})
  {
    for (const int new_height : std::array<int, 2>{ceiled_height, floored_height})
    {
      const float new_aspect_ratio = static_cast<float>(new_width) / new_height;
      const float aspect_ratio_distance = std::abs((new_aspect_ratio / aspect_ratio) - 1.f);
      if (aspect_ratio_distance < min_aspect_ratio_distance)
      {
        min_aspect_ratio_distance = aspect_ratio_distance;
        int_width = new_width;
        int_height = new_height;
      }
    }
  }

  return std::make_tuple(int_width, int_height);
}

Presenter::Presenter()
{
  m_config_changed =
      ConfigChangedEvent::Register([this](u32 bits) { ConfigChanged(bits); }, "Presenter");
}

Presenter::~Presenter()
{
  // Disable ControllerInterface's aspect ratio adjustments so mapping dialog behaves normally.
  g_controller_interface.SetAspectRatioAdjustment(1);
}

bool Presenter::Initialize()
{
  UpdateDrawRectangle();

  if (!g_gfx->IsHeadless())
  {
    SetBackbuffer(g_gfx->GetSurfaceInfo());

    m_post_processor = std::make_unique<VideoCommon::PostProcessing>();
    if (!m_post_processor->Initialize(m_backbuffer_format))
      return false;

    m_onscreen_ui = std::make_unique<OnScreenUI>();
    if (!m_onscreen_ui->Initialize(m_backbuffer_width, m_backbuffer_height, m_backbuffer_scale))
      return false;

    // Draw a blank frame (and complete OnScreenUI initialization)
    g_gfx->BindBackbuffer({{0.0f, 0.0f, 0.0f, 1.0f}});
    g_gfx->PresentBackbuffer();
  }

  return true;
}

bool Presenter::FetchXFB(u32 xfb_addr, u32 fb_width, u32 fb_stride, u32 fb_height, u64 ticks)
{
  ReleaseXFBContentLock();
  u64 old_xfb_id = m_last_xfb_id;

  if (fb_width == 0 || fb_height == 0)
  {
    // Game is blanking the screen
    m_xfb_entry.reset();
    m_last_xfb_id = std::numeric_limits<u64>::max();
  }
  else
  {
    m_xfb_entry =
        g_texture_cache->GetXFBTexture(xfb_addr, fb_width, fb_height, fb_stride, &m_xfb_rect);
    m_last_xfb_id = m_xfb_entry->id;

    m_xfb_entry->AcquireContentLock();
  }
  m_last_xfb_addr = xfb_addr;
  m_last_xfb_ticks = ticks;
  m_last_xfb_width = fb_width;
  m_last_xfb_stride = fb_stride;
  m_last_xfb_height = fb_height;

  return old_xfb_id == m_last_xfb_id;
}

void Presenter::ViSwap(u32 xfb_addr, u32 fb_width, u32 fb_stride, u32 fb_height, u64 ticks)
{
  bool is_duplicate = FetchXFB(xfb_addr, fb_width, fb_stride, fb_height, ticks);

  PresentInfo present_info;
  present_info.emulated_timestamp = ticks;
  present_info.present_count = m_present_count++;
  if (is_duplicate)
  {
    present_info.frame_count = m_frame_count - 1;  // Previous frame
    present_info.reason = PresentInfo::PresentReason::VideoInterfaceDuplicate;
  }
  else
  {
    present_info.frame_count = m_frame_count++;
    present_info.reason = PresentInfo::PresentReason::VideoInterface;
  }

  BeforePresentEvent::Trigger(present_info);

  if (!is_duplicate || !g_ActiveConfig.bSkipPresentingDuplicateXFBs)
  {
    Present();
    ProcessFrameDumping(ticks);

    AfterPresentEvent::Trigger(present_info);
  }
}

void Presenter::ImmediateSwap(u32 xfb_addr, u32 fb_width, u32 fb_stride, u32 fb_height, u64 ticks)
{
  FetchXFB(xfb_addr, fb_width, fb_stride, fb_height, ticks);

  PresentInfo present_info;
  present_info.emulated_timestamp = ticks;  // TODO: This should be the time of the next VI field
  present_info.frame_count = m_frame_count++;
  present_info.reason = PresentInfo::PresentReason::Immediate;
  present_info.present_count = m_present_count++;

  BeforePresentEvent::Trigger(present_info);

  Present();
  ProcessFrameDumping(ticks);

  AfterPresentEvent::Trigger(present_info);
}

void Presenter::ProcessFrameDumping(u64 ticks) const
{
  if (g_frame_dumper->IsFrameDumping() && m_xfb_entry)
  {
    MathUtil::Rectangle<int> target_rect;
    if (!g_ActiveConfig.bInternalResolutionFrameDumps && !g_gfx->IsHeadless())
    {
      target_rect = GetTargetRectangle();
    }
    else
    {
      int width, height;
      std::tie(width, height) =
          CalculateOutputDimensions(m_xfb_rect.GetWidth(), m_xfb_rect.GetHeight());
      target_rect = MathUtil::Rectangle<int>(0, 0, width, height);
    }

    g_frame_dumper->DumpCurrentFrame(m_xfb_entry->texture.get(), m_xfb_rect, target_rect, ticks,
                                     m_frame_count);
  }
}

void Presenter::SetBackbuffer(int backbuffer_width, int backbuffer_height)
{
  const bool is_first = m_backbuffer_width == 0 && m_backbuffer_height == 0;
  const bool size_changed =
      (m_backbuffer_width != backbuffer_width || m_backbuffer_height != backbuffer_height);
  m_backbuffer_width = backbuffer_width;
  m_backbuffer_height = backbuffer_height;
  UpdateDrawRectangle();

  OnBackbufferSet(size_changed, is_first);
}

void Presenter::SetBackbuffer(SurfaceInfo info)
{
  const bool is_first = m_backbuffer_width == 0 && m_backbuffer_height == 0;
  const bool size_changed =
      (m_backbuffer_width != (int)info.width || m_backbuffer_height != (int)info.height);
  m_backbuffer_width = info.width;
  m_backbuffer_height = info.height;
  m_backbuffer_scale = info.scale;
  m_backbuffer_format = info.format;
  if (m_onscreen_ui)
    m_onscreen_ui->SetScale(info.scale);

  OnBackbufferSet(size_changed, is_first);
}

void Presenter::OnBackbufferSet(bool size_changed, bool is_first_set)
{
  UpdateDrawRectangle();

  // Automatically update the resolution scale if the window size changed,
  // or if the game XFB resolution changed.
  if (size_changed && !is_first_set && g_ActiveConfig.iEFBScale == EFB_SCALE_AUTO_INTEGRAL &&
      m_auto_resolution_scale != AutoIntegralScale())
  {
    g_framebuffer_manager->RecreateEFBFramebuffer();
  }
  if (size_changed || is_first_set)
  {
    m_auto_resolution_scale = AutoIntegralScale();
  }
}

void Presenter::ConfigChanged(u32 changed_bits)
{
  // Check for post-processing shader changes. Done up here as it doesn't affect anything outside
  // the post-processor. Note that options are applied every frame, so no need to check those.
  if (changed_bits & ConfigChangeBits::CONFIG_CHANGE_BIT_POST_PROCESSING_SHADER && m_post_processor)
  {
    // The existing shader must not be in use when it's destroyed
    g_gfx->WaitForGPUIdle();

    m_post_processor->RecompileShader();
  }

  // Stereo mode change requires recompiling our post processing pipeline and imgui pipelines for
  // rendering the UI.
  if (changed_bits & ConfigChangeBits::CONFIG_CHANGE_BIT_STEREO_MODE)
  {
    if (m_onscreen_ui)
      m_onscreen_ui->RecompileImGuiPipeline();
    if (m_post_processor)
      m_post_processor->RecompilePipeline();
  }
}

std::tuple<MathUtil::Rectangle<int>, MathUtil::Rectangle<int>>
Presenter::ConvertStereoRectangle(const MathUtil::Rectangle<int>& rc) const
{
  // Resize target to half its original size
  auto draw_rc = rc;
  if (g_ActiveConfig.stereo_mode == StereoMode::TAB)
  {
    // The height may be negative due to flipped rectangles
    int height = rc.bottom - rc.top;
    draw_rc.top += height / 4;
    draw_rc.bottom -= height / 4;
  }
  else
  {
    int width = rc.right - rc.left;
    draw_rc.left += width / 4;
    draw_rc.right -= width / 4;
  }

  // Create two target rectangle offset to the sides of the backbuffer
  auto left_rc = draw_rc;
  auto right_rc = draw_rc;
  if (g_ActiveConfig.stereo_mode == StereoMode::TAB)
  {
    left_rc.top -= m_backbuffer_height / 4;
    left_rc.bottom -= m_backbuffer_height / 4;
    right_rc.top += m_backbuffer_height / 4;
    right_rc.bottom += m_backbuffer_height / 4;
  }
  else
  {
    left_rc.left -= m_backbuffer_width / 4;
    left_rc.right -= m_backbuffer_width / 4;
    right_rc.left += m_backbuffer_width / 4;
    right_rc.right += m_backbuffer_width / 4;
  }

  return std::make_tuple(left_rc, right_rc);
}

float Presenter::CalculateDrawAspectRatio(bool allow_stretch) const
{
  auto aspect_mode = g_ActiveConfig.aspect_mode;

  if (!allow_stretch && aspect_mode == AspectMode::Stretch)
    aspect_mode = AspectMode::Auto;

  // If stretch is enabled, we prefer the aspect ratio of the window.
  if (aspect_mode == AspectMode::Stretch)
    return (static_cast<float>(m_backbuffer_width) / static_cast<float>(m_backbuffer_height));

  auto& vi = Core::System::GetInstance().GetVideoInterface();
  const float source_aspect_ratio = vi.GetAspectRatio();

  // This will scale up the source ~4:3 resolution to its equivalent ~16:9 resolution
  if (aspect_mode == AspectMode::ForceWide ||
      (aspect_mode == AspectMode::Auto && g_widescreen->IsGameWidescreen()))
  {
    return SourceAspectRatioToWidescreen(source_aspect_ratio);
  }
  // For the "custom" mode, we force the exact target aspect ratio, without
  // acknowleding the difference between the source aspect ratio and 4:3.
  else if (aspect_mode == AspectMode::Custom)
  {
    return g_ActiveConfig.GetCustomAspectRatio();
  }

  return source_aspect_ratio;
}

void Presenter::AdjustRectanglesToFitBounds(MathUtil::Rectangle<int>* target_rect,
                                            MathUtil::Rectangle<int>* source_rect, int fb_width,
                                            int fb_height)
{
  const int orig_target_width = target_rect->GetWidth();
  const int orig_target_height = target_rect->GetHeight();
  const int orig_source_width = source_rect->GetWidth();
  const int orig_source_height = source_rect->GetHeight();
  if (target_rect->left < 0)
  {
    const int offset = -target_rect->left;
    target_rect->left = 0;
    source_rect->left += offset * orig_source_width / orig_target_width;
  }
  if (target_rect->right > fb_width)
  {
    const int offset = target_rect->right - fb_width;
    target_rect->right -= offset;
    source_rect->right -= offset * orig_source_width / orig_target_width;
  }
  if (target_rect->top < 0)
  {
    const int offset = -target_rect->top;
    target_rect->top = 0;
    source_rect->top += offset * orig_source_height / orig_target_height;
  }
  if (target_rect->bottom > fb_height)
  {
    const int offset = target_rect->bottom - fb_height;
    target_rect->bottom -= offset;
    source_rect->bottom -= offset * orig_source_height / orig_target_height;
  }
}

void Presenter::ReleaseXFBContentLock()
{
  if (m_xfb_entry)
    m_xfb_entry->ReleaseContentLock();
}

void Presenter::ChangeSurface(void* new_surface_handle)
{
  std::lock_guard<std::mutex> lock(m_swap_mutex);
  m_new_surface_handle = new_surface_handle;
  m_surface_changed.Set();
}

void Presenter::ResizeSurface()
{
  std::lock_guard<std::mutex> lock(m_swap_mutex);
  m_surface_resized.Set();
}

void* Presenter::GetNewSurfaceHandle()
{
  void* handle = m_new_surface_handle;
  m_new_surface_handle = nullptr;
  return handle;
}

u32 Presenter::AutoIntegralScale() const
{
  // Take the source resolution (XFB) and stretch it on the target aspect ratio.
  // If the target resolution is larger (on either x or y), we scale the source
  // by a integer multiplier until it won't have to be scaled up anymore.
  u32 source_width = m_last_xfb_width;
  u32 source_height = m_last_xfb_height;
  const u32 target_width = m_target_rectangle.GetWidth();
  const u32 target_height = m_target_rectangle.GetHeight();
  const float source_aspect_ratio = (float)source_width / source_height;
  const float target_aspect_ratio = (float)target_width / target_height;
  if (source_aspect_ratio >= target_aspect_ratio)
    source_width = std::round(source_height * target_aspect_ratio);
  else
    source_height = std::round(source_width / target_aspect_ratio);
  const u32 width_scale =
      source_width > 0 ? ((target_width + (source_width - 1)) / source_width) : 1;
  const u32 height_scale =
      source_height > 0 ? ((target_height + (source_height - 1)) / source_height) : 1;
  // Limit to the max to avoid creating textures larger than their max supported resolution.
  return std::min(std::max(width_scale, height_scale),
                  static_cast<u32>(Config::Get(Config::GFX_MAX_EFB_SCALE)));
}

void Presenter::SetSuggestedWindowSize(int width, int height)
{
  // While trying to guess the best window resolution, we can't allow it to use the
  // "AspectMode::Stretch" setting because that would self influence the output result,
  // given it would be based on the previous frame resolution
  const bool allow_stretch = false;
  const auto [out_width, out_height] = CalculateOutputDimensions(width, height, allow_stretch);

  // Track the last values of width/height to avoid sending a window resize event every frame.
  if (out_width == m_last_window_request_width && out_height == m_last_window_request_height)
    return;

  m_last_window_request_width = out_width;
  m_last_window_request_height = out_height;
  // Pass in the suggested window size. This might not always be acknowledged.
  Host_RequestRenderWindowSize(out_width, out_height);
}

// Crop to exact forced aspect ratios if enabled and not AspectMode::Stretch.
std::tuple<float, float> Presenter::ApplyStandardAspectCrop(float width, float height,
                                                            bool allow_stretch) const
{
  auto aspect_mode = g_ActiveConfig.aspect_mode;

  if (!allow_stretch && aspect_mode == AspectMode::Stretch)
    aspect_mode = AspectMode::Auto;

  if (!g_ActiveConfig.bCrop || aspect_mode == AspectMode::Stretch)
    return {width, height};

  // Force aspect ratios by cropping the image.
  const float current_aspect = width / height;
  float expected_aspect;
  switch (aspect_mode)
  {
  default:
  case AspectMode::Auto:
    expected_aspect = g_widescreen->IsGameWidescreen() ? (16.0f / 9.0f) : (4.0f / 3.0f);
    break;
  case AspectMode::ForceWide:
    expected_aspect = 16.0f / 9.0f;
    break;
  case AspectMode::ForceStandard:
    expected_aspect = 4.0f / 3.0f;
    break;
  // There should be no cropping needed in the custom case,
  // as output should always exactly match the target aspect ratio
  case AspectMode::Custom:
    expected_aspect = g_ActiveConfig.GetCustomAspectRatio();
    break;
  }

  if (current_aspect > expected_aspect)
  {
    // keep height, crop width
    width = height * expected_aspect;
  }
  else
  {
    // keep width, crop height
    height = width / expected_aspect;
  }

  return {width, height};
}

void Presenter::UpdateDrawRectangle()
{
  const float draw_aspect_ratio = CalculateDrawAspectRatio();

  // Update aspect ratio hack values
  // Won't take effect until next frame
  // Don't know if there is a better place for this code so there isn't a 1 frame delay
  if (g_ActiveConfig.bWidescreenHack)
  {
    auto& vi = Core::System::GetInstance().GetVideoInterface();
    float source_aspect_ratio = vi.GetAspectRatio();
    // If the game is meant to be in widescreen (or forced to),
    // scale the source aspect ratio to it.
    if (g_widescreen->IsGameWidescreen())
      source_aspect_ratio = SourceAspectRatioToWidescreen(source_aspect_ratio);

    const float adjust = source_aspect_ratio / draw_aspect_ratio;
    if (adjust > 1)
    {
      // Vert+
      g_Config.fAspectRatioHackW = 1;
      g_Config.fAspectRatioHackH = 1 / adjust;
    }
    else
    {
      // Hor+
      g_Config.fAspectRatioHackW = adjust;
      g_Config.fAspectRatioHackH = 1;
    }
  }
  else
  {
    // Hack is disabled.
    g_Config.fAspectRatioHackW = 1;
    g_Config.fAspectRatioHackH = 1;
  }

  // The rendering window size
  const float win_width = static_cast<float>(m_backbuffer_width);
  const float win_height = static_cast<float>(m_backbuffer_height);
  const float win_aspect_ratio = win_width / win_height;

  // FIXME: this breaks at very low widget sizes
  // Make ControllerInterface aware of the render window region actually being used
  // to adjust mouse cursor inputs.
  g_controller_interface.SetAspectRatioAdjustment(draw_aspect_ratio / win_aspect_ratio);

  float draw_width = draw_aspect_ratio;
  float draw_height = 1;

  // Crop the picture to a standard aspect ratio. (if enabled)
  auto [crop_width, crop_height] = ApplyStandardAspectCrop(draw_width, draw_height);

  // scale the picture to fit the rendering window
  if (win_aspect_ratio >= crop_width / crop_height)
  {
    // the window is flatter than the picture
    draw_width *= win_height / crop_height;
    crop_width *= win_height / crop_height;
    draw_height *= win_height / crop_height;
    crop_height = win_height;
  }
  else
  {
    // the window is skinnier than the picture
    draw_width *= win_width / crop_width;
    draw_height *= win_width / crop_width;
    crop_height *= win_width / crop_width;
    crop_width = win_width;
  }

  int int_draw_width;
  int int_draw_height;

  if (g_frame_dumper->IsFrameDumping())
  {
    // ensure divisibility by "VIDEO_ENCODER_LCM" to make it compatible with all the video encoders.
    // Note that this is theoretically only necessary when recording videos and not screenshots.
    draw_width =
        std::ceil(draw_width) - static_cast<int>(std::ceil(draw_width)) % VIDEO_ENCODER_LCM;
    draw_height =
        std::ceil(draw_height) - static_cast<int>(std::ceil(draw_height)) % VIDEO_ENCODER_LCM;
    int_draw_width = static_cast<int>(draw_width);
    int_draw_height = static_cast<int>(draw_height);
  }
  else
  {
    const auto int_draw_res =
        FindClosestIntegerResolution(draw_width, draw_height, win_aspect_ratio);
    int_draw_width = std::get<0>(int_draw_res);
    int_draw_height = std::get<1>(int_draw_res);
  }

  m_target_rectangle.left = static_cast<int>(std::round(win_width / 2.0 - int_draw_width / 2.0));
  m_target_rectangle.top = static_cast<int>(std::round(win_height / 2.0 - int_draw_height / 2.0));
  m_target_rectangle.right = m_target_rectangle.left + int_draw_width;
  m_target_rectangle.bottom = m_target_rectangle.top + int_draw_height;
}

std::tuple<float, float> Presenter::ScaleToDisplayAspectRatio(const int width, const int height,
                                                              bool allow_stretch) const
{
  // Scale either the width or height depending the content aspect ratio.
  // This way we preserve as much resolution as possible when scaling.
  float scaled_width = static_cast<float>(width);
  float scaled_height = static_cast<float>(height);
  const float draw_aspect = CalculateDrawAspectRatio(allow_stretch);
  if (scaled_width / scaled_height >= draw_aspect)
    scaled_height = scaled_width / draw_aspect;
  else
    scaled_width = scaled_height * draw_aspect;
  return std::make_tuple(scaled_width, scaled_height);
}

std::tuple<int, int> Presenter::CalculateOutputDimensions(int width, int height,
                                                          bool allow_stretch) const
{
  width = std::max(width, 1);
  height = std::max(height, 1);

  auto [scaled_width, scaled_height] = ScaleToDisplayAspectRatio(width, height, allow_stretch);

  // Apply crop if enabled.
  std::tie(scaled_width, scaled_height) =
      ApplyStandardAspectCrop(scaled_width, scaled_height, allow_stretch);

  auto aspect_mode = g_ActiveConfig.aspect_mode;

  if (!allow_stretch && aspect_mode == AspectMode::Stretch)
    aspect_mode = AspectMode::Auto;

  // Find the closest integer aspect ratio,
  // this avoids a small black line from being drawn on one of the four edges
  if (!g_ActiveConfig.bCrop && aspect_mode != AspectMode::Stretch)
  {
    const float draw_aspect_ratio = CalculateDrawAspectRatio(allow_stretch);
    const auto [int_width, int_height] =
        FindClosestIntegerResolution(scaled_width, scaled_height, draw_aspect_ratio);
    width = int_width;
    height = int_height;
  }
  else
  {
    width = static_cast<int>(std::ceil(scaled_width));
    height = static_cast<int>(std::ceil(scaled_height));
  }

  if (g_frame_dumper->IsFrameDumping())
  {
    // UpdateDrawRectangle() makes sure that the rendered image is divisible by "VIDEO_ENCODER_LCM"
    // for video encoders, so do that here too to match it
    width -= width % VIDEO_ENCODER_LCM;
    height -= height % VIDEO_ENCODER_LCM;
  }

  return std::make_tuple(width, height);
}

void Presenter::RenderXFBToScreen(const MathUtil::Rectangle<int>& target_rc,
                                  const AbstractTexture* source_texture,
                                  const MathUtil::Rectangle<int>& source_rc)
{
  if (g_ActiveConfig.stereo_mode == StereoMode::QuadBuffer &&
      g_ActiveConfig.backend_info.bUsesExplictQuadBuffering)
  {
    // Quad-buffered stereo is annoying on GL.
    g_gfx->SelectLeftBuffer();
    m_post_processor->BlitFromTexture(target_rc, source_rc, source_texture, 0);

    g_gfx->SelectRightBuffer();
    m_post_processor->BlitFromTexture(target_rc, source_rc, source_texture, 1);

    g_gfx->SelectMainBuffer();
  }
  else if (g_ActiveConfig.stereo_mode == StereoMode::SBS ||
           g_ActiveConfig.stereo_mode == StereoMode::TAB)
  {
    const auto [left_rc, right_rc] = ConvertStereoRectangle(target_rc);

    m_post_processor->BlitFromTexture(left_rc, source_rc, source_texture, 0);
    m_post_processor->BlitFromTexture(right_rc, source_rc, source_texture, 1);
  }
  // Every other case will be treated the same (stereo or not).
  // If there's multiple source layers, they should all be copied.
  else
  {
    m_post_processor->BlitFromTexture(target_rc, source_rc, source_texture);
  }
}

void Presenter::Present()
{
  m_present_count++;

  if (g_gfx->IsHeadless() || (!m_onscreen_ui && !m_xfb_entry))
    return;

  if (!g_gfx->SupportsUtilityDrawing())
  {
    // Video Software doesn't support drawing a UI or doing post-processing
    // So just show the XFB
    if (m_xfb_entry)
    {
      g_gfx->ShowImage(m_xfb_entry->texture.get(), m_xfb_rect);

      // Update the window size based on the frame that was just rendered.
      // Due to depending on guest state, we need to call this every frame.
      SetSuggestedWindowSize(m_xfb_rect.GetWidth(), m_xfb_rect.GetHeight());
    }
    return;
  }

  // Since we use the common pipelines here and draw vertices if a batch is currently being
  // built by the vertex loader, we end up trampling over its pointer, as we share the buffer
  // with the loader, and it has not been unmapped yet. Force a pipeline flush to avoid this.
  g_vertex_manager->Flush();

  UpdateDrawRectangle();

  g_gfx->BeginUtilityDrawing();
  g_gfx->BindBackbuffer({{0.0f, 0.0f, 0.0f, 1.0f}});

  // Render the XFB to the screen.
  if (m_xfb_entry)
  {
    // Adjust the source rectangle instead of using an oversized viewport to render the XFB.
    auto render_target_rc = GetTargetRectangle();
    auto render_source_rc = m_xfb_rect;
    AdjustRectanglesToFitBounds(&render_target_rc, &render_source_rc, m_backbuffer_width,
                                m_backbuffer_height);
    RenderXFBToScreen(render_target_rc, m_xfb_entry->texture.get(), render_source_rc);
  }

  if (m_onscreen_ui)
  {
    m_onscreen_ui->Finalize();
    m_onscreen_ui->DrawImGui();
  }

  // Present to the window system.
  {
    std::lock_guard<std::mutex> guard(m_swap_mutex);
    g_gfx->PresentBackbuffer();
  }

  if (m_xfb_entry)
  {
    // Update the window size based on the frame that was just rendered.
    // Due to depending on guest state, we need to call this every frame.
    SetSuggestedWindowSize(m_xfb_rect.GetWidth(), m_xfb_rect.GetHeight());
  }

  if (m_onscreen_ui)
    m_onscreen_ui->BeginImGuiFrame(m_backbuffer_width, m_backbuffer_height);

  g_gfx->EndUtilityDrawing();
}

void Presenter::SetKeyMap(const DolphinKeyMap& key_map)
{
  if (m_onscreen_ui)
    m_onscreen_ui->SetKeyMap(key_map);
}

void Presenter::SetKey(u32 key, bool is_down, const char* chars)
{
  if (m_onscreen_ui)
    m_onscreen_ui->SetKey(key, is_down, chars);
}

void Presenter::SetMousePos(float x, float y)
{
  if (m_onscreen_ui)
    m_onscreen_ui->SetMousePos(x, y);
}

void Presenter::SetMousePress(u32 button_mask)
{
  if (m_onscreen_ui)
    m_onscreen_ui->SetMousePress(button_mask);
}

void Presenter::DoState(PointerWrap& p)
{
  p.Do(m_frame_count);
  p.Do(m_last_xfb_ticks);
  p.Do(m_last_xfb_addr);
  p.Do(m_last_xfb_width);
  p.Do(m_last_xfb_stride);
  p.Do(m_last_xfb_height);

  // If we're loading and there is a last XFB, re-display it.
  if (p.IsReadMode() && m_last_xfb_stride != 0)
  {
    // This technically counts as the end of the frame
    AfterFrameEvent::Trigger();

    ImmediateSwap(m_last_xfb_addr, m_last_xfb_width, m_last_xfb_stride, m_last_xfb_height,
                  m_last_xfb_ticks);
  }
}

}  // namespace VideoCommon
