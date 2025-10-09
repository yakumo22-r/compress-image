#include "gui.h"
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "fmt/base.h"
#include "fmt/format.h"
#include "img_bg.h"
#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#endif

const char* glsl_version = "#version 130";

ImGuiIO* GUI::io = nullptr;
GLFWwindow* GUI::window = nullptr;
ImFont* GUI::fontBig = nullptr;
ImgData* GUI::imgBgData = nullptr;

int GUI::renderInit(const char* windowTitle,int width, int height,GLFWerrorfun callback){
    // Assuming GLFW window is created as 'window'
    fmt::println("Starting GUI...");
    glfwSetErrorCallback(callback);
    if (!glfwInit()) return 1;
    fmt::println("GLFW init success");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    io = &ImGui::GetIO();
    io->IniFilename = nullptr;
    (void)io;
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGuiStyle& style = ImGui::GetStyle();
                                     // calling this again)
    fmt::println("ImGui style set");
    window = glfwCreateWindow(width, height, windowTitle, nullptr, nullptr);
    if (window == nullptr) return 1;
    fmt::println("GLFW window created");
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

#ifdef _WIN32
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
    if (hIcon) {
        HWND hwnd = glfwGetWin32Window(window); // Get native Windows handle
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);   // Large icon (window)
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon); // Small icon (taskbar)
        // No need to call DestroyIcon; Windows manages it
    }
#endif

    // init openGL api
    if (glewInit() != GLEW_OK) {
        fmt::println("GLEW init failed!\n");
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    if (fileExits("c:\\Windows\\Fonts\\msyh.ttc")) {
        fontBig = io->Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 24.0f, nullptr, io->Fonts->GetGlyphRangesChineseFull());
    } else {
        fontBig = io->Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttf", 24.0f, nullptr, io->Fonts->GetGlyphRangesChineseFull());
    }

    IM_ASSERT(fontBig != nullptr);
    return 0;

}

void GUI::renderLoop(Callback start, Callback page, Callback end){
    start();
    ImVec4 clear_color = ImVec4(0.05f, 0.05f, 0.10f, 1.00f);
    GlImgObj* bgImg = nullptr;
    if(GUI::imgBgData){
        bgImg = createImgBgObject(imgBgData->data, imgBgData->size);
    }
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // make imgui occupy the entire window
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(io->DisplaySize.x, io->DisplaySize.y));
        ImGui::Begin("##Fullscreen", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.2f);
        ImGui::SetCursorPos({0, 0});
        page();

        ImGui::End();
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        if (bgImg){
            drawImgBg(bgImg);
        } else{
            glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if(bgImg){
        deleteImgBgObject(bgImg);
    }

    end();
}

void GUI::renderExit(){
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}
