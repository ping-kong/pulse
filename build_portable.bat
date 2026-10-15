@echo off
rem ============================================================================
rem  Pulse 便携版一键构建脚本
rem  前提：已用 apply_portable_patch.ps1 把 data-root 补丁打到本源码树。
rem  作用：配置(首次) -> 编译 7 个生产 exe -> 调用官方打包脚本生成便携 ZIP
rem        （脚本会自动写入 pulse.ini: data_dir=. ，数据默认落在程序同目录）。
rem  用法：在源码根目录运行本脚本（需管理员不重要，但需有 MSVC 工具链）。
rem ============================================================================
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
cd /d "%ROOT%"

rem 1) 准备 MSVC x64 环境（vcvars.bat 用 vswhere 定位 VS 并加载 vcvars64）。
call "%ROOT%scripts\vcvars.bat"
if errorlevel 1 (echo [错误] 未找到 Visual Studio C++ 工具链，请先安装 VS 2022 生成工具（含 MSVC + Windows SDK + CMake 组件）。 & exit /b 1)
chcp 65001 >nul
set "VSLANG=1033"
set "PULSE_BUILD_TYPE=Release"

rem 2) CMake 配置（仅当 build\CMakeCache.txt 不存在时执行）。
if not exist "%ROOT%build\CMakeCache.txt" (
    echo [1/3] 配置 CMake (Ninja, MSVC, Release)...
    set /p PULSE_UPDATE_KEY=<"%ROOT%cmake\update-public-key.txt"
    cmake -S . -B build -G Ninja ^
        -DCMAKE_BUILD_TYPE=%PULSE_BUILD_TYPE% ^
        -DLUMATEXT_SOURCE_DIR= ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
        -DPULSE_WITH_SELFTEST=OFF ^
        -DPULSE_RELEASE_SYMBOLS=OFF ^
        -DPULSE_UPDATE_MANIFEST_URL="https://github.com/jimmgreen/pulse/releases/latest/download/update-manifest.json" ^
        -DPULSE_UPDATE_PUBLIC_KEY_HEX=%PULSE_UPDATE_KEY%
    if errorlevel 1 (echo [错误] CMake 配置失败。 & exit /b 1)
) else (
    echo [1/3] 检测到已有 build 目录，跳过配置。
)

rem 3) 编译 7 个生产可执行文件（仅这些，不含 bench/test 目标）。
echo [2/3] 编译 7 个生产可执行文件...
set "TARGETS=pulse pulse_index pulse_elevated pulse_shell pulse_preview pulse_integration pulse_document"
for %%t in (%TARGETS%) do (
    echo   - 编译 %%t
    cmake --build build --target %%t
    if errorlevel 1 (echo [错误] 编译 %%t 失败。 & exit /b 1)
)

rem 4) 打包便携版（scripts\package_portable.ps1 会校验 7 exe + 2 dll，并写入 pulse.ini: data_dir=.）。
echo [3/3] 打包便携 ZIP...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\package_portable.ps1" -BuildDir build -OutputDir "%ROOT%PortableBuild"
if errorlevel 1 (echo [错误] 打包失败。 & exit /b 1)

rem 清理打包过程产生的临时 stage 目录（仅删 portable-stage-* 前缀）。
powershell -NoProfile -Command "Get-ChildItem -LiteralPath '%ROOT%PortableBuild' -Directory -Filter 'portable-stage-*' -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force"

echo.
echo 完成。便携包位于：PortableBuild\Pulse-*-portable-win-x64.zip
echo 解压后运行 pulse.exe 即可；数据默认在解压目录下（见 pulse.ini 的 data_dir）。
endlocal
