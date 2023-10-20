workspace "Hope"

    startproject "Engine"
    architecture "x86_64"
    systemversion "latest"

    configurations { "Debug", "Release", "Shipping" }

    flags { "MultiProcessorCompile" }
    intrinsics "On"
    floatingpoint "Fast"
    exceptionhandling "Off"
    rtti "Off"
    characterset "MBCS" --Multi-byte Character Set

    filter "configurations:Debug"
        defines "HE_DEBUG"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        defines "HE_RELEASE"
        runtime "Release"
        optimize "on"

    filter "configurations:Shipping"
        defines "HE_SHIPPING"
        runtime "Release"
        optimize "on"

    filter "system:windows"
        defines "_CRT_SECURE_NO_WARNINGS"

    filter {}

include "ThirdParty/ImGui"

project "Engine"

    dependson { "AssetProcessor", "ImGui" }

    kind "WindowedApp"
    location "Engine"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    files { "Engine/**.h", "Engine/**.hpp", "Engine/**.cpp", "Data/**.vert", "Data/**.frag", "Data/**.glsl" }

    includedirs { "Engine", "ThirdParty", "ThirdParty/ImGui", "ThirdParty/include" }
    libdirs { "ThirdParty/lib" }

    links { "vulkan-1", "ImGui" }

    debugdir "data"
    targetdir "bin/%{prj.name}"
    objdir "bin/intermediates/%{prj.name}"

project "Game"

    dependson { "Engine" }

    kind "SharedLib"
    location "Game"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    defines { "HE_EXPORT" }
    files { "Game/**.h", "Game/**.hpp", "Game/**.cpp" }

    includedirs { "Engine", "ThirdParty/include", }

    debugdir "Data"
    targetdir "bin"
    objdir "bin/intermediates/%{prj.name}"