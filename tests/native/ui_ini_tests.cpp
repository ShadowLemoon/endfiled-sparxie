// endfield-ui.ini 读-改-写的测试（原 C# EndfieldUiIniTests 的原生版）。退出码 0 = 全部通过。
#include <atomic>
#include <thread>

#include "endfield_test_support.h"
#include "ui_ini.h"

using namespace endfield_test;
using namespace endfield_plugin;

namespace {

IniValues Values(const char *scale, const char *safe) { return {{"ui_scale", scale}, {"safe_area", safe}}; }

std::string ReadAll(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void WriteAll(const fs::path &path, const std::string &text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

// ---- 文本（纯函数）----

void TestEmptyFileGetsTwoLinesWithCrlf() { CHECK(MergeIni("", Values("auto", "auto")) == "ui_scale=auto\r\nsafe_area=auto\r\n"); }

void TestOtherKeysCommentsAndOrderSurvive() {
  const std::string existing = "# set by hand\r\nscale=1.25\r\n\r\nsafe_left=186\r\nsafe_right=186\r\nui_scale=auto\r\n; keep me\r\nobserve=1\r\n";
  CHECK(MergeIni(existing, Values("1.40", "0,60")) ==
        "# set by hand\r\nsafe_area=0,60\r\nscale=1.25\r\n\r\nsafe_left=186\r\nsafe_right=186\r\nui_scale=1.40\r\n; keep me\r\nobserve=1\r\n");
}

void TestKeysAreCaseInsensitiveAndDuplicatesCollapse() {
  const std::string existing = "UI_SCALE=1.10\nmode=helper\nui_scale = 1.70\nsafe_area=off\n";
  CHECK(MergeIni(existing, Values("auto", "auto")) == "ui_scale=auto\nmode=helper\nsafe_area=auto\n");
}

void TestMissingKeysGoToTheTopNotTheEnd() {
  // 运行库只读文件前 2047 字节
  std::string filler;
  for (int i = 0; i < 120; ++i)
    filler += "extra_key_" + std::to_string(i) + "=value_" + std::to_string(i) + "\n";
  CHECK(filler.size() > 2047);
  const std::string text = MergeIni(filler, Values("1.50", "off"));
  CHECK(text.rfind("ui_scale=1.50\nsafe_area=off\nextra_key_0=value_0\n", 0) == 0);
  CHECK(text.find("safe_area=off") < 2047);
  CHECK(text.size() > filler.size() && text.compare(text.size() - filler.size(), filler.size(), filler) == 0);
}

void TestInsertAfterTheLeadingCommentBlock() {
  CHECK(MergeIni("# a\n# b\nobserve=1\n", Values("auto", "auto")) == "# a\n# b\nui_scale=auto\nsafe_area=auto\nobserve=1\n");
}

void TestMergeIsIdempotent() {
  const std::string once = MergeIni("observe=1\r\nscale=1.25\r\n", Values("1.35", "12,34"));
  CHECK(MergeIni(once, Values("1.35", "12,34")) == once);
}

void TestNoTrailingNewlineKeepsTheLastLine() { CHECK(MergeIni("observe=1", Values("auto", "auto")) == "ui_scale=auto\nsafe_area=auto\nobserve=1\n"); }

void TestOnlySafeTokensAreAllowed() {
  CHECK(IsSafeIniToken("ui_scale", "1.40"));
  CHECK(IsSafeIniToken("safe_area", "186,60"));
  CHECK(IsSafeIniToken("ui_scale", "auto"));
  CHECK(!IsSafeIniToken("", "x"));
  CHECK(!IsSafeIniToken("ui scale", "x"));
  CHECK(!IsSafeIniToken("a=b", "x"));
  CHECK(!IsSafeIniToken("ui_scale", "1\r\nobserve=0"));  // 换行 = 另一个设置
  CHECK(!IsSafeIniToken("ui_scale", "a b"));
  CHECK(!IsSafeIniToken("ui_scale", "x;y"));
}

// ---- 文件 ----

void TestWriteCreatesTheFileWithoutBomAndLeavesNoTemp() {
  TempDir dir;
  const fs::path path = dir.path / L"endfield-ui.ini";
  CHECK(ApplyIni(path.wstring(), Values("1.30", "auto")) == IniResult::written);
  const std::string bytes = ReadAll(path);
  CHECK(!(bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF));
  CHECK(bytes == "ui_scale=1.30\r\nsafe_area=auto\r\n");
  int temps = 0;
  for (const auto &entry : fs::directory_iterator(dir.path))
    if (entry.path().filename().wstring().find(L".tmp-") != std::wstring::npos)
      ++temps;
  CHECK(temps == 0);
}

void TestOldFileWithBomIsReadAndWrittenBackWithoutIt() {
  TempDir dir;
  const fs::path path = dir.path / L"endfield-ui.ini";
  WriteAll(path, std::string("\xEF\xBB\xBF") + "observe=1\r\n");
  CHECK(ApplyIni(path.wstring(), Values("auto", "auto")) == IniResult::written);
  const std::string bytes = ReadAll(path);
  CHECK(bytes[0] == 'u');
  CHECK(bytes.find("observe=1") != std::string::npos);
}

void TestUnchangedContentDoesNotTouchTheFile() {
  TempDir dir;
  const fs::path path = dir.path / L"endfield-ui.ini";
  CHECK(ApplyIni(path.wstring(), Values("1.30", "off")) == IniResult::written);
  const auto stamp = fs::file_time_type::clock::now() - std::chrono::hours(24 * 365);
  fs::last_write_time(path, stamp);
  CHECK(ApplyIni(path.wstring(), Values("1.30", "off")) == IniResult::unchanged);
  CHECK(fs::last_write_time(path) == stamp);
}

void TestUnsafeValuesAreRefusedAndNothingIsWritten() {
  TempDir dir;
  const fs::path path = dir.path / L"endfield-ui.ini";
  std::wstring error;
  CHECK(ApplyIni(path.wstring(), {{"ui_scale", "1\r\nevil=1"}}, &error) == IniResult::failed);
  CHECK(!error.empty());
  CHECK(!fs::exists(path));
}

void TestReplaceWorksWhileTheGameKeepsReadingTheFile() {
  // 运行库每隔几秒用 读/写/删 共享打开重读这个文件；这里让读者每毫秒来一次
  TempDir dir;
  const fs::path path = dir.path / L"endfield-ui.ini";
  CHECK(ApplyIni(path.wstring(), Values("auto", "auto")) == IniResult::written);
  std::atomic<bool> stop{false};
  std::thread reader([&] {
    while (!stop) {
      HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file != INVALID_HANDLE_VALUE) {
        char byte;
        DWORD read = 0;
        ReadFile(file, &byte, 1, &read, nullptr);
        CloseHandle(file);
      }
      Sleep(1);
    }
  });
  bool all_written = true;
  for (int i = 0; i < 20; ++i) {
    char scale[16];
    std::snprintf(scale, sizeof(scale), "1.%02d", 4 * i % 80);
    all_written = all_written && ApplyIni(path.wstring(), Values(scale, "100,100")) != IniResult::failed;
  }
  stop = true;
  reader.join();
  CHECK(all_written);
  CHECK(ReadAll(path).find("safe_area=100,100") != std::string::npos);
}

}  // namespace

int wmain() {
  Run("ini text: an empty file gets two lines with CRLF", TestEmptyFileGetsTwoLinesWithCrlf);
  Run("ini text: other keys, comments, blank lines and order survive", TestOtherKeysCommentsAndOrderSurvive);
  Run("ini text: keys are case-insensitive and duplicates collapse", TestKeysAreCaseInsensitiveAndDuplicatesCollapse);
  Run("ini text: missing keys go to the top, not the end", TestMissingKeysGoToTheTopNotTheEnd);
  Run("ini text: new keys go after the leading comment block", TestInsertAfterTheLeadingCommentBlock);
  Run("ini text: merging the same values again changes nothing", TestMergeIsIdempotent);
  Run("ini text: a file without a trailing newline keeps its last line", TestNoTrailingNewlineKeepsTheLastLine);
  Run("ini text: only safe tokens are allowed", TestOnlySafeTokensAreAllowed);
  Run("ini file: creates the file without a BOM and leaves no temp file", TestWriteCreatesTheFileWithoutBomAndLeavesNoTemp);
  Run("ini file: an old file with a BOM is read and written back without it", TestOldFileWithBomIsReadAndWrittenBackWithoutIt);
  Run("ini file: unchanged content does not touch the file", TestUnchangedContentDoesNotTouchTheFile);
  Run("ini file: unsafe values are refused and nothing is written", TestUnsafeValuesAreRefusedAndNothingIsWritten);
  Run("ini file: the replace works while the game keeps reading it", TestReplaceWorksWhileTheGameKeepsReadingTheFile);
  std::printf("%d tests, %d failed checks\n", g_run, g_failed);
  return g_failed ? 1 : 0;
}
