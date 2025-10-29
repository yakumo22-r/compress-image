#include "compress_img_api.h"
#include "compress_img.h"

#include <corecrt_io.h>
#include <string>
#include <filesystem>

#include <vector>
#include <vips/vips.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <glib-object.h>

namespace fs = std::filesystem;

#include "utils.h"

namespace compress_img {

int libVipsInit(const char* name)
{
    if (VIPS_INIT(name)) { return -1; }
    //     char** suffixes = vips_foreign_get_suffixes();
    //     if(!suffixes){
    //         fmt::println("can't get suffixes");
    //     }

    //     for(int i = 0; suffixes[i] != nullptr; i++){
    //         fmt::println("suffixe {} : {}", i, suffixes[i]);
    //     }
    return 0;
}

void libVipsExit() { vips_shutdown(); }

#define IMG_COMPRESS(format, in, out, quality) vips_##format##save(in, out, "Q", quality, nullptr)

int imgCompress(CompressInfo* i)
{
    VipsImage* in = vips_image_new_from_file(i->srcFile, nullptr);
    if (!in) {
        messageBox("Internal Error", fmt::format("vips_image_new_from_file error {}", vips_error_buffer()).c_str(), 2);
        ;
        return -1;
    }

    int err = -1;
    switch (i->outFormat) {
    case ImgFormat::WEBP:
        err = vips_webpsave(in, i->outFile, "Q", i->quality, nullptr);
        break;
    case ImgFormat::JPEG:
        err = vips_jpegsave(in, i->outFile, "Q", i->quality, "optimize_coding", true, nullptr);
        break;
    default:
        err = vips_pngsave(in, i->outFile, "Q", i->quality, "palette", true, nullptr);
    }

    if (err) {
        messageBox(getTextRes().ErrorTitle, fmt::format("vips image save error {}", vips_error_buffer()).c_str(), 2);
        return -2;
    }

    g_object_unref(in);
    return 0;
}

std::string getSuffixStr(ImgFormat format)
{
    switch (format) {
    case ImgFormat::JPEG:
        return ".jpg";
    case ImgFormat::WEBP:
        return ".webp";
    default:
        return ".png";
    }
}

std::string getImgOutFilePath(std::string_view srcFile, std::string_view _outDir, ImgFormat format)
{
    // fmt::println("getImgOutFilePath srcFile => {}", srcFile);
    // fmt::println("getImgOutFilePath outDir => {}", _outDir);
    fmt::println("getImgOutFilePath TEST => 0");
    fs::path srcPath(srcFile);
    fs::path outDir(_outDir);
    fmt::println("getImgOutFilePath TEST => 1");
    fs::path fileName = srcPath.stem();
    std::string suffix = getSuffixStr(format);
    std::string outPath = acpToUtf8((outDir / fileName).string() + suffix);
    fmt::println("getImgOutFilePath TEST DONE => {}", outPath);
    return outPath;
}

bool imgCompressRun()
{
    auto& ctx = getAppContext();
    auto text = getTextRes();
    std::string inputPath = utf8ToAcp(ctx.inputPath);
    std::string outputPath = utf8ToAcp(ctx.outputPath);
    fmt::println("imgCompressRun Test 1");
    fmt::println("imgCompressRun inputPath: {}", ctx.inputPath);
    fmt::println("imgCompressRun outputPath: {}", ctx.outputPath);
    std::vector<std::string> inputFiles;

    try {
        fmt::println("imgCompressRun create_directories CALL");
        fs::create_directories(outputPath);
        fmt::println("imgCompressRun create_directories CALLOVER");
    } catch (std::exception& e) {
        std::string msg = fmt::format("{}\nmsg: {}", text.RunPathError3, e.what());
        messageBox(text.ErrorTitle, msg.c_str(), 2);
        return false;
    }

    fs::path inPath(inputPath);
    if (fs::is_directory(inPath)) {
        for (auto& entry : fs::directory_iterator(inPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".png" || ext == ".jpeg" || ext == ".jpg" || ext == ".webp") { inputFiles.push_back(entry.path().string()); }
            }
        }
    } else if (fs::is_regular_file(inPath)) {
        std::string ext = inPath.extension().string();
        if (ext == ".png" || ext == ".jpeg" || ext == ".jpg" || ext == ".webp") {
            inputFiles.push_back(inPath.string());
        } else {
            messageBox(getTextRes().ErrorTitle, text.RunPathError2, 2);
            return false;
        }
    } else {
        messageBox(getTextRes().ErrorTitle, text.RunPathError1, 2);
        return false;
    }

    CompressInfo info;
    info.outFormat = ctx.outFormat;
    info.quality = ctx.quality;
    std::string inputPathStr;
    inputPathStr.reserve(260);
    std::string outPathStr;
    outPathStr.reserve(260);
    std::string outputPathU8 = acpToUtf8(outputPath.c_str());
    int count = 0;
    for (auto& file : inputFiles) {
        inputPathStr.clear();
        inputPathStr.append(acpToUtf8(file.c_str()));
        info.srcFile = inputPathStr.c_str();

        outPathStr.clear();
        if (ctx.rename) {
            outPathStr.append(outputPathU8);
            outPathStr.append("/");
            outPathStr.append(fmt::format("{:0{}d}", count, ctx.renameWidth));
            outPathStr.append(getSuffixStr(info.outFormat));
        } else {
            outPathStr.append(getImgOutFilePath(file, outputPath.c_str(), info.outFormat));
        }


        info.outFile = outPathStr.c_str();
        if (imgCompress(&info)) { return -1; }
        count++;
    }

    return true;
}

class BinImageImpl
{
  public:
    VipsImage* image;
    VipsImage* out;
};

BinImage::BinImage(const unsigned char* data, size_t size)
{
    impl = new BinImageImpl();
    impl->image = vips_image_new_from_buffer(data, size, nullptr, nullptr);
    if (!impl->image) {
        fmt::println("vips_image_new_from_buffer error: {}", vips_error_buffer());
        return;
    }
    width = vips_image_get_width(impl->image);
    height = vips_image_get_height(impl->image);
    if (vips_copy(impl->image, &impl->out, "format", VIPS_FORMAT_UCHAR, "bands", 4, nullptr)) {
        g_object_unref(impl->image);
        return;
    }
    // Get pixel data (RGBA)
    this->data = (unsigned char*)vips_image_get_data(impl->out);
    if (!this->data) {
        g_object_unref(impl->out);
        g_object_unref(impl->image);
        return;
    }
}

BinImage::~BinImage()
{
    if (impl->out) { g_object_unref(impl->out); }
    if (impl->image) { g_object_unref(impl->image); }
    delete impl;
}
}
