@echo off
if not exist "data/shaders/bin" mkdir "data/shaders/bin"
pushd tools
glslangValidator.exe -V --auto-map-locations ../data/shaders/default_vert.vert -o ../data/shaders/bin/default_vert.spv
glslangValidator.exe -V --auto-map-locations ../data/shaders/default_frag.frag -o ../data/shaders/bin/default_frag.spv
glslangValidator.exe -V --auto-map-locations ../data/shaders/skybox.vert -o ../data/shaders/bin/skybox.vert.spv
glslangValidator.exe -V --auto-map-locations ../data/shaders/skybox.frag -o ../data/shaders/bin/skybox.frag.spv
popd
echo assets cooked successfully