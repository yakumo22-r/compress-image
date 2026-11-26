package("ykm-libwebp")
    set_license("BSD-3-Clause")
    add_urls("https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-$(version).tar.gz")
    add_versions("1.6.0", "e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564")
    add_deps("cmake")
    on_load(function (package)
        local prefix = package:is_plat("windows") and "lib" or ""
        package:add("links", prefix .. "webp", prefix .. "sharpyuv")
    end)
    on_install(function (package)
        local configs = {"-DBUILD_SHARED_LIBS=OFF", "-DWEBP_BUILD_LIBWEBPMUX=OFF", "-DWEBP_BUILD_LIBWEBPDEMUX=OFF"}
        for _, tool in ipairs({"ANIM_UTILS", "CWEBP", "DWEBP", "GIF2WEBP", "IMG2WEBP", "VWEBP", "WEBPINFO", "WEBPMUX", "EXTRAS"}) do
            table.insert(configs, "-DWEBP_BUILD_" .. tool .. "=OFF")
        end
        import("package.tools.cmake").install(package, configs)
    end)
package_end()
