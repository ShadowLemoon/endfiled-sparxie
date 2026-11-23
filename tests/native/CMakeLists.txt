# 原生测试。由顶层 CMakeLists.txt 引入；全部在同一个输出目录里（运行库测试靠它找 EndfieldRuntime.dll 和假游戏）。
function(endfield_test_target target)
  set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES MSVC_RUNTIME_LIBRARY "MultiThreaded")
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/module/core ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

# 核心：请求校验与会话守卫，不启动游戏
add_executable(EndfieldCoreTests endfield_core_tests.cpp)
endfield_test_target(EndfieldCoreTests)
target_link_libraries(EndfieldCoreTests PRIVATE endfield_core)
add_test(NAME EndfieldCoreTests COMMAND EndfieldCoreTests)

# 运行库：一个假的 Unity 游戏（窗口、消息循环、会被重置的输入类型和帧率）加 GameAssembly.dll / UnityPlayer.dll 的替身
add_library(FakeGameAssembly SHARED fake_game_assembly.cpp)
add_library(FakeUnityPlayer SHARED fake_unity_player.cpp)
add_executable(FakeUnityEndfield WIN32 fake_unity_endfield.cpp)
add_executable(EndfieldRuntimeTests endfield_runtime_tests.cpp)
add_dependencies(EndfieldRuntimeTests FakeUnityEndfield FakeGameAssembly FakeUnityPlayer EndfieldRuntime FakeEndfield)
foreach(t FakeGameAssembly FakeUnityPlayer FakeUnityEndfield EndfieldRuntimeTests)
  endfield_test_target(${t})
endforeach()
target_link_options(FakeUnityEndfield PRIVATE /ENTRY:wWinMainCRTStartup)
target_link_libraries(EndfieldRuntimeTests PRIVATE endfield_core)
add_test(NAME EndfieldRuntimeTests COMMAND EndfieldRuntimeTests)

# 假父进程：单元测试（假 Win32 调用）和端到端测试（假 Endfield.exe）
add_executable(FakeEndfield WIN32 fake_endfield.cpp ${CMAKE_SOURCE_DIR}/module/core/endfield_decoy.cpp)
add_executable(EndfieldDecoyTests endfield_decoy_tests.cpp ${CMAKE_SOURCE_DIR}/module/core/endfield_decoy.cpp)
add_dependencies(EndfieldDecoyTests FakeEndfield)
endfield_test_target(FakeEndfield)
endfield_test_target(EndfieldDecoyTests)
target_link_libraries(FakeEndfield PRIVATE wintrust crypt32 bcrypt rpcrt4)
target_link_options(FakeEndfield PRIVATE /ENTRY:wWinMainCRTStartup)
target_link_libraries(EndfieldDecoyTests PRIVATE endfield_core wintrust crypt32 bcrypt rpcrt4)
add_test(NAME EndfieldDecoyTests COMMAND EndfieldDecoyTests)

# 运行库自检：不需要游戏（指针模式保持、ini 键与默认值、拖动阈值、设备配置、UI 大小、云模式槽位与回退）
add_executable(EndfieldRuntimeSelfTest endfield_runtime_selftest.cpp)
endfield_test_target(EndfieldRuntimeSelfTest)
target_include_directories(EndfieldRuntimeSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
target_compile_definitions(EndfieldRuntimeSelfTest PRIVATE ENDFIELD_RUNTIME_BUILD=1)
target_compile_options(EndfieldRuntimeSelfTest PRIVATE /EHa)
target_link_libraries(EndfieldRuntimeSelfTest PRIVATE user32)
add_test(NAME EndfieldRuntimeSelfTest COMMAND EndfieldRuntimeSelfTest)

# ---- 插件自己的测试：设置解析、ini 读写、对真正构建出的 module.dll 的 ABI 合规测试 ----
add_executable(SettingsTests settings_tests.cpp)
endfield_test_target(SettingsTests)
target_link_libraries(SettingsTests PRIVATE endfield_plugin_impl)
add_test(NAME SettingsTests COMMAND SettingsTests)

add_executable(UiIniTests ui_ini_tests.cpp)
endfield_test_target(UiIniTests)
target_link_libraries(UiIniTests PRIVATE endfield_plugin_impl)
add_test(NAME UiIniTests COMMAND UiIniTests)

# 运行 ABI 测试要用的东西都在同一个目录：module.dll、EndfieldRuntime.dll、FakeEndfield.exe、plugin.json
add_executable(SpxAbiTests spx_abi_tests.cpp)
endfield_test_target(SpxAbiTests)
target_include_directories(SpxAbiTests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/sparxie-plugin-abi)
add_dependencies(SpxAbiTests sparxie_endfield_module EndfieldRuntime FakeEndfield)
add_custom_command(TARGET SpxAbiTests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/plugin.json $<TARGET_FILE_DIR:SpxAbiTests>/plugin.json)
add_test(NAME SpxAbiTests COMMAND SpxAbiTests)
