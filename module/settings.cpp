#include "settings.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <variant>

namespace endfield_plugin {

namespace {

using Value = std::variant<std::monostate, bool, double, std::string>;

bool IEquals(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}

std::string ToUtf8(std::wstring_view text) {
  if (text.empty() || text.size() > 64 * 1024)
    return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (bytes <= 0)
    return {};
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
  return out;
}

// 只认扁平对象 {"key": 字符串|数字|布尔|null}。嵌套值（对象/数组）按"整份无效"处理——schema 里没有这种字段。
class FlatJson {
public:
  explicit FlatJson(std::string_view text) : text_(text) {}

  bool Parse(std::map<std::string, Value> &out) {
    SkipSpace();
    if (!Consume('{'))
      return false;
    SkipSpace();
    if (Consume('}'))
      return true;
    for (;;) {
      std::string key;
      SkipSpace();
      if (!ParseString(key))
        return false;
      SkipSpace();
      if (!Consume(':'))
        return false;
      SkipSpace();
      Value value;
      if (!ParseValue(value))
        return false;
      out[key] = std::move(value);
      SkipSpace();
      if (Consume(','))
        continue;
      return Consume('}');
    }
  }

private:
  void SkipSpace() {
    while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r' || text_[pos_] == '\n'))
      ++pos_;
  }

  bool Consume(char c) {
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool ParseString(std::string &out) {
    if (!Consume('"'))
      return false;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"')
        return true;
      if (static_cast<unsigned char>(c) < 0x20)
        return false;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= text_.size())
        return false;
      const char e = text_[pos_++];
      switch (e) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        if (pos_ + 4 > text_.size())
          return false;
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
          const char h = text_[pos_++];
          code <<= 4;
          if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
          else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
          else return false;
        }
        // 设置值只用 ASCII 令牌；非 ASCII 字符不会被任何字段使用，按占位符保留长度即可
        out.push_back(code < 0x80 ? static_cast<char>(code) : '?');
        break;
      }
      default:
        return false;
      }
    }
    return false;
  }

  bool ParseValue(Value &out) {
    if (pos_ >= text_.size())
      return false;
    const char c = text_[pos_];
    if (c == '"') {
      std::string s;
      if (!ParseString(s))
        return false;
      out = std::move(s);
      return true;
    }
    if (text_.substr(pos_, 4) == "true") {
      pos_ += 4;
      out = true;
      return true;
    }
    if (text_.substr(pos_, 5) == "false") {
      pos_ += 5;
      out = false;
      return true;
    }
    if (text_.substr(pos_, 4) == "null") {
      pos_ += 4;
      out = std::monostate{};
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      const size_t start = pos_;
      while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '-' || text_[pos_] == '+' || text_[pos_] == '.' ||
                                     text_[pos_] == 'e' || text_[pos_] == 'E'))
        ++pos_;
      const std::string number(text_.substr(start, pos_ - start));
      char *end = nullptr;
      const double value = std::strtod(number.c_str(), &end);
      if (end != number.c_str() + number.size() || !std::isfinite(value))
        return false;
      out = value;
      return true;
    }
    return false;  // 对象/数组
  }

  std::string_view text_;
  size_t pos_ = 0;
};

int ReadInt(const std::map<std::string, Value> &values, const char *key, int fallback, int min, int max) {
  const auto it = values.find(key);
  if (it == values.end() || !std::holds_alternative<double>(it->second))
    return fallback;
  const double rounded = std::round(std::get<double>(it->second));
  return static_cast<int>(std::clamp(rounded, static_cast<double>(min), static_cast<double>(max)));
}

bool ReadBool(const std::map<std::string, Value> &values, const char *key, bool fallback) {
  const auto it = values.find(key);
  return it != values.end() && std::holds_alternative<bool>(it->second) ? std::get<bool>(it->second) : fallback;
}

std::string ReadToken(const std::map<std::string, Value> &values, const char *key, std::string fallback) {
  const auto it = values.find(key);
  return it != values.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second) : fallback;
}

}  // namespace

std::string NormalizeScaleMode(std::string_view mode) { return IEquals(mode, "manual") ? "manual" : "auto"; }

std::string NormalizeSafeAreaMode(std::string_view mode) {
  if (IEquals(mode, "manual"))
    return "manual";
  if (IEquals(mode, "off"))
    return "off";
  return "auto";
}

int NormalizeScalePercent(int percent) {
  if (percent == 0)
    return kDefaultScalePercent;
  const int clamped = std::clamp(percent, kMinScalePercent, kMaxScalePercent);
  // 四舍五入到 5 的倍数，远离零取整（与主程序里的 C# 实现一致：Math.Round(x/5, AwayFromZero) * 5）
  const int stepped = static_cast<int>(std::floor(clamped / static_cast<double>(kScaleStepPercent) + 0.5)) * kScaleStepPercent;
  return std::clamp(stepped, kMinScalePercent, kMaxScalePercent);
}

int NormalizeInset(int pixels) { return std::clamp(pixels, 0, kMaxSafeInset); }

std::string ScaleValue(std::string_view mode, int percent) {
  if (NormalizeScaleMode(mode) != "manual")
    return "auto";
  char text[32];
  const int value = NormalizeScalePercent(percent);
  // 不用 printf("%.2f")：它受 C 运行库区域设置影响（逗号小数点会让运行库读错）。百分数 /100 恰好是两位小数，手工拼。
  std::snprintf(text, sizeof(text), "%d.%02d", value / 100, value % 100);
  return text;
}

std::string SafeAreaValue(std::string_view mode, int left, int right) {
  const std::string normalized = NormalizeSafeAreaMode(mode);
  if (normalized == "manual")
    return std::to_string(NormalizeInset(left)) + "," + std::to_string(NormalizeInset(right));
  return normalized;  // "off" 或 "auto"
}

ModuleSettings ParseSettings(std::wstring_view json) {
  ModuleSettings settings;
  std::map<std::string, Value> values;
  const std::string utf8 = ToUtf8(json);
  if (utf8.empty() || !FlatJson(utf8).Parse(values))
    return settings;

  settings.input_mode = IEquals(ReadToken(values, "inputMode", "touch"), "pc") ? "pc" : "touch";  // 已去掉的"自动"等一律按触屏
  settings.auto_resolution = ReadBool(values, "autoResolution", true);
  settings.width = ReadInt(values, "width", 1920, kMinWidth, kMaxWidth);
  settings.height = ReadInt(values, "height", 1080, kMinHeight, kMaxHeight);
  settings.refresh_rate = ReadInt(values, "refreshRate", 60, kMinRefresh, kMaxRefresh);
  settings.fps_unlock = ReadBool(values, "fpsUnlock", true);
  settings.target_fps = ReadInt(values, "targetFps", 120, kMinFps, kMaxFps);
  settings.ui_scale_mode = NormalizeScaleMode(ReadToken(values, "uiScaleMode", "auto"));
  settings.ui_scale_percent = NormalizeScalePercent(ReadInt(values, "uiScalePercent", kDefaultScalePercent, 0, 100000));
  settings.safe_area_mode = NormalizeSafeAreaMode(ReadToken(values, "safeAreaMode", "auto"));
  settings.safe_left = NormalizeInset(ReadInt(values, "safeLeft", 0, -100000, 100000));
  settings.safe_right = NormalizeInset(ReadInt(values, "safeRight", 0, -100000, 100000));
  return settings;
}

}  // namespace endfield_plugin
