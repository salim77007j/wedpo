@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\salim\.zcode\workspace\default\wedpo
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRUST_CORE_DIR="%CD%\core\target" -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64" > build_log.txt 2>&1
if errorlevel 1 goto :done
cmake --build build --parallel >> build_log.txt 2>&1
:done
echo BUILD_SCRIPT_DONE >> build_log.txt
