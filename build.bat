@echo off

if not exist bin mkdir bin
pushd bin

set CommonFlags=-FC -Gm- -GR- -EHa- -nologo -Oi -I../source/ -W4 /wd4996 /wd4127
set CommonDefines=-DHE_UNITY_BUILD=1
set DebugConfig=-Z7 -MTd -Od
set ReleaseConfig=-MT -O2
set Config=%DebugConfig%
set CommonLinkFlags=user32.lib -subsystem:windows -opt:ref /incremental:no

set TimeStamp=%DATE:/=_%_%TIME::=_%
set TimeStamp=%timestamp: =%

del *.pdb > nul 2> nul
rem game
cl %CommonFlags% %CommonDefines% %Config% /Fegame ../source/game/game.cpp /LD /link /PDB:game_%TimeStamp%.pdb %CommonLinkFlags% /export:init_game /export:on_event /export:on_update

rem engine

set Includes=-I../third_party/include
set LibIncludes=-libpath:../third_party/lib

cl %CommonFlags% %CommonDefines% %Config% %Includes% /Fehope ../source/win32_main.cpp /link %LibIncludes% %CommonLinkFlags% vulkan-1.lib

"../tools/glslc.exe" ../data/shaders/basic.vert -o ../data/shaders/basic.vert.spv
"../tools/glslc.exe" ../data/shaders/basic.frag -o ../data/shaders/basic.frag.spv

popd