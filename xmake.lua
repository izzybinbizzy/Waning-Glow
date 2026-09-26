-- Waning Glow - SKSE plugin. GPL-3.0-or-later, see LICENSE.txt.
set_xmakever("3.0.0")
set_project("WaningGlow")
set_version("1.0.0")
set_license("GPL-3.0-or-later")
set_arch("x64")
set_languages("c++23")
-- the DLL carries its own Visual C++ runtime, so an older runtime on a player's PC cannot stop it loading
set_runtimes("MT")
add_rules("mode.releasedbg")
set_defaultmode("releasedbg")

-- GLOW_VR=1 builds for Skyrim VR only; anything else is the SE + AE build (same switch as Illuminated)
local vr_only = os.getenv("GLOW_VR") == "1"
set_config("skyrim_se", not vr_only)
set_config("skyrim_ae", not vr_only)
set_config("skyrim_vr", vr_only)

includes("lib/commonlibsse-ng")

-- rule files are JSON (MIT-licensed library; the same version CommonLib pins for its own JSON option)
add_requires("nlohmann_json v3.12.0")

target("WaningGlow", function()
    add_defines("NOMINMAX") -- windows.h min/max macros break std::min/max and numeric_limits::max
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = "WaningGlow",
        author = "izzydoingit",
        description = "Waning Glow - an enchanted weapon's lights follow its charge",
    })
    add_packages("nlohmann_json")
    -- the source is split by job (see the file map at the top of src/main.cpp); every .cpp in src is built
    add_files("src/*.cpp")
    add_headerfiles("src/*.h", "include/*.h")
    add_includedirs("src", "include")
    set_pcxxheader("src/PCH.h")
    set_warnings("allextra")
end)

-- the light's behaviour, tested without the game: xmake build test_glow && xmake run test_glow
target("test_glow", function()
    add_defines("NOMINMAX") -- windows.h min/max macros break std::min/max and numeric_limits::max
    set_kind("binary")
    set_default(false)
    add_packages("nlohmann_json")  -- the rule files' text (src/RulesText.h) is tested too
    add_files("tests/test_glow.cpp")
    add_includedirs("src")
end)
