workspace "Hope"

    startproject "Editor"
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

group "Dependencies"
    include "ThirdParty/ImGui"
group ""

project "Engine"

    dependson { "ImGui" }
    
    kind "StaticLib"
    location "Engine"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    files { "Engine/**.h", "Engine/**.hpp", "Engine/**.cpp" }

    includedirs { "Engine", "ThirdParty", "ThirdParty/ExcaliburHash", "ThirdParty/ExcaliburHash/ExcaliburHash", "ThirdParty/ImGui", "ThirdParty/include" }
    libdirs { "ThirdParty/lib" }

    links
    {
        "vulkan-1",
        "ImGui"
    }

    filter "configurations:Debug"
        links
        {
            "SPIRV-Toolsd",
            "shaderc_sharedd",
            "spirv-cross-cored",
            "spirv-cross-glsld",
        }

    filter "configurations:Release"
        links
        {
            "SPIRV-Tools",
            "shaderc_shared",
            "spirv-cross-core",
            "spirv-cross-glsl",
        }

    filter "configurations:Shipping"
        links
        {
            "SPIRV-Tools",
            "shaderc_shared",
            "spirv-cross-core",
            "spirv-cross-glsl",
        }

    filter {}

    debugdir "data"
    targetdir "bin/%{prj.name}"
    objdir "bin/intermediates/%{prj.name}"
    targetname "Hope"

project "Editor"
    
    dependson { "Engine", "ImGui" }
    
    kind "WindowedApp"
    location "Editor"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    defines { "HE_EXPORT" }
    files { "Editor/**.h", "Editor/**.hpp", "Editor/**.cpp" }
    
    links
    {
        "Engine"
    }

    includedirs { "Engine", "ThirdParty", "ThirdParty/ImGui", "ThirdParty/ExcaliburHash", "ThirdParty/ExcaliburHash/ExcaliburHash", "ThirdParty/include" }

    debugdir "Data"
    targetdir "bin/%{prj.name}"
    objdir "bin/intermediates/%{prj.name}"
    targetname "Elpis"