@echo off
rem ============================================================
rem SUture 构建脚本（Windows / MSVC x64 / Ninja）
rem 用法: scripts\build.bat [debug^|release] [--test]
rem   无参数默认 debug；--test 追加运行单元测试
rem 注意: VS 与 Ninja 路径为本机配置（见 AGENTS.md 环境备注）
rem ============================================================
setlocal
call "D:\Application\Visual studio\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%PATH%;D:\Application\Visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;D:\Qt\6.8.3\msvc2022_64\bin"

set PRESET=%1
if "%PRESET%"=="" set PRESET=debug

cmake --preset %PRESET%
if errorlevel 1 exit /b 1
cmake --build --preset %PRESET%
if errorlevel 1 exit /b 1

if "%2"=="--test" ctest --preset %PRESET%
exit /b 0
