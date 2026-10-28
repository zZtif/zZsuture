@echo off
rem ============================================================
rem SUture 绿色包打包脚本（Windows / MSVC x64）
rem 用法: scripts\package.bat
rem 产出: D:\Wook\SUture\zZsuture\ ——完整绿色包，解压即用
rem 流程: 杀运行中的 exe → 编译 release + 全量测试 → 就地刷新
rem       exe → windeployqt 收 Qt 运行库 → 补 MSVC 运行库 DLL
rem 注意1: 就地刷新（不删目录）——所有者会在包里放使用说明等
rem       文件，整目录删除会误删；Qt 换大版本后手动清一次目录
rem 注意2: windeployqt 不拷 MSVC 运行库（msvcp140/vcruntime140），
rem       需手动从本机 VC Redist 目录补，否则干净机器缺 dll 起不来
rem ============================================================
setlocal
call "D:\Application\Visual studio\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%PATH%;D:\Application\Visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;D:\Qt\6.8.3\msvc2022_64\bin"

rem 0) 运行中的 SUture 会锁住 exe（拷贝失败），先杀
taskkill /IM SUture.exe /F >nul 2>&1

rem 1) release 编译 + 测试（测试不过不出包）
call "%~dp0build.bat" release --test
if errorlevel 1 exit /b 1

rem 2) 就地刷新 exe（PKG 是已存在的目录，copy 到目录=拷入其内）
set "PKG=D:\Wook\SUture\zZsuture"
if not exist "%PKG%" mkdir "%PKG%"
copy /y "build\release\SUture.exe" "%PKG%"
if errorlevel 1 (
    echo [错误] 拷贝 exe 失败——请关闭正在运行的 SUture 后重试
    exit /b 1
)

rem 3) Qt 运行库（DLL/插件/中文翻译）
windeployqt --release --compiler-runtime "%PKG%\SUture.exe"
if errorlevel 1 exit /b 1

rem windeployqt --compiler-runtime 会拖一个 25MB 的 vc_redist 安装器进包，
rem 运行库 DLL 已直接拷贝，安装器多余，删掉
del "%PKG%\vc_redist.x64.exe" >nul 2>&1

rem 4) MSVC 运行库（本机 Redist 版本号变了要改这行）
set "CRT=D:\Application\Visual studio\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"
rem cmd 的 copy 不支持空格分隔多源文件，逐个拷
copy /y "%CRT%\msvcp140.dll" "%PKG%" >nul
if errorlevel 1 exit /b 1
copy /y "%CRT%\vcruntime140.dll" "%PKG%" >nul
if errorlevel 1 exit /b 1
copy /y "%CRT%\vcruntime140_1.dll" "%PKG%" >nul
if errorlevel 1 exit /b 1
if errorlevel 1 exit /b 1

echo ===== 打包完成: %PKG% =====
exit /b 0
