# SUture

BDMV 多集连播字幕合并工具（Windows / C++ / Qt Widgets）。

原盘（BDMV）的 MPLS 播放列表把多集串成一条无缝播放链，而播放器最多只能挂
1-2 条字幕——本工具把逐集制作的 ASS/SRT 字幕按 MPLS 偏移合并成一个对齐的
字幕文件。

**状态**：需求分析 v1.3 / 架构设计 v1.1 已冻结，开发中（M1）。

设计文档见仓库外 `../设计/`（需求分析 / 架构设计 / 待解决问题清单）。

## 构建（M1 目标布局）

```
CMake + Qt 6.8 LTS (MSVC x64)
├─ src/core    # suture-core 静态库：解析/合并/MPLS/校验（仅依赖 QtCore）
├─ src/app     # Qt Widgets 应用
└─ tests       # Qt Test 单元测试
```

## 许可

Qt 以 LGPL v3 动态链接随发布物分发（windeployqt 绿色包）。
