@echo off
chcp 936 >nul
setlocal
pushd "%~dp0"

set "VCPATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VCPATH%" goto HAVE_VS
echo [错误] 未找到 Visual Studio 2022 编译环境:
echo %VCPATH%
echo 请先安装 Visual Studio 2022 并勾选 使用 C++ 的桌面开发 工作负荷。
pause
exit /b 1
:HAVE_VS
call "%VCPATH%" >nul
if not exist bin mkdir bin

cl /nologo /Zi /O2 /W3 /EHsc /std:c++17 /Fo"bin/" /Fe"bin\统计代码量.exe" "源.cpp" /link /SUBSYSTEM:WINDOWS comctl32.lib user32.lib gdi32.lib shell32.lib ole32.lib
if errorlevel 1 goto FAIL

echo.
echo 编译成功，可执行文件: bin\统计代码量.exe
echo 双击即可运行。
pause
exit /b 0
:FAIL
echo.
echo 编译失败，请查看上方报错信息。
pause
exit /b 1
