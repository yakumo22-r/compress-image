#ifndef YKM22_CTX_H
#define YKM22_CTX_H

#include "img.h"
#include <string>
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

#endif // !YKM22_CTX_H
