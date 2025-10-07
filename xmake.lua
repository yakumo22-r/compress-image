add_rules("mode.debug", "mode.release")
set_languages("c++20")
add_requires("libvips", {configs = {
    libspng = true,
    libjpeg = true,
    libwebp = true,
    libimagequant = true,
}})

add_requires("imgui", {configs = {glfw_opengl3 = true}})
add_requires("glfw", "glew")

add_requires("fmt")
option("locale_cn")
    set_default(false)
    add_defines("YKM22_LOCALE_CN")

option("use_stbi")
    set_default(false)
    add_defines("YKM22_IMG_BG_USE_STBI")

target("compress-img")
    set_kind("binary")
    set_license("LGPL-2.1")
    add_packages("libvips", "fmt")
    add_packages("imgui", "glfw", "glew")
    add_files("src/*.cpp")
    add_defines("IMGUI_IMPL_OPENGL_LOADER_GLEW") -- ImGui用GLEW加载OpenGL
    add_options("locale_cn")
    if is_plat("windows") then
        add_files("app.rc")
        if is_mode("release") then
            add_ldflags("/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup", {force = true})
        end
        add_syslinks("comdlg32")
    end

