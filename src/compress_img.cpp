#include <corecrt_io.h>
#include <fmt/format.h>
#include <ole2.h>
#include <vips/vips.h>

#include "fmt/base.h"
#include "compress_img.h"
#include "compress_img_api.h"
#include "compress_img_render.h"
#include "gui.h"
#include "img_bg_1.h"
#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32

namespace app = compress_img;

void f(){}

static void show_error_msg(int error, const char* msg)
{
    auto& r = app::getTextRes();
    std::string message = fmt::format("{}: {}\n {}: {}\n", r.errcodeLable, error, r.msgLable, msg);
    messageBox(r.glfwError, message.c_str(), 2);
}

int main(int argc, char** argv)
{
    app::libVipsInit(argv[0]);

    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr)) {
        fmt::println("OleInitialize failed hr:{}", long(hr));
        return -1;
    }

    int err = GUI::renderInit(app::getTextRes().windowTitle, 480, 600, show_error_msg);
    // app::renderInit();
    if(err){
        messageBox("Error", "renderInit Error", 2);
        return -1;
    }

    GUI::imgBgData = &res_bg1;

    GUI::renderLoop(app::page_start, app::page, app::page_end);
    // app::renderLoop();

    OleUninitialize();
    app::libVipsExit();
    // app::renderExit();
    GUI::renderExit();
    return 0;
}


#include "compress_img.h"

namespace compress_img {

// #define YKM22_LOCALE_CN
TextRes MakeTextRes()
{
    TextRes r;
#ifdef YKM22_LOCALE_CN
    r.windowTitle = "图片压缩 v1.0.0 @yakumo22";
    r.errcodeLable = "错误码";
    r.msgLable = "消息";
    r.glfwError = "GLFW错误";
    r.UIdropArea = "拖拽图片/文件夹到此处";
    r.UIinputPath = "图片/文件夹路径";
    r.UIoutputPath = "输出路径";
    r.UIautoOutputPath = "自动设置输出路径";
    r.UIoutputFormat = "输出格式:";
    r.UIquality = "质量:";
    r.UIRename = "重命名:";
    r.UINameWidth = "重命名宽度:";
    r.UIRun = "运行";
    r.ErrorTitle = "错误";
    r.RunPathError1 = "输入路径不是文件或文件夹";
    r.RunPathError2 = "文件路径必须是png,jpeg,webp";
    r.RunPathError3 = "输出路径不存在且无法创建";
#else
    r.windowTitle = "Image Compress v1.0.0 @yakumo22";
    r.errcodeLable = "Errcode";
    r.msgLable = "Message";
    r.glfwError = "GLFW Error";
    r.UIdropArea = "Drop Image/Directory Here";
    r.UIinputPath = "Image/Directory Path";
    r.UIoutputPath = "Output Path";
    r.UIautoOutputPath = "Auto Set Output Path";
    r.UIoutputFormat = "Output Format:";
    r.UIquality = "Quality:";
    r.UIRename = "Rename:";
    r.UINameWidth = "Name Width:";
    r.UIRun = "Run";
    r.ErrorTitle = "Error";
    r.RunPathError1 = "Input path is not a file or directory";
    r.RunPathError2 = "File path must be png, jpeg or webp";
    r.RunPathError3 = "Output path does not exist and cannot be created";
#endif
    return r;
}

AppContext& getAppContext()
{
    static AppContext ctx;
    return ctx;
}

const TextRes& getTextRes()
{
    static TextRes r = MakeTextRes();
    return r;
}

}
