// 插件设置：settings_json（宿主已按 plugin.json 的 settingsSchema 归一化）的解析，以及 endfield-ui.ini 的两个值。
// 这里再做一遍同样的归一化是纵深防御：宿主不规范（或旧宿主）时，插件拿到什么都不会抛异常、不会崩，只会回落默认值。
#pragma once

#include <string>
#include <string_view>

namespace endfield_plugin {

struct ModuleSettings {
  std::string input_mode = "touch";  // touch | pc
  bool auto_resolution = true;
  int width = 1920;
  int height = 1080;
  int refresh_rate = 60;
  bool fps_unlock = true;
  int target_fps = 120;
  std::string ui_scale_mode = "auto";  // auto | manual
  int ui_scale_percent = 140;          // 100-180, step 5
  std::string safe_area_mode = "auto"; // auto | off | manual
  int safe_left = 0;                   // 0-600
  int safe_right = 0;
};

constexpr int kMinWidth = 320, kMaxWidth = 7680;
constexpr int kMinHeight = 240, kMaxHeight = 4320;
constexpr int kMinRefresh = 24, kMaxRefresh = 1000;
constexpr int kMinFps = 10, kMaxFps = 1000;
constexpr int kMinScalePercent = 100, kMaxScalePercent = 180, kScaleStepPercent = 5, kDefaultScalePercent = 140;
constexpr int kMaxSafeInset = 600;

// 解析 UTF-16 的 settings_json。空、损坏、不是对象都得到全默认值；类型不对或越界的字段单独回落/夹到范围。永不抛异常。
ModuleSettings ParseSettings(std::wstring_view json);

std::string NormalizeScaleMode(std::string_view mode);
std::string NormalizeSafeAreaMode(std::string_view mode);
int NormalizeScalePercent(int percent);  // 0 = 默认 140；夹到 100-180 并按 5 对齐
int NormalizeInset(int pixels);          // 夹到 0-600

// endfield-ui.ini 里的值：ui_scale = "auto" 或 "1.40"；safe_area = "auto" / "off" / "左,右"。小数点永远是 '.'，与系统区域设置无关。
std::string ScaleValue(std::string_view mode, int percent);
std::string SafeAreaValue(std::string_view mode, int left, int right);

}  // namespace endfield_plugin
