#include "spx_module.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "endfield_core_abi.h"
#include "endfield_decoy.h"
#include "runtime_extract.h"
#include "settings.h"
#include "ui_ini.h"

namespace endfield_plugin {

namespace {

// 单次 ABI 调用里 install 最坏耗时约 20s（等游戏 loader）+ 5×10s（注入重试），都在宿主 120s 上限内。

struct Session {
  void *core = nullptr;  // endfield_core 的会话句柄
  ModuleSettings settings;
  std::wstring exe_path;
  std::wstring log_file;
  std::wstring ini_path;
  ExtractedRuntime runtime;
  bool launched = false;
};

std::mutex g_sessions_lock;
std::set<Session *> g_sessions;  // 活着的会话：release 幂等（重复 release 同一个指针不会二次释放）

// ---- 结果填充 ----

void Fill(SpxResult *result, int error, uint32_t stage, uint32_t detail, const wchar_t *message) {
  if (!result)
    return;
  const uint32_t size = result->size;
  result->error_code = error;
  // 只回写 result->size 覆盖到的范围（宿主约定）；缓冲固定 512 个 UTF-16 字符
  if (size >= offsetof(SpxResult, message) + sizeof(result->message)) {
    result->stage = stage;
    result->detail = detail;
    wcsncpy_s(result->message, 512, message ? message : L"", _TRUNCATE);
  }
}

SpxError Ok(SpxResult *result) {
  Fill(result, SPX_OK, 0, 0, L"");
  return SPX_OK;
}

SpxError Fail(SpxResult *result, SpxError code, uint32_t stage, const wchar_t *message, uint32_t detail = 0) {
  Fill(result, code, stage, detail ? detail : GetLastError(), message);
  return code;
}

SpxError FromCore(EndfieldError error, const EndfieldResult &core, SpxResult *result) {
  SpxError code = SPX_ERR_INTERNAL;
  switch (error) {
  case ENDFIELD_OK: code = SPX_OK; break;
  case ENDFIELD_ERR_INVALID_ARGUMENT:
  case ENDFIELD_ERR_CONFIG_INVALID:
  case ENDFIELD_ERR_SESSION_NOT_ACTIVE: code = SPX_ERR_INVALID_ARGUMENT; break;
  case ENDFIELD_ERR_ABI_MISMATCH: code = SPX_ERR_ABI_MISMATCH; break;
  case ENDFIELD_ERR_PROCESS_NOT_FOUND: code = SPX_ERR_PROCESS_NOT_FOUND; break;
  case ENDFIELD_ERR_NOT_SUPPORTED: code = SPX_ERR_NOT_SUPPORTED; break;
  case ENDFIELD_ERR_SCAN_FAILED:
  case ENDFIELD_ERR_PATCH_FAILED: code = SPX_ERR_INJECT_FAILED; break;
  case ENDFIELD_ERR_DECOY_FAILED:
  case ENDFIELD_ERR_INTERNAL:
  default: code = SPX_ERR_INTERNAL; break;
  }
  Fill(result, code, core.stage, core.detail, core.message ? core.message : L"");
  return code;
}

EndfieldResult NewCoreResult() {
  EndfieldResult result{};
  result.size = sizeof(result);
  return result;
}

Session *Find(void *handle) {
  std::lock_guard<std::mutex> guard(g_sessions_lock);
  auto *session = static_cast<Session *>(handle);
  return g_sessions.count(session) ? session : nullptr;
}

std::wstring ToWide(const SpxStr &text) {
  return text.p && text.chars ? std::wstring(text.p, text.chars) : std::wstring();
}

// 一行写进 log_dir 里和运行库共用的日志文件（和核心里 CoreLog 同格式的简单版）
void LogLine(const std::wstring &file, const char *format, ...) {
  if (file.empty())
    return;
  char text[500];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text) - 2, format, args);
  va_end(args);
  SYSTEMTIME now;
  GetLocalTime(&now);
  char line[600];
  const int n = snprintf(line, sizeof(line), "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [plugin] %s\n", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                         now.wMilliseconds, text);
  HANDLE log = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log == INVALID_HANDLE_VALUE)
    return;
  DWORD written = 0;
  WriteFile(log, line, static_cast<DWORD>(n), &written, nullptr);
  CloseHandle(log);
}

// ---- 模块信息 ----

const SpxStr kExeNames[] = {{L"Endfield.exe", 12}};
SpxModuleInfo g_info;
SpxModuleApi g_api;

SpxError GetInfo(const SpxModuleInfo **out) {
  if (!out)
    return SPX_ERR_INVALID_ARGUMENT;
  *out = &g_info;
  return SPX_OK;
}

// ---- 会话 ----

SpxError CreateSession(const SpxLaunchRequest *request, SpxResult *result, void **out) {
  if (!request || !out || request->size < offsetof(SpxLaunchRequest, host))
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"启动请求缺失或过小");
  *out = nullptr;

  auto session = std::make_unique<Session>();
  session->settings = ParseSettings(request->settings_json.p ? std::wstring_view(request->settings_json.p, request->settings_json.chars) : std::wstring_view());
  if (request->size >= offsetof(SpxLaunchRequest, disable_touch_ui) + sizeof(request->disable_touch_ui) && request->disable_touch_ui != 0)
    session->settings.input_mode = "pc";
  const ModuleSettings &s = session->settings;
  session->exe_path = ToWide(request->exe_path);
  const std::wstring data_dir = ToWide(request->data_dir);
  const std::wstring log_dir = ToWide(request->log_dir);
  if (session->exe_path.empty() || data_dir.empty())
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"缺少游戏 EXE 路径或数据目录");
  if (!s.fps_unlock && s.input_mode == "pc")
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"输入模式为键鼠且未启用帧率解锁时没有可做的事");

  if (!log_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    session->log_file = (std::filesystem::path(log_dir) / L"endfield-runtime.log").wstring();
  }

  std::wstring error;
  if (!session->runtime.Extract(data_dir, &error))
    return Fail(result, SPX_ERR_INTERNAL, 4, error.c_str());

  // 运行库每隔几秒重读它自己目录里的 endfield-ui.ini；启动前写一次（第一版没有运行中热更新，见 README）
  session->ini_path = (std::filesystem::path(session->runtime.directory()) / kIniFileName).wstring();
  const IniValues values = {{"ui_scale", ScaleValue(s.ui_scale_mode, s.ui_scale_percent)}, {"safe_area", SafeAreaValue(s.safe_area_mode, s.safe_left, s.safe_right)}};
  if (ApplyIni(session->ini_path, values, &error) == IniResult::failed)
    return Fail(result, SPX_ERR_INTERNAL, 4, error.c_str());

  const std::wstring runtime_dll = session->runtime.dll_path();
  const std::wstring log_file = session->log_file;
  EndfieldLaunchRequest core_request{};
  core_request.size = sizeof(core_request);
  core_request.abi_version = ENDFIELD_ABI_VERSION;
  core_request.fps_unlock_enabled = s.fps_unlock ? 1 : 0;
  core_request.target_fps = s.target_fps;
  core_request.touch_ui_enabled = s.input_mode == "pc" ? 0 : 1;
  int width = s.width, height = s.height;
  if (s.auto_resolution) {
    // 关闭"固定分辨率"= 用主显示器分辨率（宽高取不到时退回设置里的值）
    const int screen_w = GetSystemMetrics(SM_CXSCREEN), screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w >= kMinWidth && screen_h >= kMinHeight) {
      width = screen_w;
      height = screen_h;
    }
  }
  core_request.width = width;
  core_request.height = height;
  core_request.refresh_rate = s.refresh_rate;
  core_request.game_executable_path = session->exe_path.c_str();
  core_request.game_executable_path_chars = static_cast<uint32_t>(session->exe_path.size());
  core_request.flags = 0;  // 测试开关只在仓库内的原生测试里使用，生产路径永远是 0
  core_request.parent_source_path = nullptr;
  core_request.parent_source_path_chars = 0;
  core_request.runtime_path = runtime_dll.c_str();
  core_request.runtime_path_chars = static_cast<uint32_t>(runtime_dll.size());
  core_request.log_file = log_file.empty() ? nullptr : log_file.c_str();
  core_request.log_file_chars = static_cast<uint32_t>(log_file.size());

  EndfieldResult core_result = NewCoreResult();
  const EndfieldError rc = endfield_create_session(&core_request, &core_result, &session->core);
  if (rc != ENDFIELD_OK)
    return FromCore(rc, core_result, result);

  Session *raw = session.release();
  {
    std::lock_guard<std::mutex> guard(g_sessions_lock);
    g_sessions.insert(raw);
  }
  *out = raw;
  return Ok(result);
}

SpxError PrepareLaunch(void *handle, SpxResult *result) {
  Session *session = Find(handle);
  if (!session)
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"会话无效");
  // 清掉以前崩溃的会话留在游戏目录里的假父进程暂存目录（属主进程已不存在的）
  std::error_code ec;
  const std::filesystem::path game(session->exe_path);
  if (game.has_parent_path())
    endfield::SweepStale(game.parent_path());
  return Ok(result);
}

SpxError AttachJob(void *handle, void *job, SpxResult *result) {
  Session *session = Find(handle);
  if (!session)
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"会话无效");
  EndfieldResult core_result = NewCoreResult();
  return FromCore(endfield_attach_job(session->core, job, &core_result), core_result, result);
}

SpxError Install(void *handle, uint32_t game_pid, SpxResult *result) {
  Session *session = Find(handle);
  if (!session)
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"会话无效");
  if (game_pid != 0)
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"该模块自己创建游戏进程，宿主不应传入 game_pid");

  EndfieldResult core_result = NewCoreResult();
  const EndfieldError rc = endfield_launch(session->core, &core_result);
  if (rc != ENDFIELD_OK)
    return FromCore(rc, core_result, result);
  session->launched = true;

  EndfieldSessionInfo info{};
  info.size = sizeof(info);
  if (endfield_get_session_info(session->core, &info) == ENDFIELD_OK)
    LogLine(session->log_file, "launched: game pid %lu, decoy pid %lu, parent pid seen by the OS %lu", info.game_pid, info.decoy_pid, info.parent_pid_seen);
  return FromCore(rc, core_result, result);
}

SpxError PostInstall(void *handle, SpxResult *result) {
  if (!Find(handle))
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"会话无效");
  return Ok(result);
}

SpxError WaitExit(void *handle, uint32_t timeout_ms, SpxResult *result) {
  Session *session = Find(handle);
  if (!session)
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"会话无效");
  EndfieldResult core_result = NewCoreResult();
  const EndfieldError rc = endfield_wait_game_exit(session->core, timeout_ms, &core_result);
  // 核心用 ENDFIELD_ERR_NOT_SUPPORTED + "等待超时" 表示本轮没等到退出；宿主约定是 SPX_ERR_TIMEOUT
  if (rc == ENDFIELD_ERR_NOT_SUPPORTED && core_result.stage == ENDFIELD_STAGE_WAIT)
{
    Fill(result, SPX_ERR_TIMEOUT, ENDFIELD_STAGE_WAIT, 0, L"等待超时");  // 超时不是出错，detail 不带陈旧的 GetLastError
    return SPX_ERR_TIMEOUT;
  }
  return FromCore(rc, core_result, result);
}

SpxError SetParam(void *handle, SpxStr key, int64_t value, SpxResult *result) {
  Session *session = Find(handle);
  if (!session)
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"会话无效");
  if (ToWide(key) != L"targetFps")
    return Fail(result, SPX_ERR_NOT_SUPPORTED, 1, L"不支持的热调参数");
  if (!session->settings.fps_unlock)
    return Fail(result, SPX_ERR_NOT_SUPPORTED, 1, L"帧率解锁未启用，不能热调目标帧率");
  if (value < kMinFps || value > kMaxFps)
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"目标帧率超出 10-1000");
  EndfieldResult core_result = NewCoreResult();
  return FromCore(endfield_set_target_fps(session->core, static_cast<int32_t>(value), &core_result), core_result, result);
}

SpxError Abort(void *handle, SpxResult *result) {
  // 启动失败时核心已经自己结束了游戏和假父进程；Running 前宿主的 Job 兜底。这里不额外杀进程，清理交给 release。
  if (!Find(handle))
    return Fail(result, SPX_ERR_INVALID_ARGUMENT, 1, L"会话无效");
  return Ok(result);
}

SpxError Release(void *handle) {
  Session *session = nullptr;
  {
    std::lock_guard<std::mutex> guard(g_sessions_lock);
    auto *candidate = static_cast<Session *>(handle);
    if (!g_sessions.erase(candidate))
      return SPX_OK;  // 幂等：不认识的/已经释放的句柄什么都不做
    session = candidate;
  }
  if (session->core)
    endfield_release(session->core);
  session->runtime.Close();
  delete session;
  return SPX_OK;
}

void InitTables() {
  static bool done = false;
  if (done)
    return;
  done = true;
  g_info = {};
  g_info.size = sizeof(g_info);
  g_info.abi_major = SPX_MODULE_ABI_MAJOR;
  g_info.abi_minor = SPX_MODULE_ABI_MINOR;
  g_info.id = {kPluginId, static_cast<uint32_t>(wcslen(kPluginId))};
  g_info.display_name = {L"明日方舟：终末地", 8};
  g_info.version = {kPluginVersion, static_cast<uint32_t>(wcslen(kPluginVersion))};
  g_info.caps = SPX_CAP_CREATES_PROCESS | SPX_CAP_INJECTS | SPX_CAP_HOT_PARAMS | SPX_CAP_DISABLE_TOUCH_UI;
  g_info.exe_names = kExeNames;
  g_info.exe_count = 1;

  g_api = {};
  g_api.size = sizeof(g_api);
  g_api.abi_major = SPX_MODULE_ABI_MAJOR;
  g_api.abi_minor = SPX_MODULE_ABI_MINOR;
  g_api.get_info = GetInfo;
  g_api.create_session = CreateSession;
  g_api.prepare_launch = PrepareLaunch;
  g_api.attach_job = AttachJob;
  g_api.install = Install;
  g_api.post_install = PostInstall;
  g_api.wait_exit = WaitExit;
  g_api.set_param = SetParam;
  g_api.abort = Abort;
  g_api.release = Release;
}

}  // namespace

const SpxModuleApi *GetModuleApi() {
  InitTables();
  return &g_api;
}

}  // namespace endfield_plugin
