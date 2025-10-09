#ifndef YKM22_IMG_BG_H
#define YKM22_IMG_BG_H

#include <cstddef>

struct ImgData{
    const unsigned char* data;
    size_t size;
};

extern struct GlImgObj;

GlImgObj* createImgBgObject(const unsigned char* imgData, size_t imgSize);
void deleteImgBgObject(GlImgObj* o);
void drawImgBg(GlImgObj* o);

#endif // !YKM22_IMG_BG_H
