#include <corecrt_io.h>
#include <filesystem>
#include <fmt/format.h>
#include <ole2.h>
#include <string>
#include <vips/vips.h>

#include "fmt/base.h"
#include "img.h"
#include "render.h"
#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32

int main(int argc, char** argv)
{
    libVipsInit(argv[0]);

    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr)) {
        fmt::println("OleInitialize failed hr:{}", long(hr));
        return -1;
    }

    if(renderInit()){
        messageBox("Error", "renderInit Error", 2);
        return -1;
    }

//     CompressInfo info;
//     info.srcFile = "C:/Users/23580/Pictures/Screenshots/文件夹/1213.png";
//     info.outFormat = ImgFormat::JPEG;
//     std::string outFilePath = getImgOutFilePath(info.srcFile, "C:/Users/23580/Pictures/Screenshots/文件夹/压缩", info.outFormat);
//     info.outFile = outFilePath.c_str();
//     fmt::println("outFilePath => {}", info.outFile);
//     info.quality = 90;
//     if(!fileExits(info.srcFile)){
//         fmt::println("file not exist => {}", info.srcFile);
//         return -1;
//     }

//     azureParentDirectory(info.outFile);
//     int errcode = imgCompress(&info);
//     fmt::println("imgCompress errcode:{}", errcode);

    renderLoop();

    OleUninitialize();
    libVipsExit();
    renderExit();
    return 0;
}
