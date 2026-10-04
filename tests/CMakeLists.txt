# tests —— 仅链接 suture-core（core 无 GUI 依赖，测试不需要 Widgets）
# 用标准 add_test 写法（qt_add_test 需 Qt 内部构建环境，不可用于外部项目）
add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE suture-core Qt6::Test)
add_test(NAME tst_core COMMAND tst_core)
