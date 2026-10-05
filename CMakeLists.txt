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
    src/core/version.cpp
    src/core/diag.cpp
    src/core/time.cpp
    src/core/textdec.cpp
    src/core/assparse.cpp
    src/core/asswrite.cpp
    src/core/epnum.cpp
    src/core/load.cpp
    src/core/merge.cpp)
target_include_directories(suture-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_link_libraries(suture-core PUBLIC Qt6::Core)

# 测试样例目录（仓库外 ../测试：六组 24 文件，版权原因不入库）
set(SUTURE_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../测试"
    CACHE PATH "SUture 六组字幕样例目录")

# ===== SUture 应用（Qt Widgets）=====
qt_add_executable(SUture
    src/app/main.cpp
    src/app/mainwindow.cpp
    src/app/mainwindow.hpp
    src/app/session.cpp
    src/app/session.hpp
    src/app/filelistmodel.cpp
    src/app/filelistmodel.hpp
    src/app/diagmodel.cpp
    src/app/diagmodel.hpp
    src/app/resources.qrc)
target_link_libraries(SUture PRIVATE suture-core Qt6::Widgets)

# GUI 子系统：启动不弹控制台窗口
set_target_properties(SUture PROPERTIES WIN32_EXECUTABLE TRUE)

# ===== 测试 =====
enable_testing()
add_subdirectory(tests)
