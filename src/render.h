#ifndef YKM_RENDER_H
#define YKM_RENDER_H

void show_error_msg(int error, const char* msg);

int renderInit();
int renderLoop();
int renderExit();

#endif // !YKM_RENDER_H
