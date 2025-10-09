#ifndef YKM22_COMPRESS_IMAGE_H
#define YKM22_COMPRESS_IMAGE_H

#include "compress_img_api.h"
#include <string>
namespace compress_img {

struct AppContext
{
    std::string inputPath;
    std::string outputPath;
    ImgFormat outFormat = ImgFormat::PNG;
    int quality = 90;
    int renameWidth = 0;
    bool rename = false;
};

struct TextRes
{
    const char* windowTitle;
    const char* errcodeLable;
    const char* msgLable;
    const char* glfwError;
    const char* UIdropArea;
    const char* UIinputPath;
    const char* UIoutputPath;
    const char* UIautoOutputPath;
    const char* UIoutputFormat;
    const char* UIquality;
    const char* UIRename;
    const char* UINameWidth;
    const char* UIRun;
    const char* ErrorTitle;
    const char* RunPathError1;
    const char* RunPathError2;
    const char* RunPathError3;
};

AppContext& getAppContext();
const TextRes& getTextRes();
} // namespace compress_img

#endif // !YKM22_COMPRESS_IMAGE_H
