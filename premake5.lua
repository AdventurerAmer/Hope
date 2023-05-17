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
    characterset "MBCS" -- Multi-byte Character Set

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

project "Engine"

    kind "WindowedApp"
    location "source"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"

    files { "source/**.h", "source/**.hpp", "source/**.cpp" }

    includedirs { "source", "third_party/include" }
    libdirs { "third_party/lib" }

    links { "vulkan-1" }

    debugdir "data"
    targetdir "bin/%{prj.name}"
    objdir "bin/intermediates/%{prj.name}"