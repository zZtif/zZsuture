cmake_minimum_required(VERSION 3.24)
project(SUture VERSION 0.1.0 LANGUAGES CXX)

# C++20（架构 D12：MSVC 2022 x64 + Qt 6.8 LTS）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Test)

qt_standard_project_setup()   # AUTOMOC/AUTORCC/AUTOUIC 全开

# ===== suture-core：领域逻辑静态库 =====
# 依赖规则（架构 §1.2）：core 只链 QtCore，永不链 Widgets——
# 违反"逻辑不知道界面存在"在此处即编译失败。
add_library(suture-core STATIC
    src/core/version.cpp)
target_include_directories(suture-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_link_libraries(suture-core PUBLIC Qt6::Core)

# ===== SUture 应用（Qt Widgets）=====
qt_add_executable(SUture
    src/app/main.cpp
    src/app/mainwindow.cpp
    src/app/mainwindow.hpp
    src/app/resources.qrc)
target_link_libraries(SUture PRIVATE suture-core Qt6::Widgets)

# GUI 子系统：启动不弹控制台窗口
set_target_properties(SUture PROPERTIES WIN32_EXECUTABLE TRUE)

# ===== 测试 =====
enable_testing()
add_subdirectory(tests)
