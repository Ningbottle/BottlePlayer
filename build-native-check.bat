@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug
