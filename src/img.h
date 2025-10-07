#ifndef YKM_IMG_H
#define YKM_IMG_H

#include <string>
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

void azureParentDirectory(const char* file);
bool fileExits(const char* file);

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

#endif // !YKM_IMG_H
