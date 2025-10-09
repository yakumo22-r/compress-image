#include "fmt/base.h"
#include "img_bg.h"

#include <cstddef>
#include <gl/glew.h>

using freeImgData = void (*)(void*);
struct TexInfo
{
    int width;
    int height;
    int nrChannels;
    unsigned char* data;
    freeImgData freeFn;
};

#ifdef YKM22_IMG_BG_USE_LIBVIPS
#include "vips/image.h"
void libvipsFreeData(void* ptr) { g_free(ptr); }

#include <vips/vips.h>
TexInfo loadTexByLibvips(const unsigned char* imgData, size_t imgSize)
{
    TexInfo info;
    info.data = nullptr;
    VipsImage* img = vips_image_new_from_buffer(imgData,imgSize, NULL, NULL);
    if (!img) {
        fmt::println("vips_image_new_from_buffer failed");
        return info;
    }
    info.width = img->Xsize;
    info.height = img->Ysize;
    info.nrChannels = img->Bands;

    size_t size;
    void* data_ptr = vips_image_write_to_memory(img, &size);
    if (!data_ptr) {
        fmt::println("vips_image_write_to_memory failed");
        g_object_unref(img);
        return info;
    }

    info.data = static_cast<unsigned char*>(data_ptr);
    info.freeFn = libvipsFreeData;
    g_object_unref(img);
    return info;
}
#else
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
TexInfo loadTexByStbi(const unsigned char* imgData, size_t imgSize)
{
    TexInfo info;
    info.data = stbi_load_from_memory(imgData, (int)imgSize, &info.width, &info.height, &info.nrChannels, 0);
    info.freeFn = stbi_image_free;
    return info;
}
#endif

struct GlImgObj
{
    unsigned int texId;
    unsigned int shaderProgram;
    unsigned int VBO, VAO, EBO;
};

const char* vertexShaderSource = "#version 330 core\n"
                                 "layout (location = 0) in vec3 aPos;\n"
                                 "layout (location = 1) in vec2 aTexCoord;\n"
                                 "out vec2 TexCoord;\n"
                                 "void main() {\n"
                                 "   gl_Position = vec4(aPos, 1.0);\n"
                                 "   TexCoord = aTexCoord;\n"
                                 "}\0";

// Fragment Shader source
const char* fragmentShaderSource = "#version 330 core\n"
                                   "out vec4 FragColor;\n"
                                   "in vec2 TexCoord;\n"
                                   "uniform sampler2D texture1;\n"
                                   "void main() {\n"
                                   "   FragColor = texture(texture1, TexCoord);\n"
                                   "}\0";

// Vertex data for a quad
float vertices[] = {
    // positions        // texture coords
    1.0f,  1.0f,  0.0f, 1.0f, 0.0f, // top right
    1.0f,  -1.0f, 0.0f, 1.0f, 1.0f, // bottom right
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, // bottom left
    -1.0f, 1.0f,  0.0f, 0.0f, 0.0f  // top left
};
unsigned int indices[] = {0, 1, 3, 1, 2, 3};

unsigned int createTexture(const unsigned char* imgData, size_t imgSize)
{
#ifdef YKM22_IMG_BG_USE_LIBVIPS
    TexInfo info = loadTexByLibvips(imgData, imgSize);
#else
    TexInfo info = loadTexByStbi(imgData, imgSize);
#endif
    // unsigned char* data = stbi_load("bg.png", &width, &height, &nrChannels, 0);
    unsigned int texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (info.data) {
        unsigned int format = info.nrChannels == 3 ? GL_RGB : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, format, info.width, info.height, 0, format, GL_UNSIGNED_BYTE, info.data);
        info.freeFn(info.data);
        // glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        printf("Failed to load texture\n");
    }
    return texId;
}

unsigned int createShaderProgram()
{
    // Vertex shader
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // Fragment shader
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // Shader program
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Cleanup
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}

void createVertex(GlImgObj& o)
{
    // Set up VBO, VAO, EBO
    glGenVertexArrays(1, &o.VAO);
    glGenBuffers(1, &o.VBO);
    glGenBuffers(1, &o.EBO);

    glBindVertexArray(o.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, o.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, o.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Texture coord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

GlImgObj* createImgBgObject(const unsigned char* imgData, size_t imgSize)
{
    GlImgObj* o = new GlImgObj();
    o->texId = createTexture(imgData, imgSize);
    o->shaderProgram = createShaderProgram();
    createVertex(*o);
    return o;
}

void deleteImgBgObject(GlImgObj* o)
{
    if (o == nullptr) return;
    glDeleteVertexArrays(1, &o->VAO);
    glDeleteBuffers(1, &o->VBO);
    glDeleteBuffers(1, &o->EBO);
    glDeleteTextures(1, &o->texId);
    glDeleteProgram(o->shaderProgram);
    delete o;
}

void drawImgBg(GlImgObj* o)
{
    if (o == nullptr) return;
    glUseProgram(o->shaderProgram);
    glBindTexture(GL_TEXTURE_2D, o->texId);
    glBindVertexArray(o->VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}
