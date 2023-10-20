@echo off
if not exist "data/shaders/bin" mkdir "data/shaders/bin"
pushd tools
glslangValidator.exe -V --auto-map-locations ../data/shaders/mesh.vert -o ../data/shaders/bin/mesh.vert.spv
glslangValidator.exe -V --auto-map-locations ../data/shaders/mesh.frag -o ../data/shaders/bin/mesh.frag.spv
popd
echo assets cooked successfully