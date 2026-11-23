#include "ui_ini.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace endfield_plugin {

namespace {

constexpr int kMoveAttempts = 8;

bool IEquals(const std::string &a, const std::string &b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

std::string Trim(const std::string &text) {
  size_t start = 0, end = text.size();
  while (start < end && std::isspace(static_cast<unsigned char>(text[start])))
    ++start;
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
    --end;
  return text.substr(start, end - start);
}

bool IsComment(const std::string &line) {
  size_t i = 0;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
    ++i;
  return i < line.size() && (line[i] == '#' || line[i] == ';' || line[i] == '[');
}

// 空字符串 = 这一行不是 "key=value"（空行、注释、没有等号）
std::string KeyOf(const std::string &line) {
  const std::string trimmed = Trim(line);
  if (trimmed.empty() || IsComment(trimmed))
    return {};
  const size_t equals = trimmed.find('=');
  return equals == std::string::npos || equals == 0 ? std::string() : Trim(trimmed.substr(0, equals));
}

std::vector<std::string> SplitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::string normalized;
  normalized.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
      continue;  // CRLF → LF
    normalized.push_back(text[i]);
  }
  size_t start = 0;
  for (;;) {
    const size_t at = normalized.find('\n', start);
    if (at == std::string::npos) {
      lines.push_back(normalized.substr(start));
      break;
    }
    lines.push_back(normalized.substr(start, at - start));
    start = at + 1;
  }
  return lines;
}

bool ReadFileText(const std::wstring &path, std::string &out) {
  out.clear();
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;  // 文件不存在 = 空文本；其他错误 = 失败
  }
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart < 1024 * 1024;
  if (ok) {
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ok = out.empty() || (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size());
  }
  CloseHandle(file);
  if (ok && out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF)
    out.erase(0, 3);  // 带 BOM 的旧文件：按正常文本读，写回时不再带 BOM
  return ok;
}

bool WriteAtomic(const std::wstring &path, const std::string &content, std::wstring *error) {
  const std::filesystem::path target(path);
  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  UUID id{};
  UuidCreate(&id);
  wchar_t suffix[40];
  swprintf_s(suffix, L"%08lX%04X%04X", id.Data1, id.Data2, id.Data3);
  const std::wstring temp = (target.parent_path() / (std::wstring(L".") + kIniFileName + L".tmp-" + suffix)).wstring();

  HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if (error)
      *error = L"无法创建临时文件";
    return false;
  }
  DWORD written = 0;
  const bool ok = (content.empty() || WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr)) && written == content.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!ok) {
    DeleteFileW(temp.c_str());
    if (error)
      *error = L"写临时文件失败";
    return false;
  }
  // 游戏里的运行库可能正好共享打开着这个文件读：替换可能和它竞争，退避重试几次
  for (int attempt = 1; attempt <= kMoveAttempts; ++attempt) {
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return true;
    if (attempt < kMoveAttempts)
      Sleep(30 * attempt);
  }
  DeleteFileW(temp.c_str());
  if (error)
    *error = L"替换 endfield-ui.ini 失败";
  return false;
}

}  // namespace

bool IsSafeIniToken(const std::string &key, const std::string &value) {
  if (key.empty() || std::any_of(key.begin(), key.end(), [](char c) { return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_'); }))
    return false;
  return std::none_of(value.begin(), value.end(), [](char c) {
    return !(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == ',' || c == '-' || c == '_');
  });
}

std::string MergeIni(const std::string &existing, const IniValues &values) {
  const std::string newline = existing.empty() || existing.find("\r\n") != std::string::npos ? "\r\n" : "\n";
  std::vector<std::string> lines = existing.empty() ? std::vector<std::string>() : SplitLines(existing);
  if (!lines.empty() && lines.back().empty())
    lines.pop_back();

  IniValues pending = values;
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string key = KeyOf(lines[i]);
    if (key.empty())
      continue;
    const auto hit = std::find_if(pending.begin(), pending.end(), [&](const auto &p) { return IEquals(p.first, key); });
    if (hit != pending.end()) {
      lines[i] = hit->first + "=" + hit->second;
      pending.erase(hit);
      continue;
    }
    // 已设置过的键的第二行：运行库读的是后一行，所以只能留一行
    if (std::any_of(values.begin(), values.end(), [&](const auto &v) { return IEquals(v.first, key); })) {
      lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
      --i;
    }
  }

  if (!pending.empty()) {
    // 运行库只读文件前 2047 字节：缺的键放最前（开头的注释块/空行之后），绝不放末尾
    size_t at = 0;
    while (at < lines.size() && (Trim(lines[at]).empty() || IsComment(lines[at])))
      ++at;
    std::vector<std::string> inserted;
    for (const auto &p : pending)
      inserted.push_back(p.first + "=" + p.second);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), inserted.begin(), inserted.end());
  }

  if (lines.empty())
    return {};
  std::string result;
  for (const auto &line : lines)
    result += line + newline;
  return result;
}

IniResult ApplyIni(const std::wstring &path, const IniValues &values, std::wstring *error) {
  for (const auto &p : values) {
    if (!IsSafeIniToken(p.first, p.second)) {
      if (error)
        *error = L"ini 键值含不允许的字符";
      return IniResult::failed;
    }
  }
  std::string existing;
  if (!ReadFileText(path, existing)) {
    if (error)
      *error = L"无法读取 endfield-ui.ini";
    return IniResult::failed;
  }
  const std::string updated = MergeIni(existing, values);
  if (updated == existing)
    return IniResult::unchanged;
  return WriteAtomic(path, updated, error) ? IniResult::written : IniResult::failed;
}

}  // namespace endfield_plugin
