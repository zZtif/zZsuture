// buildinfo.hpp —— 构建标识（生成文件 buildinfo.cpp 提供 kBuildStamp）
// 形如 "03f939b+2026-10-05 20:41:22"：git 短哈希 + 配置时间。
// 验证问题时先看状态栏的构建标识，确认跑的是不是最新程序。
#pragma once

namespace suture {

extern const char* kBuildStamp;

}  // namespace suture
