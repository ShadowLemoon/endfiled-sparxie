cmake_minimum_required(VERSION 3.20)
project(SparxieEndfieldPlugin LANGUAGES CXX)

# 静态 CRT：插件 DLL 装进别人的进程，不依赖用户机器上的 VC 运行库
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_compile_options(/utf-8)

# 所有可执行文件和 DLL 放进同一个输出目录：运行库测试靠它找到 EndfieldRuntime.dll 和假游戏
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

enable_testing()

# ---- 运行库：注入到游戏进程里的 DLL。构建后复制一份到 runtime_blob，由 module.dll 以资源形式嵌入 ----
add_library(EndfieldRuntime SHARED runtime/endfield_runtime.cpp)
target_include_directories(EndfieldRuntime PRIVATE module/core runtime)
target_compile_definitions(EndfieldRuntime PRIVATE ENDFIELD_RUNTIME_BUILD=1)
# 运行库用 __try/__except 保护对游戏内存的读取：处理器也必须接住普通读取的异常（clang-cl 只在异步异常模式下保证）
target_compile_options(EndfieldRuntime PRIVATE /EHa)
add_custom_command(TARGET EndfieldRuntime POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/runtime_blob
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:EndfieldRuntime> ${CMAKE_BINARY_DIR}/runtime_blob/EndfieldRuntime.dll)

# ---- 核心：启动、假父进程、注入（静态库；插件里只有 sparxie_module_query 一个导出）----
add_library(endfield_core STATIC module/core/endfield_core.cpp module/core/endfield_decoy.cpp)
target_include_directories(endfield_core PUBLIC module/core)
target_compile_definitions(endfield_core PUBLIC ENDFIELD_CORE_STATIC=1)
# 假父进程：WinVerifyTrust、CryptQueryObject / CryptHashCertificate2、UuidCreate
target_link_libraries(endfield_core PUBLIC wintrust crypt32 bcrypt rpcrt4)

# ---- ABI 适配层（静态库，测试可以不经 DLL 直接用）----
add_library(endfield_plugin_impl STATIC
  module/settings.cpp module/ui_ini.cpp module/runtime_extract.cpp module/spx_module.cpp)
target_include_directories(endfield_plugin_impl PUBLIC module third_party/sparxie-plugin-abi)
target_link_libraries(endfield_plugin_impl PUBLIC endfield_core bcrypt rpcrt4)

# ---- 插件本体：module.dll（嵌入运行库）----
add_library(sparxie_endfield_module SHARED module/module_main.cpp module/module.rc)
set_target_properties(sparxie_endfield_module PROPERTIES OUTPUT_NAME module PREFIX "")
target_include_directories(sparxie_endfield_module PRIVATE ${CMAKE_BINARY_DIR}/runtime_blob)
target_link_libraries(sparxie_endfield_module PRIVATE endfield_plugin_impl)
add_dependencies(sparxie_endfield_module EndfieldRuntime)
set_source_files_properties(module/module.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/runtime_blob/EndfieldRuntime.dll)

add_subdirectory(tests/native)
