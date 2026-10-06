# tests —— 仅链接 suture-core（core 无 GUI 依赖，测试不需要 Widgets）
# 用标准 add_executable + add_test 写法（qt_add_test 需 Qt 内部构建环境）
function(suture_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE suture-core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

suture_add_test(tst_time)        # T4 厘秒时间
suture_add_test(tst_textdec)     # P1  编码探测
suture_add_test(tst_ass)         # P2/P3/P4/P5/P9/P10/P11/T2/T5/S5/S6
suture_add_test(tst_epnum)       # P6/P8 文件名识别 + 装载门面
suture_add_test(tst_merge)       # 阶段 2/3：S1-S7/T3/T6/C1/C2 合并引擎+校验
suture_add_test(tst_mpls)        # 阶段 3：T1/T3/D10 MPLS 解析与映射
suture_add_test(tst_roundtrip)   # 阶段 1 验收：六组全量往返

# 拖拽排序（GUI 层）：模拟鼠标按下/移动/松开。需要 app 层源码 + Widgets
add_executable(tst_drag tst_drag.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/filelistmodel.cpp)
target_include_directories(tst_drag PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_link_libraries(tst_drag PRIVATE suture-core Qt6::Test Qt6::Widgets)
add_test(NAME tst_drag COMMAND tst_drag)

target_compile_definitions(tst_roundtrip PRIVATE
    SUTURE_TEST_DATA_DIR="${SUTURE_TEST_DATA_DIR}")
target_compile_definitions(tst_merge PRIVATE
    SUTURE_TEST_DATA_DIR="${SUTURE_TEST_DATA_DIR}")
