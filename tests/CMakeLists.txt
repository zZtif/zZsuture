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
suture_add_test(tst_roundtrip)   # 阶段 1 验收：六组全量往返

target_compile_definitions(tst_roundtrip PRIVATE
    SUTURE_TEST_DATA_DIR="${SUTURE_TEST_DATA_DIR}")
