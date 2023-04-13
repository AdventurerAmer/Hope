@echo off

if not exist bin mkdir bin
pushd bin
cl -FC -Gm- -GR- -EHa- -nologo -Z7 -MTd -Od -Oi -W4 /wd4996 /wd4127 -DHE_UNITY_BUILD=1 /Fehope ../source/win32_main.cpp /link kernel32.lib user32.lib -subsystem:windows -opt:ref /incremental:no
popd