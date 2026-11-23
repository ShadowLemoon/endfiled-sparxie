// ABI 合规测试：真正加载构建出来的 module.dll，按 Sparxie 宿主的方式调用它。退出码 0 = 全部通过。
// 覆盖：唯一导出、版本协商、get_info 与 plugin.json 一致、size 规则、错误信息内联缓冲、运行库的解压/校验/锁定、设置写进 ini、release 幂等、
// 以及"没有厂商签名的游戏一定被安全拒绝"（生产路径不允许跳过签名检查）。
#include <thread>

#include "endfield_test_support.h"
#include "sparxie_game_module.h"

using namespace endfield_test;

namespace {

using QueryFn = SpxError(__cdecl *)(uint32_t, uint32_t, const SpxModuleApi **);

fs::path ExeDir() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return fs::path(path).parent_path();
}

struct Loaded {
  HMODULE module = nullptr;
  QueryFn query = nullptr;
  const SpxModuleApi *api = nullptr;
};

Loaded LoadModuleOnce() {
  static Loaded loaded = [] {
    Loaded l;
    l.module = LoadLibraryExW((ExeDir() / L"module.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (l.module) {
      l.query = reinterpret_cast<QueryFn>(GetProcAddress(l.module, "sparxie_module_query"));
      if (l.query)
        l.query(SPX_MODULE_ABI_MAJOR, SPX_MODULE_ABI_MINOR, &l.api);
    }
    return l;
  }();
  return loaded;
}

SpxStr Str(const std::wstring &text) { return {text.c_str(), static_cast<uint32_t>(text.size())}; }

SpxResult NewResult() {
  SpxResult r{};
  r.size = sizeof(r);
  return r;
}

std::string ReadAll(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

struct Fixture {
  TempDir dir;
  fs::path game_dir = dir.path / L"game";
  fs::path data_dir = dir.path / L"data";
  fs::path log_dir = dir.path / L"logs";
  fs::path exe = game_dir / L"Endfield.exe";
  std::wstring exe_text, data_text, log_text, settings;
  SpxLaunchRequest request{};

  explicit Fixture(const std::wstring &settings_json = L"{}") : settings(settings_json) {
    fs::create_directories(game_dir);
    exe_text = exe.wstring();
    data_text = data_dir.wstring();
    log_text = log_dir.wstring();
    Rebuild();
  }

  void Rebuild() {
    request = {};
    request.size = sizeof(request);
    request.exe_path = Str(exe_text);
    request.settings_json = Str(settings);
    request.log_dir = Str(log_text);
    request.data_dir = Str(data_text);
  }
};

// ---- 导出与版本 ----

void TestOnlyOneExport() {
  const Loaded loaded = LoadModuleOnce();
  CHECK(loaded.module != nullptr);
  const auto *base = reinterpret_cast<const unsigned char *>(loaded.module);
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
  const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
  const auto &directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  CHECK(directory.VirtualAddress != 0);
  const auto *exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY *>(base + directory.VirtualAddress);
  CHECK(exports->NumberOfFunctions == 1);
  CHECK(exports->NumberOfNames == 1);
  const auto *names = reinterpret_cast<const DWORD *>(base + exports->AddressOfNames);
  CHECK(std::string(reinterpret_cast<const char *>(base + names[0])) == "sparxie_module_query");
}

void TestVersionNegotiation() {
  const Loaded loaded = LoadModuleOnce();
  CHECK(loaded.query != nullptr && loaded.api != nullptr);
  const SpxModuleApi *api = nullptr;
  CHECK(loaded.query(SPX_MODULE_ABI_MAJOR + 1, 0, &api) == SPX_ERR_ABI_MISMATCH);
  CHECK(api == nullptr);
  CHECK(loaded.query(SPX_MODULE_ABI_MAJOR, 99, &api) == SPX_OK);  // 次版本只追加：宿主更新也认
  CHECK(api != nullptr && api->abi_major == SPX_MODULE_ABI_MAJOR);
  CHECK(loaded.query(SPX_MODULE_ABI_MAJOR, 0, nullptr) == SPX_ERR_INVALID_ARGUMENT);
  CHECK(loaded.api->size >= sizeof(SpxModuleApi));
  CHECK(loaded.api->get_info && loaded.api->create_session && loaded.api->prepare_launch && loaded.api->attach_job && loaded.api->install &&
        loaded.api->post_install && loaded.api->wait_exit && loaded.api->set_param && loaded.api->abort && loaded.api->release);
}

void TestInfoMatchesManifest() {
  const Loaded loaded = LoadModuleOnce();
  const SpxModuleInfo *info = nullptr;
  CHECK(loaded.api->get_info(&info) == SPX_OK && info != nullptr);
  CHECK(info->size >= sizeof(SpxModuleInfo));
  const std::wstring id(info->id.p, info->id.chars);
  CHECK(id == L"ext.skyhua0224.endfield");
  CHECK(info->exe_count == 1 && info->exe_names != nullptr);
  CHECK(std::wstring(info->exe_names[0].p, info->exe_names[0].chars) == L"Endfield.exe");
  CHECK(info->caps == (SPX_CAP_CREATES_PROCESS | SPX_CAP_INJECTS | SPX_CAP_HOT_PARAMS));

  // plugin.json（构建时复制到可执行文件旁边）必须和 get_info 一致，宿主加载时也会交叉核对
  const std::string manifest = ReadAll(ExeDir() / L"plugin.json");
  CHECK(!manifest.empty());
  CHECK(manifest.find("\"id\": \"ext.skyhua0224.endfield\"") != std::string::npos);
  CHECK(manifest.find("\"exeNames\": [\"Endfield.exe\"]") != std::string::npos);
  CHECK(manifest.find("\"createsProcess\"") != std::string::npos);
  CHECK(manifest.find("\"injects\"") != std::string::npos);
  CHECK(manifest.find("\"hotParams\"") != std::string::npos);
  CHECK(manifest.find("\"needsArgs\"") == std::string::npos);
  CHECK(manifest.find("\"abiMajor\": 1") != std::string::npos);
  const std::wstring version(info->version.p, info->version.chars);
  std::string version_ascii;
  for (wchar_t ch : version)
    version_ascii.push_back(static_cast<char>(ch));  // 版本号只含 ASCII
  CHECK(manifest.find("\"version\": \"" + version_ascii + "\"") != std::string::npos);
}

// ---- 创建会话 ----

void TestCreateSessionRefusesBadRequests() {
  const Loaded loaded = LoadModuleOnce();
  void *session = reinterpret_cast<void *>(1);
  SpxResult result = NewResult();
  CHECK(loaded.api->create_session(nullptr, &result, &session) == SPX_ERR_INVALID_ARGUMENT);

  Fixture f;
  f.request.size = offsetof(SpxLaunchRequest, host) - 1;  // 小于模块需要读的字段
  session = nullptr;
  result = NewResult();
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_ERR_INVALID_ARGUMENT);
  CHECK(session == nullptr);

  Fixture no_exe;
  no_exe.exe_text.clear();
  no_exe.Rebuild();
  result = NewResult();
  CHECK(loaded.api->create_session(&no_exe.request, &result, &session) == SPX_ERR_INVALID_ARGUMENT);

  // 键鼠模式又不解锁帧率：没有可做的事
  Fixture nothing_to_do(L"{\"inputMode\":\"pc\",\"fpsUnlock\":false}");
  result = NewResult();
  session = nullptr;
  CHECK(loaded.api->create_session(&nothing_to_do.request, &result, &session) == SPX_ERR_INVALID_ARGUMENT);
  CHECK(session == nullptr);
  CHECK(result.error_code == SPX_ERR_INVALID_ARGUMENT);
  CHECK(result.message[0] != 0);
  CHECK(wcsnlen(result.message, 512) < 512);  // 以 NUL 结尾，信息在宿主给的内联缓冲里
}

void TestResultSmallerThanTheBufferIsNotWrittenBeyondItsSize() {
  const Loaded loaded = LoadModuleOnce();
  Fixture f(L"{\"inputMode\":\"pc\",\"fpsUnlock\":false}");
  SpxResult result = NewResult();
  for (auto &ch : result.message)
    ch = 0x5A5A;
  result.size = offsetof(SpxResult, message);  // 宿主只给了固定头部
  void *session = nullptr;
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_ERR_INVALID_ARGUMENT);
  CHECK(result.message[0] == 0x5A5A);  // 缓冲没被动
}

// ---- 运行库与设置 ----

void TestRuntimeIsExtractedVerifiedAndLocked() {
  const Loaded loaded = LoadModuleOnce();
  Fixture f;
  void *session = nullptr;
  SpxResult result = NewResult();
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_OK);
  CHECK(session != nullptr);

  fs::path extracted;
  for (const auto &entry : fs::recursive_directory_iterator(f.data_dir))
    if (entry.path().filename() == L"EndfieldRuntime.dll")
      extracted = entry.path();
  CHECK(!extracted.empty());
  CHECK(extracted.parent_path().parent_path().filename() == L"runtime");  // 目录形如 data_dir/runtime/<哈希前缀>
  CHECK(extracted.parent_path().filename().wstring().size() == 12);
  CHECK(ReadAll(extracted) == ReadAll(ExeDir() / L"EndfieldRuntime.dll"));  // 嵌入的字节就是构建出的运行库

  // 会话期间运行库被持有（拒绝写入/删除）
  HANDLE writer = CreateFileW(extracted.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(writer == INVALID_HANDLE_VALUE);
  if (writer != INVALID_HANDLE_VALUE)
    CloseHandle(writer);
  CHECK(!DeleteFileW(extracted.c_str()));

  CHECK(loaded.api->release(session) == SPX_OK);
  // 释放后锁就没了
  writer = CreateFileW(extracted.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(writer != INVALID_HANDLE_VALUE);
  if (writer != INVALID_HANDLE_VALUE)
    CloseHandle(writer);
}

void TestATamperedRuntimeFileIsReplaced() {
  const Loaded loaded = LoadModuleOnce();
  Fixture f;
  void *session = nullptr;
  SpxResult result = NewResult();
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_OK);
  fs::path extracted;
  for (const auto &entry : fs::recursive_directory_iterator(f.data_dir))
    if (entry.path().filename() == L"EndfieldRuntime.dll")
      extracted = entry.path();
  CHECK(loaded.api->release(session) == SPX_OK);
  CHECK(!extracted.empty());

  {
    std::ofstream tamper(extracted, std::ios::binary | std::ios::trunc);
    tamper << "this is not the runtime";
  }
  session = nullptr;
  result = NewResult();
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_OK);
  CHECK(ReadAll(extracted) == ReadAll(ExeDir() / L"EndfieldRuntime.dll"));  // 被改过的文件按哈希发现并覆盖回正确的字节
  CHECK(loaded.api->release(session) == SPX_OK);
}

void TestSettingsAreWrittenToTheIniBesideTheRuntime() {
  const Loaded loaded = LoadModuleOnce();
  Fixture f(L"{\"uiScaleMode\":\"manual\",\"uiScalePercent\":133,\"safeAreaMode\":\"manual\",\"safeLeft\":186,\"safeRight\":9999}");
  void *session = nullptr;
  SpxResult result = NewResult();
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_OK);
  fs::path ini;
  for (const auto &entry : fs::recursive_directory_iterator(f.data_dir))
    if (entry.path().filename() == L"endfield-ui.ini")
      ini = entry.path();
  CHECK(!ini.empty());
  CHECK(ReadAll(ini) == "ui_scale=1.35\r\nsafe_area=186,600\r\n");
  CHECK(ini.parent_path() == [&] {
    for (const auto &entry : fs::recursive_directory_iterator(f.data_dir))
      if (entry.path().filename() == L"EndfieldRuntime.dll")
        return entry.path().parent_path();
    return fs::path();
  }());  // 运行库从自己所在目录读 ini
  CHECK(loaded.api->release(session) == SPX_OK);
}

void TestDefaultSettingsWriteAutoAuto() {
  const Loaded loaded = LoadModuleOnce();
  Fixture f(L"");  // 宿主传了空字符串
  void *session = nullptr;
  SpxResult result = NewResult();
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_OK);
  for (const auto &entry : fs::recursive_directory_iterator(f.data_dir))
    if (entry.path().filename() == L"endfield-ui.ini")
      CHECK(ReadAll(entry.path()) == "ui_scale=auto\r\nsafe_area=auto\r\n");
  CHECK(loaded.api->release(session) == SPX_OK);
}

// ---- 会话流程 ----

void TestSessionGuardsBeforeLaunch() {
  const Loaded loaded = LoadModuleOnce();
  Fixture f;
  void *session = nullptr;
  SpxResult result = NewResult();
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_OK);

  CHECK(loaded.api->post_install(session, &result) == SPX_OK);
  CHECK(loaded.api->abort(session, &result) == SPX_OK);
  // 没启动：没什么可等，热调也没有共享状态
  CHECK(loaded.api->wait_exit(session, 10, &result) == SPX_ERR_INVALID_ARGUMENT);
  CHECK(loaded.api->set_param(session, Str(L"targetFps"), 144, &result) == SPX_ERR_INVALID_ARGUMENT);
  CHECK(loaded.api->set_param(session, Str(L"targetFps"), 9, &result) == SPX_ERR_INVALID_ARGUMENT);   // 越界
  CHECK(loaded.api->set_param(session, Str(L"somethingElse"), 1, &result) == SPX_ERR_NOT_SUPPORTED);  // 未知参数
  // 宿主不会传 game_pid：本模块自己创建进程
  CHECK(loaded.api->install(session, 1234, &result) == SPX_ERR_INVALID_ARGUMENT);
  CHECK(loaded.api->release(session) == SPX_OK);
}

void TestHotFpsIsRefusedWhenUnlockIsOff() {
  const Loaded loaded = LoadModuleOnce();
  Fixture f(L"{\"fpsUnlock\":false}");
  void *session = nullptr;
  SpxResult result = NewResult();
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_OK);
  CHECK(loaded.api->set_param(session, Str(L"targetFps"), 144, &result) == SPX_ERR_NOT_SUPPORTED);
  CHECK(loaded.api->release(session) == SPX_OK);
}

void TestReleaseIsIdempotent() {
  const Loaded loaded = LoadModuleOnce();
  Fixture f;
  void *session = nullptr;
  SpxResult result = NewResult();
  CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_OK);
  CHECK(loaded.api->release(session) == SPX_OK);
  CHECK(loaded.api->release(session) == SPX_OK);                        // 第二次什么都不做
  CHECK(loaded.api->release(nullptr) == SPX_OK);
  CHECK(loaded.api->release(reinterpret_cast<void *>(0x1234)) == SPX_OK);  // 不认识的句柄也不崩
  // 释放后再用：被拒绝而不是崩
  CHECK(loaded.api->install(session, 0, &result) == SPX_ERR_INVALID_ARGUMENT);
}

void TestAGameWithoutAValidVendorSignatureIsRefusedSafely() {
  // 生产路径永远不跳过签名检查：一个没有厂商签名的 Endfield.exe（这里是假游戏）必须被拒绝，不能被启动。
  const Loaded loaded = LoadModuleOnce();
  Fixture f;
  fs::copy_file(ExeDir() / L"FakeEndfield.exe", f.exe);
  fs::copy_file(ExeDir() / L"FakeEndfield.exe", f.game_dir / L"PlatformProcess.exe");

  // 宿主的方式：整个会话在同一个专用线程上
  SpxError install_rc = SPX_OK;
  SpxResult install_result = NewResult();
  std::thread host([&] {
    void *session = nullptr;
    SpxResult result = NewResult();
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    CHECK(loaded.api->create_session(&f.request, &result, &session) == SPX_OK);
    CHECK(loaded.api->prepare_launch(session, &result) == SPX_OK);
    CHECK(loaded.api->attach_job(session, job, &result) == SPX_OK);
    install_rc = loaded.api->install(session, 0, &install_result);
    CHECK(loaded.api->abort(session, &result) == SPX_OK);
    CHECK(loaded.api->release(session) == SPX_OK);
    CloseHandle(job);
  });
  host.join();

  CHECK(install_rc != SPX_OK);
  CHECK(install_result.error_code == install_rc);
  CHECK(install_result.stage == ENDFIELD_STAGE_DECOY);  // 失败在假父进程这一步（签名校验），还没有任何进程被创建
  CHECK(install_result.message[0] != 0);
  // 没有留下暂存的假父进程目录
  int staged = 0;
  for (const auto &entry : fs::directory_iterator(f.game_dir))
    if (entry.is_directory())
      ++staged;
  CHECK(staged == 0);
}

}  // namespace

int wmain() {
  Run("abi: module.dll exports exactly sparxie_module_query", TestOnlyOneExport);
  Run("abi: version negotiation", TestVersionNegotiation);
  Run("abi: get_info matches plugin.json", TestInfoMatchesManifest);
  Run("abi: create_session refuses bad requests with an inline message", TestCreateSessionRefusesBadRequests);
  Run("abi: a result smaller than the buffer is not written beyond its size", TestResultSmallerThanTheBufferIsNotWrittenBeyondItsSize);
  Run("runtime: extracted to data_dir, verified, and locked while the session lives", TestRuntimeIsExtractedVerifiedAndLocked);
  Run("runtime: a tampered extracted file is replaced", TestATamperedRuntimeFileIsReplaced);
  Run("settings: written to the ini beside the runtime", TestSettingsAreWrittenToTheIniBesideTheRuntime);
  Run("settings: empty settings write auto/auto", TestDefaultSettingsWriteAutoAuto);
  Run("session: guards before launch", TestSessionGuardsBeforeLaunch);
  Run("session: hot fps is refused when the unlock is off", TestHotFpsIsRefusedWhenUnlockIsOff);
  Run("session: release is idempotent", TestReleaseIsIdempotent);
  Run("session: a game without a valid vendor signature is refused safely", TestAGameWithoutAValidVendorSignatureIsRefusedSafely);
  std::printf("%d tests, %d failed checks\n", g_run, g_failed);
  return g_failed ? 1 : 0;
}
