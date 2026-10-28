@echo off
rem ============================================================
rem SUture 绿色包打包脚本（Windows / MSVC x64）
rem 用法: scripts\package.bat
rem 产出: D:\Wook\SUture\zZsuture\ ——完整绿色包，解压即用
rem 流程: 编译 release + 全量测试 → 清空旧包 → 拷 exe
rem       → windeployqt 收 Qt 运行库 → 补 MSVC 运行库 DLL
rem 注意: windeployqt 不拷 MSVC 运行库（msvcp140/vcruntime140），
rem       需手动从本机 VC Redist 目录补，否则干净机器缺 dll 起不来
rem ============================================================
setlocal
call "D:\Application\Visual studio\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%PATH%;D:\Application\Visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;D:\Qt\6.8.3\msvc2022_64\bin"

rem 1) release 编译 + 测试（测试不过不出包）
call "%~dp0build.bat" release --test
if errorlevel 1 exit /b 1

rem 2) 清空旧包重建
set "PKG=D:\Wook\SUture\zZsuture"
if exist "%PKG%" rmdir /s /q "%PKG%"
mkdir "%PKG%"
copy /y "build\release\SUture.exe" "%PKG%\" >nul
if errorlevel 1 exit /b 1

rem 3) Qt 运行库（DLL/插件/中文翻译）
windeployqt --release --compiler-runtime "%PKG%\SUture.exe"
if errorlevel 1 exit /b 1

rem 4) MSVC 运行库（本机 Redist 版本号变了要改这行）
set "CRT=D:\Application\Visual studio\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"
copy /y "%CRT%\msvcp140.dll" "%CRT%\vcruntime140.dll" "%CRT%\vcruntime140_1.dll" "%PKG%\" >nul
if errorlevel 1 exit /b 1

echo.
echo ===== 打包完成: %PKG% =====
exit /b 0
