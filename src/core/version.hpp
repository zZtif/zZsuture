// version.hpp —— suture-core 版本常量
// 与根 CMakeLists.txt 的 project(SUture VERSION ...) 一致，靠文件尾的
// static_assert 守护（CMake 注入 SUTURE_CMAKE_VERSION 供比对）——
// 两处改漏一处直接编译失败，不再依赖人工记忆同步。
#pragma once

#include <string_view>

namespace suture {

inline constexpr char kVersionString[] = "1.0.0";

// 返回版本字符串（供测试与"关于"等界面使用）
const char* versionString();

}  // namespace suture

// 守护：CMake 的 project VERSION 与此处的 kVersionString 必须一致。
// 非 CMake 构建（不存在）无此宏，跳过守护不误伤。
#ifdef SUTURE_CMAKE_VERSION
static_assert(std::string_view(suture::kVersionString) ==
                  std::string_view(SUTURE_CMAKE_VERSION),
              "version.hpp 与 CMakeLists.txt 的版本号不一致——两处都要改");
#endif
