#ifndef YKM_UTILS_H
#define YKM_UTILS_H

#include <string>

std::string utf8ToAcp(const char* src);
std::string acpToUtf8(const char* src);
// level: 0-info, 1-warning, 2-error
void messageBox(const char* title, const char* msg, int level);

std::string filePicker(const std::string& inputPath);

std::string getOutputDirectory(const std::string& path, const std::string& name);

void azureParentDirectory(const char* file);
bool fileExits(const char* file);

#endif // !YKM_UTILS_H
