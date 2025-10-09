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

function utils_cpp()
    add_files("src/file_drop.cpp", "src/img_bg_gl.cpp", "src/utils.cpp", "src/gui.cpp");
end

target("compress-img")
    set_kind("binary")
    set_license("LGPL-2.1")
    add_packages("libvips")
    add_packages("imgui", "glfw", "glew", "fmt")
    utils_cpp()
    add_files(
        "src/compress_img.cpp", 
        "src/compress_img_render.cpp", 
        "src/compress_img_api.cpp", 
        "src/img_bg_1.cpp")
    add_defines("IMGUI_IMPL_OPENGL_LOADER_GLEW")
    add_defines("YKM22_IMG_BG_USE_LIBVIPS")
    add_options("locale_cn")
    if is_plat("windows") then
        add_files("compress_img.rc")
        if is_mode("release") then
            add_ldflags("/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup", {force = true})
        end
        add_syslinks("comdlg32", "ole32")
    end

target("file-rename")
    set_kind("binary")
    set_license("LGPL-2.1")
    add_packages("imgui", "glfw", "glew", "fmt")
    utils_cpp()
    add_files(
        "src/file_rename.cpp",
        "src/img_bg_2.cpp")
    add_defines("IMGUI_IMPL_OPENGL_LOADER_GLEW") -- ImGui用GLEW加载OpenGL
    add_options("locale_cn")
    if is_plat("windows") then
        add_files("file_rename.rc")
        if is_mode("release") then
            add_ldflags("/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup", {force = true})
        end
        add_syslinks("comdlg32", "ole32")
    end
