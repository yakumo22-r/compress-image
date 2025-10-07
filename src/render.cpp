#include "render.h"
#include "ctx.h"
#include "file_drop.h"
#include "fmt/base.h"
#include "fmt/format.h"
#include "img.h"
#include "img_bg.h"
#include "utils.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <string>
#include <vector>
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

float main_scale = 0;
const char* glsl_version = "#version 130";
ImGuiIO* io = nullptr;
GLFWwindow* window = nullptr;
static std::vector<std::string> dropped_files;
void glfw_drop_callback(GLFWwindow* window, int count, const char** paths);

DropHelper drop;
ImFont* fontBig = nullptr;

int renderInit()
{


    auto& r = getTextRes();
    glfwSetErrorCallback(show_error_msg);
    if (!glfwInit()) return 1;

    // GL 3.0 + GLSL 130
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only

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
    style.ScaleAllSizes(main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style +
                                     // calling this again)
    style.FontScaleDpi =
        main_scale; // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    window = glfwCreateWindow((int)(480), (int)(600), r.windowTitle, nullptr, nullptr);
    if (window == nullptr) return 1;
    // glfwSetDropCallback(window, glfw_drop_callback);
    drop.registerDrop(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

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

void HoverBrightButton();
void DrawTextInputWithFilePicker(const char* title, std::string& inputText, const char* buttonLabel = "Browse");

void Page()
{
    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(getTextRes().windowTitle).x) * 0.5f);
    ImGui::Text("%s", getTextRes().windowTitle);
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.6f, 1.0f)); // Transparent input background
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));  // Transparent button background
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.8f, 0.7f, 1.0f));  // Light gray border
    HoverBrightButton();
    std::string inputPath;
    DrawTextInputWithFilePicker(getTextRes().UIinputPath, getAppContext().inputPath, "~");
    DrawTextInputWithFilePicker(getTextRes().UIoutputPath, getAppContext().outputPath, "~");

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f); // Rounded corners for input and button
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4)); // Adjust padding

    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 240) * 0.5f);
    if (ImGui::Button(getTextRes().UIautoOutputPath, ImVec2(240, 0))) { getAppContext().outputPath = getOutputDirectory(getAppContext().inputPath, "output"); }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Text(getTextRes().UIoutputFormat);
    ImGui::SameLine();
    int* selectedFormat = (int*)&(getAppContext().outFormat);
    if (ImGui::RadioButton("png", selectedFormat, 0)) {}
    ImGui::SameLine();
    if (ImGui::RadioButton("jpeg", selectedFormat, 1)) {}
    ImGui::SameLine();
    if (ImGui::RadioButton("webp", selectedFormat, 2)) {}

    ImGui::PushID("SliderInt");
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Text(getTextRes().UIquality);
    ImGui::SameLine();
    ImGui::SliderInt("##slider", &getAppContext().quality, 0, 100);
    ImGui::PopID();

    ImGui::PushID("RenameLine");
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Text(getTextRes().UIRename);
    ImGui::SameLine();
    ImGui::Checkbox("##rename", &getAppContext().rename);
    if (getAppContext().rename) {
        ImGui::SameLine();
        ImGui::Text(getTextRes().UINameWidth);
        ImGui::SameLine();
        ImGui::PushItemWidth(40);
        ImGui::Text("%d", getAppContext().renameWidth); // Read-only text, synced with slider
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::PushItemWidth(150);
        ImGui::SliderInt("##slider", &getAppContext().renameWidth, 2, 6); // Range 2-6
        ImGui::PopItemWidth();
    }
    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 150) * 0.5f);
    if (ImGui::Button(getTextRes().UIRun, ImVec2(150, 40))) { imgCompressRun(); }

    ImGui::PopStyleColor(3); // Pop all style colors
    ImGui::PopStyleVar(3);   // Pop all style vars
}

bool isInArea(ImVec2 inputPos, ImVec2 pos, ImVec2 size)
{
    return (inputPos.x >= pos.x && inputPos.x <= pos.x + size.x && inputPos.y >= pos.y && inputPos.y <= pos.y + size.y);
}
// Function to draw text input with title and file picker button
void DrawTextInputWithFilePicker(const char* title, std::string& inputText, const char* buttonLabel)
{
    // Draw the title above the input box

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);        // Rounded corners for input and button
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4)); // Adjust padding
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));  // Spacing between elements
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent input background

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::Text("%s", title);

    // Buffer for input text
    char buffer[260];
    strncpy(buffer, inputText.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // Input text field
    ImGui::PushID(title);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 35); // Reserve space for button
    float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::InputTextWithHint("##input", "input file or directory", buffer, sizeof(buffer))) {
        inputText.clear();
        inputText.append(buffer); // Update inputText when user types
    }

    ImGui::PopItemWidth();

    // File picker button
    ImGui::SameLine();
    if (ImGui::Button(buttonLabel, ImVec2(25, 0))) {
        std::string path = filePicker(inputText);
        inputText.clear();
        inputText.append(path);
    }
    ImGui::PopID();
    ImGui::PopStyleColor(1); // Pop all style colors
    ImGui::PopStyleVar(4);   // Pop all style vars
}

void HoverBrightButton()
{
    // Define the region size
    ImVec2 window_size = ImGui::GetWindowSize();
    float vertical_offset = ImGui::GetCursorScreenPos().y;
    ImVec2 region_size(360.0f, 100.0f);

    ImVec2 pos((window_size.x - region_size.x) * 0.5f, vertical_offset + 20);

    bool is_hovered = false;
    if (drop.isOver()) {
        auto mPos = drop.getPos();
        is_hovered = isInArea(ImVec2(mPos.x, mPos.y), pos, region_size);
    }
    if (drop.isDropped()) {
        is_hovered = false;
        auto mPos = drop.getPos();
        if (isInArea(ImVec2(mPos.x, mPos.y), pos, region_size)) {
            getAppContext().inputPath.clear();
            getAppContext().inputPath.append(drop.getDroppedFiles().front());
        }
        drop.reset();
    }
    // Get ImGui draw list to render custom stuff
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Default background color (normal state)
    ImU32 bg_color = ImGui::GetColorU32(ImVec4(0, 0, 0, 0));

    // Brighter background color when hovered
    if (is_hovered) { bg_color = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f)); }

    // Border color (white for visibility)
    ImU32 border_color = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    // Draw rounded rectangle with thick border
    draw_list->AddRectFilled(pos, ImVec2(pos.x + region_size.x, pos.y + region_size.y), bg_color, 10.0f);        // Rounded corners with 10px radius
    draw_list->AddRect(pos, ImVec2(pos.x + region_size.x, pos.y + region_size.y), border_color, 10.0f, 0, 3.0f); // Thick border (3px)

    // Calculate text size and position to center it
    const char* text = getTextRes().UIdropArea;
    ImVec2 text_size = ImGui::CalcTextSize(text);
    ImVec2 text_pos(pos.x + (region_size.x - text_size.x) * 0.5f, pos.y + (region_size.y - text_size.y) * 0.5f);

    // Draw centered text
    draw_list->AddText(text_pos, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), text);
    ImGui::SetCursorPos(ImVec2(0, pos.y + region_size.y));
}

int renderLoop()
{
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.70f, 1.00f);

    GlImgObj* bgImg = createImgBgObject();
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
        Page();

        ImGui::End();
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        // glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        // glClear(GL_COLOR_BUFFER_BIT);
        drawImgBg(bgImg);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    // glDeleteTextures(1, &texture);
    deleteImgBgObject(bgImg);
    return 0;
}
int renderExit()
{
    // Cleanup
    drop.removeDrop();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

void glfw_drop_callback(GLFWwindow* window, int count, const char** paths)
{
    dropped_files.clear();
    fmt::println("glfw_drop_callback count:{}", count);
    for (int i = 0; i < count; i++) {
        fmt::println("dropped file: {}", paths[i]);
        dropped_files.push_back(paths[i]);
    }
}

void show_error_msg(int error, const char* msg)
{
    auto& r = getTextRes();
    std::string message = fmt::format("{}: {}\n {}: {}\n", r.errcodeLable, error, r.msgLable, msg);
    messageBox(r.glfwError, message.c_str(), 2);
}
