// version.hpp —— suture-core 版本常量
// 注意：与根 CMakeLists.txt 的 project(SUture VERSION ...) 保持手动同步。
#pragma once

namespace suture {

inline constexpr char kVersionString[] = "1.0.0";

// 返回版本字符串（供测试与"关于"等界面使用）
const char* versionString();

}  // namespace suture
