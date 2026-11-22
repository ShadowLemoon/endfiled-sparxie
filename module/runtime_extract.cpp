#include "runtime_extract.h"

#include <Windows.h>
#include <bcrypt.h>

#include <filesystem>
#include <vector>

namespace endfield_plugin {

namespace {

constexpr int kResourceId = 101;  // module.rc: 101 RCDATA "EndfieldRuntime.dll"

RuntimeBlob ResourceBlob() {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&ResourceBlob), &module))
    return {};
  HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(kResourceId), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
  if (!resource)
    return {};
  HGLOBAL loaded = LoadResource(module, resource);
  const DWORD size = SizeofResource(module, resource);
  const void *data = loaded ? LockResource(loaded) : nullptr;
  return data && size ? RuntimeBlob{data, size} : RuntimeBlob{};
}

RuntimeBlobProvider g_provider = &ResourceBlob;

bool ReadWhole(HANDLE file, std::vector<unsigned char> &out) {
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 64LL * 1024 * 1024)
    return false;
  out.resize(static_cast<size_t>(size.QuadPart));
  SetFilePointer(file, 0, nullptr, FILE_BEGIN);
  DWORD read = 0;
  return ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size();
}

}  // namespace

void SetRuntimeBlobProvider(RuntimeBlobProvider provider) { g_provider = provider ? provider : &ResourceBlob; }

RuntimeBlob GetRuntimeBlob() { return g_provider(); }

std::string Sha256Hex(const void *data, size_t size) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
    return {};
  unsigned char digest[32] = {};
  const NTSTATUS status = BCryptHash(algorithm, nullptr, 0, static_cast<PUCHAR>(const_cast<void *>(data)), static_cast<ULONG>(size), digest, sizeof(digest));
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (status != 0)
    return {};
  static const char hex[] = "0123456789abcdef";
  std::string text;
  for (unsigned char byte : digest) {
    text.push_back(hex[byte >> 4]);
    text.push_back(hex[byte & 15]);
  }
  return text;
}

void ExtractedRuntime::Close() {
  if (lock_ && lock_ != INVALID_HANDLE_VALUE)
    CloseHandle(lock_);
  lock_ = nullptr;
}

bool ExtractedRuntime::Extract(const std::wstring &data_dir, std::wstring *error) {
  Close();
  const RuntimeBlob blob = GetRuntimeBlob();
  if (!blob.data || blob.size == 0) {
    if (error)
      *error = L"插件里没有嵌入运行库（EndfieldRuntime.dll）";
    return false;
  }
  const std::string hash = Sha256Hex(blob.data, blob.size);
  if (hash.size() != 64) {
    if (error)
      *error = L"无法计算运行库哈希";
    return false;
  }

  const std::filesystem::path directory = std::filesystem::path(data_dir) / L"runtime" / std::wstring(hash.begin(), hash.begin() + 12);
  const std::filesystem::path dll = directory / L"EndfieldRuntime.dll";
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec) {
    if (error)
      *error = L"无法创建运行库目录";
    return false;
  }

  // 目录名只含哈希前缀，不足以证明内容：已有文件必须逐字节核对，不一致就覆盖（先删再写）。
  auto open_locked = [&]() {
    return CreateFileW(dll.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  };
  auto matches = [&](HANDLE file) {
    std::vector<unsigned char> bytes;
    return ReadWhole(file, bytes) && bytes.size() == blob.size && Sha256Hex(bytes.data(), bytes.size()) == hash;
  };

  HANDLE file = open_locked();
  if (file != INVALID_HANDLE_VALUE && !matches(file)) {
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    DeleteFileW(dll.c_str());
  }
  if (file == INVALID_HANDLE_VALUE) {
    HANDLE out = CreateFileW(dll.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
      if (error)
        *error = L"无法写入运行库（文件可能被占用）";
      return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(out, blob.data, static_cast<DWORD>(blob.size), &written, nullptr) && written == blob.size && FlushFileBuffers(out);
    CloseHandle(out);
    if (!ok) {
      if (error)
        *error = L"写入运行库失败";
      return false;
    }
    file = open_locked();
  }
  if (file == INVALID_HANDLE_VALUE || !matches(file)) {
    if (file != INVALID_HANDLE_VALUE)
      CloseHandle(file);
    if (error)
      *error = L"运行库校验失败（文件在解压后被改动）";
    return false;
  }

  lock_ = file;  // 拒绝写入（FILE_SHARE_READ）：会话期间无法被替换
  dll_path_ = dll.wstring();
  directory_ = directory.wstring();
  return true;
}

}  // namespace endfield_plugin
