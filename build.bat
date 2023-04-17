@echo off

set CommonFlags=-FC -Gm- -GR- -EHa- -nologo -Oi -I../source/ -W4 /wd4996 /wd4127
set CommonDefines=-DHE_UNITY_BUILD=1
set DebugConfig=-Z7 -MTd -Od
set ReleaseConfig=-MT -O2
set CommonLinkFlags=user32.lib -subsystem:windows -opt:ref /incremental:no
if not exist bin mkdir bin
pushd bin
rem game
cl %CommonFlags% %CommonDefines% %DebugConfig% /Fegame ../source/game/game.cpp /LD /link %CommonLinkFlags% /export:init_game /export:on_event /export:on_update
rem engine
cl %CommonFlags% %CommonDefines% %DebugConfig% /Fehope ../source/win32_main.cpp /link %CommonLinkFlags%
popd