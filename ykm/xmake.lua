set_project("ykm-app")
set_version("0.2.0")

set_languages("c11", "cxx17")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")

option("ykm_app")
    set_default("")
    set_showmenu(true)
    set_description("Path to a YKM application project with generated embedded sources")
option_end()

add_requires("libsdl3 3.2.16")
add_requires("libsdl3_image")
add_requires("quickjs-ng 0.9.0")
includes("native/libwebp.lua")
add_requires("ykm-libwebp 1.6.0", {system = false})
add_requires("imgui v1.91.9")

local function configure_native_target()
    add_includedirs("src", "include", {public = true})
    if is_mode("debug") then
        add_defines("YKM_DEBUG_CONSOLE")
    end
    if is_plat("windows") then
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    end
end

target("ykm-core")
    set_kind("static")
    add_files(
        "src/app_storage.cpp",
        "src/debug_log.cpp",
        "src/event_loop.cpp",
        "src/jsapi.cpp",
        "src/js_util.cpp",
        "src/mod_fs.cpp",
        "src/mod_path.cpp",
        "src/mod_webp.cpp",
        "src/module_loader.cpp",
        "src/session.cpp")
    add_packages("quickjs-ng")
    add_packages("ykm-libwebp", {public = true})
    configure_native_target()

target("ykm-sys")
    set_kind("static")
    add_deps("ykm-core")
    add_files("src/host_tools.cpp", "src/system_window.cpp", "src/system_event.cpp", "src/win32_window.cpp", "src/window_events.cpp", "src/sys_api.cpp")
    add_packages("quickjs-ng")
    if not is_plat("windows") then
        add_files("src/sdl_window.cpp")
        add_packages("libsdl3")
    end
    configure_native_target()
    if is_plat("windows") then
        add_syslinks("ole32", "oleaut32", "uuid", "shell32")
    end

target("ykm-ui-imgui")
    set_kind("static")
    add_deps("ykm-core", "ykm-sys")
    add_files(
        "src/component_api.cpp",
        "src/d3d11_renderer.cpp",
        "src/debug_console.cpp",
        "src/desktop_host.cpp",
        "src/fonts.cpp",
        "src/host_windows.cpp",
        "src/imgui_impl_dx11.cpp",
        "src/imgui_impl_win32.cpp",
        "src/ui_imgui_api.cpp",
        "src/ui_renderer.cpp")
    add_packages("quickjs-ng", "imgui")
    if not is_plat("windows") then
        add_packages("libsdl3", "libsdl3_image")
    end
    if is_plat("windows") then
        add_syslinks("d3d11", "dxgi", "d3dcompiler")
    end
    configure_native_target()

target("ykm")
    set_kind("binary")
    set_rundir(os.projectdir())
    add_deps("ykm-core", "ykm-sys", "ykm-ui-imgui")
    add_files("src/main.cpp", "src/embedded_app.c", "src/packed_jsapi.c")
    if has_config("ykm_app") and get_config("ykm_app") ~= "" then
        local generated = path.join(get_config("ykm_app"), ".ykm", "generated")
        remove_files("src/embedded_app.c", "src/packed_jsapi.c")
        add_files(path.join(generated, "ykm_embedded_app.c"), path.join(generated, "ykm_jsapi_register_packed.c"))
    end
    configure_native_target()
    add_packages("quickjs-ng", "imgui")
    if not is_plat("windows") then
        add_packages("libsdl3", "libsdl3_image")
    end
    if is_plat("windows") then
        add_syslinks("ole32", "oleaut32", "uuid", "shell32")
    end
    after_build(function (target)
        if not (has_config("ykm_app") and get_config("ykm_app") ~= "") then
            os.cp("widgets", path.join(target:targetdir(), "widgets"))
        end
    end)

if has_config("ykm_app") and get_config("ykm_app") ~= "" then
    includes(path.join(get_config("ykm_app"), ".ykm", "generated", "ykm_native.lua"))
end

if is_plat("windows") then
    target("ykm-test-win32-sys")
        set_kind("binary")
        set_default(false)
        add_deps("ykm-core")
        add_files("tools/tests/win32_sys.cpp", "src/system_window.cpp", "src/win32_window.cpp", "src/system_event.cpp", "src/embedded_app.c")
        add_syslinks("user32", "gdi32", "shell32")
        configure_native_target()

    target("ykm-test-d3d11-surface")
        set_kind("binary")
        set_default(false)
        add_files("tools/tests/d3d11_surface.cpp", "src/d3d11_renderer.cpp")
        add_syslinks("user32", "gdi32", "d3d11", "dxgi")
        configure_native_target()

    target("ykm-test-host-surface")
        set_kind("binary")
        set_default(false)
        add_deps("ykm-ui-imgui")
        add_files("tools/tests/host_surface.cpp", "src/embedded_app.c", "src/packed_jsapi.c")
        add_packages("quickjs-ng", "imgui")
        add_syslinks("ole32", "oleaut32", "uuid", "shell32")
        configure_native_target()
end
