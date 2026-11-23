// module.dll 唯一的导出：sparxie_module_query。其余全部是内部符号。
#include "spx_module.h"

extern "C" __declspec(dllexport) SpxError __cdecl sparxie_module_query(uint32_t host_abi_major, uint32_t host_abi_minor, const SpxModuleApi **out) {
  (void)host_abi_minor;  // 次版本只追加，向前兼容
  if (!out)
    return SPX_ERR_INVALID_ARGUMENT;
  *out = nullptr;
  if (host_abi_major != SPX_MODULE_ABI_MAJOR)
    return SPX_ERR_ABI_MISMATCH;
  *out = endfield_plugin::GetModuleApi();
  return SPX_OK;
}
