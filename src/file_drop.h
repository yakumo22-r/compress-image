#ifndef YKM_FILE_DROP_H
#define YKM_FILE_DROP_H

#include "GLFW/glfw3.h"
#include <vector>
#include <string>

extern class DropTarget;

class DropHelper
{
  private:
    DropTarget* target;

  public:
    struct Pos
    {
        long x;
        long y;
    };
    void registerDrop(GLFWwindow* window);
    void removeDrop();
    bool isOver();
    bool isDropped();
    Pos getPos();
    void reset();
    const std::vector<std::string>& getDroppedFiles();
};
#endif // !YKM_FILE_DROP_H
