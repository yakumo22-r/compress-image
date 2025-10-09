#include <corecrt_io.h>
#include <filesystem>
#include <fmt/format.h>
#include <ole2.h>
#include <set>

#include "img_bg_2.h"
#include "file_drop.h"
#include "fmt/base.h"
#include "gui.h"
#include "imgui.h"
#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32

namespace file_rename {
struct TextRes
{
    const char* windowTitle;
    const char* errcodeLable;
    const char* msgLable;
    const char* glfwError;
    const char* UIdropArea;
    const char* UIinputPath;
    const char* UIRestrictionExt;
    const char* UIRestrictionTips;
    const char* UINamePrefix;
    const char* UINameWidth;
    const char* UIExample;
    const char* UIRun;
    const char* ErrorNamme;
    const char* Error1;
} text;
struct Context
{
    std::string inputPath;
    std::string renamePrefix;
    std::string restrictionExtStr;
    int renameWidth;
    bool prefix;
    bool restrictionExt;
} ctx;

void initContext();

void page_start();
void page();
void page_end();

} // namespace file_rename

using namespace file_rename;

static void show_error_msg(int error, const char* msg)
{
    auto& r = text;
    std::string message = fmt::format("{}: {}\n {}: {}\n", r.errcodeLable, error, r.msgLable, msg);
    messageBox(r.glfwError, message.c_str(), 2);
}

int main(int argc, char** argv)
{
    initContext();
    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr)) {
        fmt::println("OleInitialize failed hr:{}", long(hr));
        return -1;
    }

    int err = GUI::renderInit(text.windowTitle, 440, 560, show_error_msg);
    // app::renderInit();
    GUI::imgBgData = &res_bg2; // &res_bg2;
    if(err){
        messageBox("Error", "renderInit Error", 2);
        return -1;
    }

    // GUI::imgBgData = &res_bg1;

    GUI::renderLoop(page_start, page, page_end);

    OleUninitialize();
    GUI::renderExit();
    return 0;
}

namespace file_rename {
void initContext(){
    auto& r = text;
#ifdef YKM22_LOCALE_CN
    r.windowTitle = "批量重命名 v1.0.0 @yakumo22";
    r.errcodeLable = "错误码";
    r.msgLable = "消息";
    r.glfwError = "GLFW错误";
    r.UIdropArea = "拖拽文件夹到此处";
    r.UIinputPath = "输入路径";
    r.UIRestrictionExt = "限制扩展名: ";
    r.UIRestrictionTips = "(例如.png 多扩展名用空格隔开)";
    r.UINamePrefix = "文件名前缀: ";
    r.UINameWidth = "编号宽度: ";
    r.UIExample = "预览: ";
    r.UIRun = "运行";
    r.ErrorNamme = "错误";
    r.Error1 = "输入路径不合法(未填写或是路径有误)";
#else
    r.windowTitle = "Batch Rename v1.0.0 @yakumo22";
    r.errcodeLable = "Errcode";
    r.msgLable = "Message";
    r.glfwError = "GLFW Error";
    r.UIdropArea = "Drop folder here";
    r.UIinputPath = "Input Path";
    r.UIRestrictionExt = "Restrict Extension: ";
    r.UIRestrictionTips = "(e.g.: .png for multiple extensions use space to separate)";
    r.UINamePrefix = "File Name Prefix: ";
    r.UINameWidth = "Number Width: ";
    r.UIExample = "e.g.: ";
    r.UIRun = "Run";
    r.ErrorNamme = "Error";
    r.Error1 = "Input path is invalid (empty or wrong path)";
#endif


}
std::set<std::string> split_by_space(const std::string& input) {
    std::set<std::string> result;
    std::stringstream ss(input);
    std::string word;
    while (ss >> word) {
        result.insert(word); // Insert into set, automatically handles duplicates
    }
    return result;
}

// render page
namespace fs = std::filesystem;
void do_batch_rename(){
    std::string inputPath = utf8ToAcp(ctx.inputPath.c_str());
    if (ctx.inputPath.empty() || !fs::is_directory(fs::path(inputPath))) {
        messageBox(text.ErrorNamme, text.Error1, 2);
        return;
    }
    bool useExt = ctx.restrictionExt;
    std::set<std::string> extSet = split_by_space(ctx.restrictionExtStr);
    for (const auto& ext : extSet) {
        fmt::println("ext: {}", ext);
    }

    int i = 0;
    for (const auto& entry : fs::directory_iterator(inputPath)){
        std::string a = entry.path().string();
        if (fs::is_regular_file(entry)){
        //     fmt::println("entry: {}", entry.path().string());
            std::string oldName = entry.path().string().c_str();
            if (oldName[0] == '.') {
                continue;
            }

            std::string ext = entry.path().extension().string();
            if (useExt && extSet.find(ext) == extSet.end()) {
                continue;
            }
            std::string newName = fmt::format("{}{:0{}d}{}",ctx.prefix ? ctx.renamePrefix : "",i,ctx.renameWidth,acpToUtf8(ext.c_str()));
            newName = utf8ToAcp(newName.c_str());
            i++;

            if(newName != oldName){
                fs::rename(entry.path(), entry.path().parent_path() / newName);
                fmt::println("rename: {} -> {}", acpToUtf8(oldName.c_str()), acpToUtf8(newName.c_str()));
            }
            fmt::println("-----------------------------");
        }
    }
}

void assgin_auto_input_path(const std::string& srcUtf){
    fmt::println("assgin_auto_input_path: {}", srcUtf);
    fs::path p(utf8ToAcp(srcUtf.c_str()));
    if(fs::is_regular_file(p)){
        ctx.inputPath = acpToUtf8(p.parent_path().string().c_str());
    }else{
        ctx.inputPath = acpToUtf8(p.string().c_str());
    }
}

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
void DrawTextInputWithFilePicker(const char* title, std::string& inputText, const char* buttonLabel);
void page()
{
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(text.windowTitle).x) * 0.5f);
    ImGui::Text("%s", text.windowTitle);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.6f, 1.0f)); // Transparent input background
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));  // Transparent button background
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.8f, 0.7f, 1.0f));  // Light gray border
    HoverBrightButton();
    DrawTextInputWithFilePicker(text.UIinputPath, ctx.inputPath, "~");
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f); // Rounded corners for input and button
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4)); // Adjust padding

    ImGui::PushID("RenameLine");

    ImGui::Text(text.UINamePrefix);
    ImGui::SameLine();
    ImGui::Checkbox("##rename", &ctx.prefix);
    if(ctx.prefix){
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent input background
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::SameLine();
        char buffer[100];
        strncpy(buffer, ctx.renamePrefix.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        ImGui::PushItemWidth(200);
        if (ImGui::InputTextWithHint("##input", "file name prefix", buffer, sizeof(buffer))) {
            ctx.renamePrefix.clear();
            ctx.renamePrefix.append(buffer); // Update inputText when user types
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar();
    }

    ImGui::Text(text.UIRestrictionExt);
    ImGui::SameLine();
    ImGui::Checkbox("##RestrictionExt", &ctx.restrictionExt);
    if(ctx.restrictionExt){
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent input background
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        char buffer[100];
        strncpy(buffer, ctx.restrictionExtStr.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        ImGui::PushItemWidth(400);
        if (ImGui::InputTextWithHint("##RestrictionExtInput", ".png .jpg", buffer, sizeof(buffer))) {
            ctx.restrictionExtStr.clear();
            ctx.restrictionExtStr.append(buffer); // Update inputText when user types
        }
        ImGui::PopItemWidth();
        ImGui::TextWrapped(text.UIRestrictionTips);
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar();
    }


    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Text(text.UINameWidth);
    ImGui::SameLine();
    ImGui::PushItemWidth(40);
    ImGui::Text("%d", ctx.renameWidth); // Read-only text, synced with slider
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(200);
    ImGui::SliderInt("##slider", &ctx.renameWidth, 1, 8); // Range 2-6
    ImGui::PopItemWidth();

    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    std::string exampleStr = fmt::format("{}: {}{:0{}d}.file",text.UIExample,ctx.prefix ? ctx.renamePrefix : "",1,ctx.renameWidth);
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(exampleStr.c_str()).x) * 0.5f);
    ImGui::Text("%s", exampleStr.c_str());

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 150) * 0.5f);
    if (ImGui::Button(text.UIRun, ImVec2(150, 40))) { 
        do_batch_rename();
    }


    ImGui::PopStyleColor(3); // Pop all style colors
    ImGui::PopStyleVar(3);   // Pop all style vars

}

void page_end()
{
    drop.removeDrop();
}

bool isInArea(ImVec2 inputPos, ImVec2 pos, ImVec2 size)
{
    return (inputPos.x >= pos.x && inputPos.x <= pos.x + size.x && inputPos.y >= pos.y && inputPos.y <= pos.y + size.y);
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
            // ctx.inputPath.clear();
            // ctx.inputPath.append(drop.getDroppedFiles().front());
            assgin_auto_input_path(drop.getDroppedFiles().front());
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
    const char* text = ::text.UIdropArea;
    ImVec2 text_size = ImGui::CalcTextSize(text);
    ImVec2 text_pos(pos.x + (region_size.x - text_size.x) * 0.5f, pos.y + (region_size.y - text_size.y) * 0.5f);

    // Draw centered text
    draw_list->AddText(text_pos, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), text);
    ImGui::SetCursorPos(ImVec2(0, pos.y + region_size.y));
}


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
        assgin_auto_input_path(path);
    }
    ImGui::PopID();
    ImGui::PopStyleColor(1); // Pop all style colors
    ImGui::PopStyleVar(4);   // Pop all style vars
}
}
