#ifndef YKM_GUI_H
#define YKM_GUI_H
#include "img_bg.h"
#include <imgui.h>
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

class GUI{
    public:
        using Callback=void(*)();
        static ImGuiIO* io;
        static GLFWwindow* window;
        static ImFont* fontBig;
        static ImgData* imgBgData;
        static int renderInit(const char* windowTitle,int width, int height,GLFWerrorfun callback);
        static void renderLoop(Callback start,Callback page, Callback end);
        static void renderExit();
};
#endif // !YKM_GUI_H
