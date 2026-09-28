-- Standalone IAD source build.
-- Set COMMONLIBF4_PATH to the root of a CommonLibF4 checkout.
local commonlibf4_path = os.getenv("COMMONLIBF4_PATH")
if not commonlibf4_path or commonlibf4_path == "" then
    raise("Set COMMONLIBF4_PATH to the root of a CommonLibF4 checkout")
end

commonlibf4_path = path.normalize(commonlibf4_path)
if not os.isdir(commonlibf4_path) then
    raise("COMMONLIBF4_PATH does not point to a directory: " .. commonlibf4_path)
end

includes(commonlibf4_path)
set_languages("c++23")

add_requires("simpleini v4.25")
add_requires("nlohmann_json v3.12.0")
add_requires("imgui v1.92.8-docking", { configs = { win32 = true, dx11 = true } })
add_requires("microsoft-detours 2023.6.8")

target("ImmersiveArsenalDisplays")
    set_group("Projects")
    set_version("3.0.0")
    add_rules("commonlibf4.plugin", {
        name = "ImmersiveArsenalDisplays",
        author = "Hwushen",
        description = "ImmersiveArsenalDisplays",
        address_library = true,
        struct_use = false,
        sig_scanning = false
    })

    add_deps("commonlibf4")
    add_packages("simpleini", "nlohmann_json", "imgui", "microsoft-detours")

    on_load(function (target)
        local imgui = target:pkg("imgui")
        if imgui then
            local include_root = path.join(imgui:installdir(), "include")
            target:add("includedirs", include_root)
            target:add("includedirs", path.join(include_root, "backends"))
        end
        local detours = target:pkg("microsoft-detours")
        if detours then
            target:add("includedirs", path.join(detours:installdir(), "include"))
        end
    end)

    add_cxflags("/utf-8", { force = true })
    add_cxxflags("/utf-8", { force = true })
    add_files("src/**.cpp")
    remove_files("src/Combat/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src", "src/Data", "src/Engine", "src/System", "src/UI", "src/Combat")
    set_pcxxheader("src/pch.h")

    if is_mode("release", "releasedbg") then
        add_ldflags("/GUARD:CFG:NO")
        add_ldflags("/CETCOMPAT:NO")
    end

    after_build(function (target)
        local plugin_dir = path.join(os.scriptdir(), "data", "F4SE", "Plugins")
        os.mkdir(plugin_dir)
        os.cp(target:targetfile(), plugin_dir)
        local symbol_file = target:symbolfile()
        if symbol_file and os.isfile(symbol_file) then
            os.cp(symbol_file, plugin_dir)
        end
    end)
