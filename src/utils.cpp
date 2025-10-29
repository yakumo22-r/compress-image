#include "utils.h"
#include "fmt/base.h"
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

std::string utf8ToAcp(std::string_view src)
{
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, src.data(), -1, nullptr, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), -1, &wstr[0], wlen);
    int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string acpStr(len, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &acpStr[0], len, nullptr, nullptr);
    return std::string(acpStr.c_str());
#endif // _WIN32
}

std::string acpToUtf8(std::string_view src)
{
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_ACP, 0, src.data(), -1, nullptr, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, src.data(), -1, &wstr[0], wlen);
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string acpStr(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &acpStr[0], len, nullptr, nullptr);
    return std::string(acpStr.c_str());
#endif // _WIN32
}

void messageBox(const char* title, const char* msg, int level)
{
#ifdef _WIN32
    UINT type = MB_OK;
    if (level == 0) {
        type |= MB_ICONINFORMATION;
    } else if (level == 1) {
        type |= MB_ICONWARNING;
    } else if (level == 2) {
        type |= MB_ICONERROR;
    } else {
        type |= MB_ICONINFORMATION;
    }
    MessageBoxA(nullptr, utf8ToAcp(msg).c_str(), utf8ToAcp(title).c_str(), type);
#endif // _WIN32
}

std::string filePicker(const std::string& inputPath)
{
    std::string outPath;
#ifdef _WIN32
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    char szFile[260] = {0};
    memcpy(szFile, inputPath.c_str(), inputPath.size());
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL; // Set to your window handle if needed
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "All Files\0*.*\0img Files\0*.png\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (!GetOpenFileName(&ofn)) {
        // messageBox("ERROR", "error when open file picker", 2);
        return std::string();
    }
    outPath = acpToUtf8(ofn.lpstrFile);
#endif // _WIN32:
    return outPath;
}
namespace fs = std::filesystem;
std::string getOutputDirectory(const std::string& pathU8, const std::string& nameU8)
{
    std::string path = utf8ToAcp(pathU8);
    std::string name = utf8ToAcp(nameU8);
    fs::path p(path);
    std::string res = "";
    if (fs::is_directory(p)) {
        res = acpToUtf8((p / name).string().c_str());
    } else if (fs::is_regular_file(p)) {
        auto parent = p.parent_path();
        if (!parent.empty()) {
            res = acpToUtf8((parent / name).string().c_str());
        }  
    }
    fmt::println("auto set output directory {}", res);
    return res;
}

void azureParentDirectory(const char* file)
{
    std::string filePath = utf8ToAcp(file);
    if (filePath.empty()) { return; }
    fs::path fsPath(filePath);
    fs::path parentPath = fsPath.parent_path();
    if (!fs::exists(parentPath)) { fs::create_directories(parentPath); }
}

bool fileExits(const char* file) { return fs::exists(utf8ToAcp(file)); }
