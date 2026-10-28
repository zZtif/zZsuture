# SUture
<img width="640" height="640" alt="2311" src="https://github.com/user-attachments/assets/eced7324-f808-446e-86ec-fbcba5d602dd" />



BDMV 多集连播字幕合并工具（Windows / C++ / Qt Widgets）。

主要用于解决部分BDMV盘一个m2ts文件播放多集的情况，这种情况下需要打字幕就会变得非常困难，所以该工具就提供了字幕合成的功能以及针对特殊情况情况(比如说插入了一段黑场)对字幕进行完美合轴，
该工具具有GUI界面，具体使用方法参考工具压缩包的说明文档

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
