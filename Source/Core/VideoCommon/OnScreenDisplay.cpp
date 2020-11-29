// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <map>
#include <mutex>
#include <string>

#include <fmt/format.h>
#include <imgui.h>

#include "Common/CommonTypes.h"
#include "Common/Timer.h"
#include "Core/ConfigManager.h"
#include "Core/Slippi/SlippiPlayback.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "AudioCommon/AudioCommon.h"

#ifdef IS_PLAYBACK
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#define BUTTON_WIDTH 54.0f
#endif


#include <imgui_internal.h>
#include "Core/Core.h"
#include "Core/Host.h"
#include "Common/Logging/Log.h"
#include "VideoCommon/IconsFontAwesome4.h"

extern std::unique_ptr<SlippiPlaybackStatus> g_playbackStatus;
#endif

namespace OSD
{
  constexpr float LEFT_MARGIN = 10.0f;    // Pixels to the left of OSD messages.
  constexpr float TOP_MARGIN = 10.0f;     // Pixels above the first OSD message.
  constexpr float WINDOW_PADDING = 4.0f;  // Pixels between subsequent OSD messages.

  struct Message
  {
    Message() = default;
    Message(std::string text_, u32 timestamp_, u32 color_)
      : text(std::move(text_)), timestamp(timestamp_), color(color_)
    {
    }
    std::string text;
    u32 timestamp = 0;
    u32 color = 0;
  };
  static std::multimap<MessageType, Message> s_messages;
  static std::mutex s_messages_mutex;

  static ImVec4 RGBAToImVec4(const u32 rgba)
  {
    return ImVec4(static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
      static_cast<float>((rgba >> 8) & 0xFF) / 255.0f,
      static_cast<float>((rgba >> 0) & 0xFF) / 255.0f,
      static_cast<float>((rgba >> 24) & 0xFF) / 255.0f);
  }

  static float DrawMessage(int index, const Message& msg, const ImVec2& position, int time_left)
  {
    // We have to provide a window name, and these shouldn't be duplicated.
    // So instead, we generate a name based on the number of messages drawn.
    const std::string window_name = fmt::format("osd_{}", index);

    // The size must be reset, otherwise the length of old messages could influence new ones.
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f));

    // Gradually fade old messages away.
    const float alpha = std::min(1.0f, std::max(0.0f, time_left / 1024.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    float window_height = 0.0f;
    if (ImGui::Begin(window_name.c_str(), nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing))
    {
      // Use %s in case message contains %.
      ImGui::TextColored(RGBAToImVec4(msg.color), "%s", msg.text.c_str());
      window_height =
        ImGui::GetWindowSize().y + (WINDOW_PADDING * ImGui::GetIO().DisplayFramebufferScale.y);
    }

    ImGui::End();
    ImGui::PopStyleVar();

    return window_height;
  }

  void AddTypedMessage(MessageType type, std::string message, u32 ms, u32 rgba)
  {
    std::lock_guard lock{ s_messages_mutex };
    s_messages.erase(type);
    s_messages.emplace(type, Message(std::move(message), Common::Timer::GetTimeMs() + ms, rgba));
  }

  void AddMessage(std::string message, u32 ms, u32 rgba)
  {
    std::lock_guard lock{ s_messages_mutex };
    s_messages.emplace(MessageType::Typeless,
      Message(std::move(message), Common::Timer::GetTimeMs() + ms, rgba));
  }

  void DrawMessages()
  {
    const bool draw_messages = SConfig::GetInstance().bOnScreenDisplayMessages;
    const u32 now = Common::Timer::GetTimeMs();
    const float current_x = LEFT_MARGIN * ImGui::GetIO().DisplayFramebufferScale.x;
    float current_y = TOP_MARGIN * ImGui::GetIO().DisplayFramebufferScale.y;
    int index = 0;

    std::lock_guard lock{ s_messages_mutex };

    for (auto it = s_messages.begin(); it != s_messages.end();)
    {
      const Message& msg = it->second;
      const int time_left = static_cast<int>(msg.timestamp - now);

      if (time_left <= 0)
      {
        it = s_messages.erase(it);
        continue;
      }
      else
      {
        ++it;
      }

      if (draw_messages)
        current_y += DrawMessage(index++, msg, ImVec2(current_x, current_y), time_left);
    }
  }

  void ClearMessages()
  {
    std::lock_guard lock{ s_messages_mutex };
    s_messages.clear();
  }

#ifdef IS_PLAYBACK
  static s32 frame = 0;

  static std::string GetTimeForFrame(s32 currFrame) {
    int currSeconds = int((currFrame - Slippi::GAME_FIRST_FRAME) / 60);
    int currMinutes = (int)(currSeconds / 60);
    int currRemainder = (int)(currSeconds % 60);
    // Position string (i.e. MM:SS)
    char currTime[6];
    sprintf(currTime, "%02d:%02d", currMinutes, currRemainder);
    return std::string(currTime);
  }

  bool showHelp = false;
  u32 idle_tick = Common::Timer::GetTimeMs();
  ImVec2 prev_mouse(0, 0);

  bool ButtonCustom(const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags = 0)
  {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
      return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrentLineTextBaseOffset) // Try to vertically align buttons that are smaller/have no padding so that text baseline matches (bit hacky, since it shouldn't be a flag)
      pos.y += window->DC.CurrentLineTextBaseOffset - style.FramePadding.y;
    ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
      return false;

    if (window->DC.ItemFlags & ImGuiItemFlags_ButtonRepeat)
      flags |= ImGuiButtonFlags_Repeat;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);
    if (pressed)
      ImGui::MarkItemEdited(id);

    // Render
    const ImU32 col = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::RenderNavHighlight(bb, id);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

    if (hovered || held)
      ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, style.ButtonTextAlign, &bb, ImVec4(0.9f, 0.9f, 0.9f, style.Alpha));
    else
      ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, style.ButtonTextAlign, &bb, ImVec4(0.9f, 0.9f, 0.9f, 0.6f * style.Alpha));

    // Automatically close popups
    //if (pressed && !(flags & ImGuiButtonFlags_DontClosePopups) && (window->Flags & ImGuiWindowFlags_Popup))
    //    CloseCurrentPopup();

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.LastItemStatusFlags);
    return pressed;
  }

  bool SeekBarBehavior(const ImRect& bb, ImGuiID id, int* v, int v_min, int v_max, float power, ImGuiSliderFlags flags, ImVec4 color, ImVec2 valuesize, const char* label, char* value)
  {
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiAxis axis = (flags & ImGuiSliderFlags_Vertical) ? ImGuiAxis_Y : ImGuiAxis_X;
    const bool is_decimal = false; // TODO handle other types
    const bool is_power = (power != 1.0f) && is_decimal;

    const float slider_sz = (bb.Max[axis] - bb.Min[axis]);
    const float slider_usable_pos_min = bb.Min[axis];
    const float slider_usable_pos_max = bb.Max[axis];

    float linear_zero_pos = 0.0f;
    if (is_power && v_min * v_max < 0.0f)
    {
      const float linear_dist_min_to_0 = ImPow(v_min >= 0 ? (float)v_min : -(float)v_min, (float)1.0f / power);
      const float linear_dist_max_to_0 = ImPow(v_max >= 0 ? (float)v_max : -(float)v_max, (float)1.0f / power);
      linear_zero_pos = (float)(linear_dist_min_to_0 / (linear_dist_min_to_0 + linear_dist_max_to_0));
    }
    else
    {
      linear_zero_pos = v_min < 0.0f ? 1.0f : 0.0f;
    }

    const bool isDown = g.IO.MouseDown[0];
    bool value_changed = false;
    bool isActive = g.ActiveId == id;
    static bool isHeld = false;

    auto hover_bb = ImRect(
      ImVec2(5.0f, ImGui::GetWindowHeight() - 95.0f),
      ImVec2(ImGui::GetWindowWidth() - 5.0f, bb.Min.y));

    const bool hovered = ImGui::ItemHoverable(hover_bb, id);

    if (!isHeld && isActive) {
      ImGui::ClearActiveID();
    }

    // Calculate mouse position if hovered
    int new_value = 0;
    if (hovered || isHeld) {
      const float mouse_abs_pos = g.IO.MousePos[axis];
      float clicked_t = (slider_sz > 0.0f) ? ImClamp((mouse_abs_pos - slider_usable_pos_min) / slider_sz, 0.0f, 1.0f) : 0.0f;
      if (axis == ImGuiAxis_Y)
        clicked_t = 1.0f - clicked_t;

      if (is_power)
      {
        if (clicked_t < linear_zero_pos)
        {
          float a = 1.0f - (clicked_t / linear_zero_pos);
          a = ImPow(a, power);
          new_value = ImLerp(ImMin(v_max, 0), v_min, a);
        }
        else
        {
          float a;
          if (ImFabs(linear_zero_pos - 1.0f) > 1.e-6f)
            a = (clicked_t - linear_zero_pos) / (1.0f - linear_zero_pos);
          else
            a = clicked_t;
          a = ImPow(a, power);
          new_value = ImLerp(ImMax(v_min, 0), v_max, a);
        }
      }
      else
      {
        new_value = ImLerp(v_min, v_max, clicked_t);
      }

      // Only change value if left mouse button is actually down
      if (*v != new_value && isDown)
      {
        *v = new_value;
      }
    }

    if (isHeld) {
      ImGui::SetActiveID(id, window);
      isHeld = isHeld && isDown;
      // If no longer held, slider was let go. Trigger mark edited
      if (!isHeld) {
        value_changed = true;
        g_playbackStatus->targetFrameNum = *v;
      }
    }
    else
      isHeld = hovered && isDown;

    float new_grab_t = ImGui::SliderCalcRatioFromValueT<int, float>(ImGuiDataType_S32, new_value, v_min, v_max, power, linear_zero_pos);
    float curr_grab_t = ImGui::SliderCalcRatioFromValueT<int, float>(ImGuiDataType_S32, *v, v_min, v_max, power, linear_zero_pos);

    if (axis == ImGuiAxis_Y) {
      new_grab_t = 1.0f - new_grab_t;
      curr_grab_t = 1.0f - curr_grab_t;
    }
    const float new_grab_pos = ImLerp(slider_usable_pos_min, slider_usable_pos_max, new_grab_t);
    const float curr_grab_pos = ImLerp(slider_usable_pos_min, slider_usable_pos_max, curr_grab_t);

    // Draw all the things

    // Darken screen when seeking
    if (isHeld)
      window->DrawList->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.6f)));

    window->DrawList->AddRectFilled(ImVec2(0, bb.Min.y), ImVec2(ImGui::GetWindowWidth(), ImGui::GetWindowHeight()), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.75f * style.Alpha)));

    // Grey background line
    window->DrawList->AddLine(ImVec2(bb.Min.x, bb.Min.y - 4.0f), ImVec2(bb.Max.x, bb.Min.y - 4.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.5f * style.Alpha)), 8);

    // Whiter, more opaque line up to mouse position
    if (hovered && !isHeld)
      window->DrawList->AddLine(ImVec2(bb.Min.x, bb.Min.y - 4.0f), ImVec2(new_grab_pos, bb.Min.y - 4.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, style.Alpha)), 8);

    if (hovered || isHeld)
      window->DrawList->AddText(ImVec2(new_grab_pos - valuesize.x / 2, bb.Min.y - 30), ImColor(255, 255, 255), GetTimeForFrame(new_value).c_str());

    // Colored line, circle indicator, and text
    if (isHeld) {
      window->DrawList->AddLine(ImVec2(bb.Min.x, bb.Min.y - 4.0f), ImVec2(new_grab_pos, bb.Min.y - 4.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 1.0f, 0.0f, 1.0)), 8);
      window->DrawList->AddCircleFilled(ImVec2(new_grab_pos, bb.Min.y - 2.0f), 12.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 1.0f, 0.0f, 1.0)));
    }

    // Progress bar
    if (!isHeld) {
      frame = (g_playbackStatus->targetFrameNum == INT_MAX) ? g_playbackStatus->currentPlaybackFrame : g_playbackStatus->targetFrameNum;
      window->DrawList->AddLine(ImVec2(bb.Min.x, bb.Min.y - 4.0f), ImVec2(curr_grab_pos, bb.Min.y - 4.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 1.0f, 0.0f, style.Alpha)), 8);
    }

    return value_changed;
  }

  bool VolumeBarBehavior(const ImRect& bb, ImGuiID id, int* v, int v_min, int v_max, float power,
    ImGuiSliderFlags flags, ImVec4 color)
  {
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiAxis axis = (flags & ImGuiSliderFlags_Vertical) ? ImGuiAxis_Y : ImGuiAxis_X;
    const bool is_decimal = false;  // TODO handle other types
    const bool is_power = (power != 1.0f) && is_decimal;

    const float slider_sz = (bb.Max[axis] - bb.Min[axis]);
    const float slider_usable_pos_min = bb.Min[axis];
    const float slider_usable_pos_max = bb.Max[axis];

    float linear_zero_pos = 0.0f;
    if (is_power && v_min * v_max < 0.0f)
    {
      const float linear_dist_min_to_0 =
        ImPow(v_min >= 0 ? (float)v_min : -(float)v_min, (float)1.0f / power);
      const float linear_dist_max_to_0 =
        ImPow(v_max >= 0 ? (float)v_max : -(float)v_max, (float)1.0f / power);
      linear_zero_pos = (float)(linear_dist_min_to_0 / (linear_dist_min_to_0 + linear_dist_max_to_0));
    }
    else
    {
      linear_zero_pos = v_min < 0.0f ? 1.0f : 0.0f;
    }

    const bool isDown = g.IO.MouseDown[0];
    const bool hovered = ImGui::ItemHoverable(bb, id);
    static bool isHeld = false;
    bool value_changed = false;
    bool isActive = g.ActiveId == id;

    if (!isHeld && isActive)
      ImGui::ClearActiveID();

    if (isHeld)
      ImGui::SetActiveID(id, window);

    // Calculate mouse position if clicked or held
    int new_value = 0;
    if (isHeld || hovered)
    {
      const float mouse_abs_pos = g.IO.MousePos[axis];
      float clicked_t = (slider_sz > 0.0f) ?
        ImClamp((mouse_abs_pos - slider_usable_pos_min) / slider_sz, 0.0f, 1.0f) :
        0.0f;
      if (axis == ImGuiAxis_Y)
        clicked_t = 1.0f - clicked_t;

      if (is_power)
      {
        if (clicked_t < linear_zero_pos)
        {
          float a = 1.0f - (clicked_t / linear_zero_pos);
          a = ImPow(a, power);
          new_value = ImLerp(ImMin(v_max, 0), v_min, a);
        }
        else
        {
          float a;
          if (ImFabs(linear_zero_pos - 1.0f) > 1.e-6f)
            a = (clicked_t - linear_zero_pos) / (1.0f - linear_zero_pos);
          else
            a = clicked_t;
          a = ImPow(a, power);
          new_value = ImLerp(ImMax(v_min, 0), v_max, a);
        }
      }
      else
      {
        new_value = ImLerp(v_min, v_max, clicked_t);
      }

      // Only change value if left mouse button is actually down
      if (*v != new_value && isDown)
      {
        value_changed = true;
        *v = new_value;
      }
    }

    isHeld = isHeld ? isHeld && isDown : hovered && isDown;

    float grab_t = ImGui::SliderCalcRatioFromValueT<int, float>(ImGuiDataType_S32, *v, v_min,
      v_max, power, linear_zero_pos);
    if (axis == ImGuiAxis_Y)
    {
      grab_t = 1.0f - grab_t;
    }

    const float grab_pos = ImLerp(slider_usable_pos_min, slider_usable_pos_max, grab_t);

    // Grey background line
    window->DrawList->AddLine(
      ImVec2(bb.Min.x, bb.Max.y - 5.0f), ImVec2(bb.Max.x, bb.Max.y - 5.0f),
      ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.5f * style.Alpha)), 8);

    // Colored line and circle indicator
    window->DrawList->AddLine(ImVec2(bb.Min.x, bb.Max.y - 5.0f), ImVec2(grab_pos, bb.Max.y - 5.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, style.Alpha)), 8);

    if (isHeld)
      window->DrawList->AddCircleFilled(
        ImVec2(grab_pos, bb.Max.y - 5.0f), 12,
        ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, style.Alpha)));

    return value_changed;
  }

  bool SeekBar(const char* label, ImVec4 color, int* v, int v_min, int v_max, float power, const char* format)
  {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
      return false;

    const ImGuiID id = window->GetID(label);
    const float w = ImGui::GetWindowWidth() - 10;

    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true) * 1.0f;
    const ImRect frame_bb(
      ImVec2(0.0f, ImGui::GetWindowHeight() - 70.0f),
      ImVec2(ImGui::GetWindowWidth(), ImGui::GetWindowHeight()));

    if (!format)
      format = "%d";

    char value_buf[64];
    const char* value_buf_end = value_buf + ImFormatString(value_buf, IM_ARRAYSIZE(value_buf), format, *v);
    const bool value_changed = SeekBarBehavior(frame_bb, id, v, v_min, v_max, power, ImGuiSliderFlags_None, color, ImGui::CalcTextSize(value_buf, NULL, true), value_buf_end, value_buf);

    return value_changed;
  }

  bool VolumeBar(const char* label, ImVec4 color, int* v, int v_min, int v_max, float power)
  {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
      return false;

    const ImGuiID id = window->GetID(label);
    const ImRect frame_bb(ImVec2(BUTTON_WIDTH * 5, ImGui::GetWindowHeight() - 50.0f), ImVec2(BUTTON_WIDTH * 5 + 80.0f, ImGui::GetWindowHeight() - 32.0f));

    const bool value_changed =
      VolumeBarBehavior(frame_bb, id, v, v_min, v_max, power, ImGuiSliderFlags_None, color);

    return value_changed;
  }

  void DrawSlippiPlaybackControls()
  {
    const auto height = ImGui::GetWindowHeight();
    // We have to provide a window name, and these shouldn't be duplicated.
    // So instead, we generate a name based on the number of messages drawn.
    const std::string window_name = fmt::format("Slippi Playback Controls");
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    auto mousePos = ImGui::GetMousePos();

    auto currTime = Common::Timer::GetTimeMs();
    if (!(mousePos[0] == prev_mouse[0] && mousePos[1] == prev_mouse[1])) {
      idle_tick = currTime;
    }
    prev_mouse = mousePos;

    s32 diff = currTime - idle_tick;
    diff = diff < 1000 ? 0 : diff - 1000;


    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowBorderSize = 0.0f;

    style.WindowPadding = ImVec2(0.0f, 0.0f);

    if (ImGui::Begin(window_name.c_str(), nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing))
    {
#define LABEL_BOX_TOP (ImGui::GetWindowHeight() - 150.0f)
#define LABEL_BOX_BOTTOM (ImGui::GetWindowHeight() - 96.0f)
#define LABEL_TEXT_HEIGHT (ImGui::GetWindowHeight() - 140.0f)
      ImGui::SetWindowFontScale(0.25f);

      if (SeekBar("SlippiSeek", ImVec4(1.0f, 0.0f, 0.0f, 1.0f), &frame, Slippi::PLAYBACK_FIRST_SAVE, g_playbackStatus->lastFrame, 1.0, "%d")) {
        Host_PlaybackSeek();
      }

      style.Alpha = (showHelp || ImGui::GetHoveredID()) ? 1 : std::max(0.0001f, 1.0f - (diff / 1000.0f));

      ImGui::SetCursorPos(ImVec2(0.0f, ImGui::GetWindowHeight() - 62.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.45f));
      //if (ButtonCustom(paused ? ICON_FA_PLAY : ICON_FA_PAUSE, ImVec2(40.0f, BUTTON_WIDTH))) {
      //  INFO_LOG(SLIPPI, "playing");
      //}
      //ImGui::SameLine(0.0f, 5.0f);
      if (ButtonCustom(ICON_FA_FAST_BACKWARD, ImVec2(BUTTON_WIDTH, BUTTON_WIDTH))) {
        if (g_playbackStatus->targetFrameNum == INT_MAX) {
          g_playbackStatus->targetFrameNum = g_playbackStatus->currentPlaybackFrame - 1200;
          Host_PlaybackSeek();
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(5.0f, LABEL_BOX_TOP),
          ImVec2(400.0f, LABEL_BOX_BOTTOM),
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.9f)));
        ImGui::SetCursorPos(ImVec2(15.0f, LABEL_TEXT_HEIGHT));
        ImGui::Text("Jump Back (Shift + Left Arrow)");
      }

      // Step back
      ImGui::SetCursorPos(ImVec2(BUTTON_WIDTH, ImGui::GetWindowHeight() - 62.0f));
      if (ButtonCustom(ICON_FA_STEP_BACKWARD, ImVec2(BUTTON_WIDTH, BUTTON_WIDTH))) {
        if (g_playbackStatus->targetFrameNum == INT_MAX) {
          g_playbackStatus->targetFrameNum = g_playbackStatus->currentPlaybackFrame - 300;
          Host_PlaybackSeek();
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(54.0f, LABEL_BOX_TOP),
          ImVec2(450.0f, LABEL_BOX_BOTTOM),
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.9f)));
        ImGui::SetCursorPos(ImVec2(64.0f, LABEL_TEXT_HEIGHT));
        ImGui::Text("Step Back (Left Arrow)");
      }

      // Step forward
      ImGui::SetCursorPos(ImVec2(BUTTON_WIDTH * 2, ImGui::GetWindowHeight() - 62.0f));
      if (ButtonCustom(ICON_FA_STEP_FORWARD, ImVec2(BUTTON_WIDTH, BUTTON_WIDTH))) {
        if (g_playbackStatus->targetFrameNum == INT_MAX) {
          g_playbackStatus->targetFrameNum = g_playbackStatus->currentPlaybackFrame + 300;
          Host_PlaybackSeek();
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(108.0f, LABEL_BOX_TOP),
          ImVec2(600.0f, LABEL_BOX_BOTTOM),
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.9f)));
        ImGui::SetCursorPos(ImVec2(118.0f, LABEL_TEXT_HEIGHT));
        ImGui::Text("Step Forward (Right Arrow)");
      }

      // Jump forward
      ImGui::SetCursorPos(ImVec2(BUTTON_WIDTH * 3, ImGui::GetWindowHeight() - 62.0f));
      if (ButtonCustom(ICON_FA_FAST_FORWARD, ImVec2(BUTTON_WIDTH, BUTTON_WIDTH))) {
        if (g_playbackStatus->targetFrameNum == INT_MAX) {
          g_playbackStatus->targetFrameNum = g_playbackStatus->currentPlaybackFrame + 1200;
          Host_PlaybackSeek();
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(162.0f, LABEL_BOX_TOP),
          ImVec2(662.0f, LABEL_BOX_BOTTOM),
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.9f)));
        ImGui::SetCursorPos(ImVec2(172.0f, LABEL_TEXT_HEIGHT));
        ImGui::Text("Jump Forward (Shift + Right Arrow)");
      }

      // Volume
      static bool isIconHovered = false;
      static bool isVolumeVisible = false;
      int* volume = &SConfig::GetInstance().m_Volume;
      static int prev;
      ImGui::SetCursorPos(ImVec2(BUTTON_WIDTH * 4, ImGui::GetWindowHeight() - 62.0f));
      if (ButtonCustom(*volume == 0 ? ICON_FA_VOLUME_OFF : ICON_FA_VOLUME_UP, ImVec2(BUTTON_WIDTH, BUTTON_WIDTH))) {
        if (*volume == 0) {
          *volume = prev == 0 ? 30 : prev; // todo: find good default value
        }
        else {
          prev = *volume;
          *volume = 0;
        }
        AudioCommon::UpdateSoundStream();
      }

      if (VolumeBar("SlippiVolume", ImVec4(1.0f, 0.0f, 0.0f, 1.0f), volume, 0, 100, 1.0)) {
        AudioCommon::UpdateSoundStream();
      }

      // Help
      ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - BUTTON_WIDTH * 2, ImGui::GetWindowHeight() - 62.0f));
      if (ButtonCustom(ICON_FA_QUESTION_CIRCLE, ImVec2(BUTTON_WIDTH, BUTTON_WIDTH))) {
        showHelp = !showHelp;
      }
      if (showHelp) {
        ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(ImGui::GetWindowWidth() - 600.0f, ImGui::GetWindowHeight() - 416.0f),
          ImVec2(ImGui::GetWindowWidth() - 50.0f, LABEL_BOX_BOTTOM),
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.8f * style.Alpha)));
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 580.0f, ImGui::GetWindowHeight() - 386.0f));
        ImGui::Text("Play/Pause: Spacebar");
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 580.0f, ImGui::GetWindowHeight() - 346.0f));
        ImGui::Text("Step Back (5s): Left Arrow");
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 580.0f, ImGui::GetWindowHeight() - 306.0f));
        ImGui::Text("Step Forward (5s): Right Arrow");
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 580.0f, ImGui::GetWindowHeight() - 266.0f));
        ImGui::Text("Jump Back (20s): Shift + Left Arrow");
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 580.0f, ImGui::GetWindowHeight() - 226.0f));
        ImGui::Text("Jump Forward (20s): Shift + Right Arrow");
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 580.0f, ImGui::GetWindowHeight() - 186.0f));
        ImGui::Text("Frame Advance: Period");
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 580.0f, ImGui::GetWindowHeight() - 146.0f));
        ImGui::Text("Big jumps may take several seconds.");
      }
      if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(ImGui::GetWindowWidth() - 150.0f, LABEL_BOX_TOP),
          ImVec2(ImGui::GetWindowWidth() - 54.0f, LABEL_BOX_BOTTOM),
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.9f)));
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 140.0f, LABEL_TEXT_HEIGHT));
        ImGui::Text("View Help");
      }

      // Fullscreen
      ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - BUTTON_WIDTH, ImGui::GetWindowHeight() - 62.0f));
      if (ButtonCustom(ICON_FA_EXPAND, ImVec2(BUTTON_WIDTH, BUTTON_WIDTH))) {
        Host_Fullscreen();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(ImGui::GetWindowWidth() - 500.0f, LABEL_BOX_TOP),
          ImVec2(ImGui::GetWindowWidth() - 5.0f, LABEL_BOX_BOTTOM),
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.9f)));
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 490.0f, LABEL_TEXT_HEIGHT));
        ImGui::Text("Toggle Fullscreen (Alt + Enter)");
      }

      ImGui::PopStyleVar();

      // Time text
      ImGui::SetCursorPos(ImVec2(380.0f, ImGui::GetWindowHeight() - 50.0f));
      auto playbackTime = GetTimeForFrame(g_playbackStatus->currentPlaybackFrame);
      auto endTime = GetTimeForFrame(g_playbackStatus->lastFrame);
      auto timeString = playbackTime + " / " + endTime;
      ImGui::Text("%s", timeString.c_str());
    }
    ImGui::End();
  }
#endif
}  // namespace OSD
