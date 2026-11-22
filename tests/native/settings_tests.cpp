// 设置解析与 ini 值的测试（原 C# EndfieldUiSettingsTests / EndfieldLaunchRequestTests 的原生版）。退出码 0 = 全部通过。
#include <clocale>

#include "endfield_test_support.h"
#include "settings.h"

using namespace endfield_test;
using namespace endfield_plugin;

namespace {

void TestScaleValue() {
  CHECK(ScaleValue("auto", 140) == "auto");
  CHECK(ScaleValue("manual", 140) == "1.40");
  CHECK(ScaleValue("manual", 100) == "1.00");
  CHECK(ScaleValue("manual", 180) == "1.80");
  CHECK(ScaleValue("MANUAL", 125) == "1.25");  // 模式不分大小写
  CHECK(ScaleValue("manual", 0) == "1.40");    // 0 = 没人发这个字段 = 默认
  CHECK(ScaleValue("manual", 9999) == "1.80"); // 夹到上限
  CHECK(ScaleValue("manual", -50) == "1.00");  // 夹到下限
  CHECK(ScaleValue("manual", 133) == "1.35");  // 对齐到 5 的倍数
  CHECK(ScaleValue("manual", 112) == "1.10");
  CHECK(ScaleValue("garbage", 150) == "auto"); // 不认识的模式 = auto
}

void TestScaleValueIgnoresLocale() {
  // 运行库按 '.' 读小数；即使进程区域设置的小数点是逗号，也必须写出点号
  const char *previous = std::setlocale(LC_NUMERIC, nullptr);
  const std::string saved = previous ? previous : "C";
  const char *german = std::setlocale(LC_NUMERIC, "de-DE");
  CHECK(ScaleValue("manual", 135) == "1.35");
  CHECK(ScaleValue("manual", 135).find(',') == std::string::npos);
  (void)german;
  std::setlocale(LC_NUMERIC, saved.c_str());
}

void TestSafeAreaValue() {
  CHECK(SafeAreaValue("auto", 5, 6) == "auto");
  CHECK(SafeAreaValue("off", 5, 6) == "off");
  CHECK(SafeAreaValue("manual", 186, 60) == "186,60");
  CHECK(SafeAreaValue("manual", -5, 99999) == "0,600");  // 夹到 0-600
  CHECK(SafeAreaValue("???", 1, 2) == "auto");
  CHECK(SafeAreaValue("OFF", 1, 2) == "off");
}

void TestNormalizers() {
  CHECK(NormalizeScaleMode("manual") == "manual");
  CHECK(NormalizeScaleMode("") == "auto");
  CHECK(NormalizeSafeAreaMode("manual") == "manual");
  CHECK(NormalizeSafeAreaMode("off") == "off");
  CHECK(NormalizeSafeAreaMode("x") == "auto");
  CHECK(NormalizeScalePercent(0) == 140);
  CHECK(NormalizeScalePercent(1) == 100);
  CHECK(NormalizeScalePercent(1000) == 180);
  CHECK(NormalizeScalePercent(102) == 100);
  CHECK(NormalizeScalePercent(103) == 105);
  CHECK(NormalizeInset(-1) == 0);
  CHECK(NormalizeInset(601) == 600);
}

void TestEmptyAndBrokenJsonGiveDefaults() {
  for (const wchar_t *text : {L"", L"   ", L"{", L"not json", L"[1,2]", L"\"x\"", L"{\"a\":}", L"{\"a\":{\"nested\":1}}", L"{\"a\":[1]}", L"{} trailing"}) {
    const ModuleSettings s = ParseSettings(text);
    CHECK(s.input_mode == "touch");
    CHECK(s.auto_resolution);
    CHECK(s.width == 1920 && s.height == 1080 && s.refresh_rate == 60);
    CHECK(s.fps_unlock && s.target_fps == 120);
    CHECK(s.ui_scale_mode == "auto" && s.ui_scale_percent == 140);
    CHECK(s.safe_area_mode == "auto" && s.safe_left == 0 && s.safe_right == 0);
  }
}

void TestSettingsParsedFromJson() {
  const ModuleSettings s = ParseSettings(
      L"{ \"inputMode\": \"pc\", \"autoResolution\": false, \"width\": 2560, \"height\": 1440, \"refreshRate\": 165, \"fpsUnlock\": true, \"targetFps\": 240,"
      L"  \"uiScaleMode\": \"manual\", \"uiScalePercent\": 150, \"safeAreaMode\": \"manual\", \"safeLeft\": 186, \"safeRight\": 60, \"unknownKey\": 1 }");
  CHECK(s.input_mode == "pc");
  CHECK(!s.auto_resolution);
  CHECK(s.width == 2560 && s.height == 1440 && s.refresh_rate == 165);
  CHECK(s.fps_unlock && s.target_fps == 240);
  CHECK(s.ui_scale_mode == "manual" && s.ui_scale_percent == 150);
  CHECK(s.safe_area_mode == "manual" && s.safe_left == 186 && s.safe_right == 60);
}

void TestOutOfRangeAndWrongTypesFallBack() {
  // 已经去掉的"自动"输入模式和任何不认识的值按触屏处理；类型不对的字段回落默认，越界的夹到范围
  const ModuleSettings s = ParseSettings(
      L"{ \"inputMode\": \"auto\", \"autoResolution\": \"yes\", \"width\": 99999, \"height\": 1, \"refreshRate\": 5000, \"targetFps\": \"fast\", \"fpsUnlock\": 1,"
      L"  \"uiScaleMode\": \"weird\", \"uiScalePercent\": 133, \"safeAreaMode\": 7, \"safeLeft\": -4, \"safeRight\": 1000000 }");
  CHECK(s.input_mode == "touch");
  CHECK(s.auto_resolution);                       // 类型不对 → 默认 true
  CHECK(s.width == kMaxWidth);                    // 夹到上限
  CHECK(s.height == kMinHeight);                  // 夹到下限
  CHECK(s.refresh_rate == kMaxRefresh);
  CHECK(s.target_fps == 120);                     // 类型不对 → 默认
  CHECK(s.fps_unlock);                            // 类型不对 → 默认 true
  CHECK(s.ui_scale_mode == "auto");
  CHECK(s.ui_scale_percent == 135);               // 133 → 对齐到 135
  CHECK(s.safe_area_mode == "auto");
  CHECK(s.safe_left == 0 && s.safe_right == kMaxSafeInset);
}

void TestStringEscapesAndNulls() {
  const ModuleSettings s = ParseSettings(L"{ \"inputMode\": \"p\\u0063\", \"width\": null, \"note\": \"a\\\"b\\\\c\" }");
  CHECK(s.input_mode == "pc");  // c = 'c'
  CHECK(s.width == 1920);       // null → 默认
}

void TestDoubleValuesAreRounded() {
  const ModuleSettings s = ParseSettings(L"{ \"width\": 1279.6, \"targetFps\": 143.4, \"uiScalePercent\": 1.5e2 }");
  CHECK(s.width == 1280);
  CHECK(s.target_fps == 143);
  CHECK(s.ui_scale_percent == 150);
}

}  // namespace

int wmain() {
  Run("settings: ui_scale values", TestScaleValue);
  Run("settings: the decimal point does not depend on the locale", TestScaleValueIgnoresLocale);
  Run("settings: safe_area values", TestSafeAreaValue);
  Run("settings: normalizers", TestNormalizers);
  Run("settings: empty / broken json gives the defaults", TestEmptyAndBrokenJsonGiveDefaults);
  Run("settings: every field is read from json", TestSettingsParsedFromJson);
  Run("settings: wrong types fall back, out-of-range values are clamped", TestOutOfRangeAndWrongTypesFallBack);
  Run("settings: escapes and nulls", TestStringEscapesAndNulls);
  Run("settings: fractional numbers are rounded", TestDoubleValuesAreRounded);
  std::printf("%d tests, %d failed checks\n", g_run, g_failed);
  return g_failed ? 1 : 0;
}
