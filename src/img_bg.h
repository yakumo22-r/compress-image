#ifndef YKM22_IMG_BG_H
#define YKM22_IMG_BG_H

#include <cstddef>
const unsigned char* get_img_bg_data();
size_t get_img_bg_size();

extern struct GlImgObj;

GlImgObj* createImgBgObject();
void deleteImgBgObject(GlImgObj* o);
void drawImgBg(GlImgObj* o);

#endif // !YKM22_IMG_BG_H
