// 运行库 EndfieldRuntime.dll 随 module.dll 一起发布（嵌在资源里）。Sparxie 里用户确认的哈希只覆盖 module.dll 和 plugin.json，
// 所以运行库必须在 module.dll 里：会话开始时解压到 <data_dir>\runtime\<sha256 前 12 位>\EndfieldRuntime.dll，
// 重新校验哈希，并一直持有一个拒绝写入的句柄，直到会话释放——期间别人无法替换它。
#pragma once

#include <string>

namespace endfield_plugin {

struct RuntimeBlob {
  const void *data = nullptr;
  size_t size = 0;
};

// 提供要解压的运行库字节。默认从 module.dll 的资源里取；测试可以换成自己的字节。
using RuntimeBlobProvider = RuntimeBlob (*)();
void SetRuntimeBlobProvider(RuntimeBlobProvider provider);
RuntimeBlob GetRuntimeBlob();

// 解压后的运行库：路径 + 持有的句柄（拒绝写入）。Close() 之后文件才能被替换。
class ExtractedRuntime {
public:
  ExtractedRuntime() = default;
  ExtractedRuntime(const ExtractedRuntime &) = delete;
  ExtractedRuntime &operator=(const ExtractedRuntime &) = delete;
  ~ExtractedRuntime() { Close(); }

  // 成功返回 true。失败时 error 里是给用户看的原因（中文，不含路径以外的敏感信息）。
  bool Extract(const std::wstring &data_dir, std::wstring *error);
  void Close();

  const std::wstring &dll_path() const { return dll_path_; }
  const std::wstring &directory() const { return directory_; }

private:
  std::wstring dll_path_;
  std::wstring directory_;
  void *lock_ = nullptr;  // HANDLE
};

// 小写十六进制 SHA-256（内部用，测试也用）
std::string Sha256Hex(const void *data, size_t size);

}  // namespace endfield_plugin
