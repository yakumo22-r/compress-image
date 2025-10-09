#ifndef YKM22_COMPRESS_IMAGE_API_H
#define YKM22_COMPRESS_IMAGE_API_H

#include <string>

namespace compress_img {
enum class ImgFormat
{
    PNG = 0,
    JPEG = 1,
    WEBP = 2,
};

struct CompressInfo
{
    const char* srcFile;
    const char* outFile;
    int quality;
    ImgFormat outFormat;
};

int libVipsInit(const char* name);
void libVipsExit();
int imgCompress(CompressInfo* i);

bool imgCompressRun();

extern class BinImageImpl;
class BinImage
{
  public:
    BinImage(const unsigned char* data, size_t size);
    ~BinImage();
    unsigned char* getData() const { return data; };
    int getWidth() const { return width; };
    int getHeight() const { return height; };

  private:
    int width;
    int height;
    BinImageImpl* impl;
    unsigned char* data;
};
} // namespace compress_img

#endif // !YKM22_COMPRESS_IMAGE_API_H
