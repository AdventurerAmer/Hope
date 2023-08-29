@echo off

pushd Tools
glslangValidator.exe -V --auto-map-locations ../Data/shaders/mesh.vert -o ../Data/shaders/bin/mesh.vert.spv
glslangValidator.exe -V --auto-map-locations ../Data/shaders/mesh.frag -o ../Data/shaders/bin/mesh.frag.spv
popd

echo assets cooked successfully