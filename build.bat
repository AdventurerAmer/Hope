@echo off

if not exist bin mkdir bin
pushd bin

set CommonFlags=-FC -Gm- -GR- -EHa- -nologo -Oi -I../source/ -W4 /wd4996 /wd4127
set CommonDefines=-DHE_UNITY_BUILD=1
set DebugConfig=-Z7 -MTd -Od
set ReleaseConfig=-MT -O2
set Config=%DebugConfig%
set CommonLinkFlags=user32.lib -subsystem:windows -opt:ref /incremental:no

set time_stamp=%date:~-4,4%%date:~-10,2%%date:~-7,2%_%time:~0,2%%time:~3,2%%time:~6,2%

del *.pdb > nul 2> nul
rem game
cl %CommonFlags% %CommonDefines% %Config% /Fegame ../source/game/game.cpp /LD /link /PDB:game_%time_stamp%.pdb %CommonLinkFlags% /export:init_game /export:on_event /export:on_update
rem engine
cl %CommonFlags% %CommonDefines% %Config% /Fehope ../source/win32_main.cpp /link %CommonLinkFlags%
popd