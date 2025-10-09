#include "compress_img_render.h"
#include "compress_img.h"
#include "file_drop.h"
#include "fmt/base.h"
#include "fmt/format.h"
#include "compress_img_api.h"
#include "gui.h"
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

namespace compress_img{
ImGuiIO* io = nullptr;
GLFWwindow* window = nullptr;

DropHelper drop;

void page_start()
{
    io = GUI::io;
    window = GUI::window;
    drop.registerDrop(GUI::window);
}

void HoverBrightButton();
void DrawTextInputWithFilePicker(const char* title, std::string& inputText, const char* buttonLabel = "Browse");

void page()
{
    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(getTextRes().windowTitle).x) * 0.5f);
    ImGui::Text("%s", getTextRes().windowTitle);
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.6f, 1.0f)); // Transparent input background
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));  // Transparent button background
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.8f, 0.7f, 1.0f));  // Light gray border
    HoverBrightButton();
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

void page_end()
{
    drop.removeDrop();
}
}
