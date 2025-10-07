#include "ctx.h"
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
