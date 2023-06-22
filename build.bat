@echo off

if not exist bin mkdir bin
pushd bin

set CommonFlags=-FC -Gm- -GR- -EHa- -nologo -Oi -I../source/ -W4 /wd4996 /wd4127
set CommonDefines=-DHE_UNITY_BUILD=1
set DebugConfig=-Z7 -MTd -Od
set ReleaseConfig=-MT -O2
set CommonLinkFlags=user32.lib -subsystem:windows -opt:ref /incremental:no
set ExportedGameFunctions=/export:init_game /export:on_event /export:on_update
set Includes=-I../third_party/include
set LibIncludes=-libpath:../third_party/lib

set TimeStamp=%DATE:/=_%_%TIME::=_%
set TimeStamp=%timestamp: =%

del *.pdb > nul 2> nul

rem game
cl %CommonFlags% %CommonDefines% %DebugConfig% /Fegame ../game/game.cpp /LD /link /PDB:game_%TimeStamp%.pdb %CommonLinkFlags% %ExportedGameFunctions%

rem todo(amer): temprary just compiling shaders here for now...
"../tools/glslangValidator.exe" --reflect-all-block-variables -V ../data/shaders/common.vert -o ../data/shaders/common.vert.spv
"../tools/glslangValidator.exe" -V ../data/shaders/mesh.vert -o ../data/shaders/mesh.vert.spv
"../tools/glslangValidator.exe" -V ../data/shaders/mesh.frag -o ../data/shaders/mesh.frag.spv

popd