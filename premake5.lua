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
        defines "HOPE_DEBUG"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        defines "HOPE_RELEASE"
        runtime "Release"
        optimize "on"

    filter "configurations:Shipping"
        defines "HOPE_SHIPPING"
        runtime "Release"
        optimize "on"

    filter "system:windows"
        defines "_CRT_SECURE_NO_WARNINGS"

    filter {}

project "AssetProcessor"
    kind "ConsoleApp"
    location "AssetProcessor"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"

    files { "AssetProcessor/**.h", "AssetProcessor/**.hpp", "AssetProcessor/**.cpp", "Data/**.vert", "Data/**.frag", "Data/**.glsl" }
    includedirs { "Engine", "ThirdParty/include" }

    debugdir "Data"
    targetdir "bin/%{prj.name}"
    objdir "bin/intermediates/%{prj.name}"

include "ThirdParty/ImGui"

project "Engine"

    dependson { "AssetProcessor", "ImGui" }

    kind "WindowedApp"
    location "Engine"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"

    files { "Engine/**.h", "Engine/**.hpp", "Engine/**.cpp", "Data/**.vert", "Data/**.frag", "Data/**.glsl" }

    includedirs { "Engine", "ThirdParty", "ThirdParty/ImGui", "ThirdParty/include" }
    libdirs { "ThirdParty/lib" }

    links { "vulkan-1", "ImGui" }

    debugdir "Data"
    targetdir "bin/%{prj.name}"
    objdir "bin/intermediates/%{prj.name}"

project "TestGame"

    dependson { "Engine" }

    kind "SharedLib"
    location "TestGame"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"

    defines { "HOPE_EXPORT" }
    files { "TestGame/**.h", "TestGame/**.hpp", "TestGame/**.cpp" }

    includedirs { "Engine", "ThirdParty/include", }

    debugdir "Data"
    targetdir "bin"
    objdir "bin/intermediates/%{prj.name}"