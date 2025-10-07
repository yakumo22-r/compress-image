#include "fmt/base.h"
#include "fmt/format.h"
#include "utils.h"
#include <cstdint>
#ifdef _WIN32

#include "file_drop.h"
#include <vector>
#include <string>
#include <windows.h>
#include <shellapi.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

std::string wideToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return "";

    // Convert wide char to multi-byte (ACP)
    int size = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size == 0) return "";

    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, result.data(), size, nullptr, nullptr);
    return acpToUtf8(result.c_str());
}

class DropTarget : public IDropTarget
{
  public:
    ULONG cRef;
    HWND hwnd;
    std::vector<std::string> dropped_files;
    bool isOver = false;
    bool isDropped = false;
    long pos[2] = {0, 0};

    DropTarget(HWND hwnd) : cRef(1), hwnd(hwnd) {}
    ~DropTarget() {}
    // IUnknown methods
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject)
    {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = 0;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)(void) { return InterlockedIncrement(&cRef); }
    STDMETHOD_(ULONG, Release)(void)
    {
        ULONG cRef = InterlockedDecrement(&this->cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    // IDropTarget methods
    STDMETHOD(DragEnter)(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
    {
        isOver = true;
        POINT pt2 = {pt.x, pt.y};
        ScreenToClient(hwnd, &pt2);
        pos[0] = pt2.x;
        pos[1] = pt2.y;
        fmt::println("DragEnter {} {}", pt2.x, pt2.y);
        // *pdwEffect = CheckArea(pt.x, pt.y) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
    {
        isOver = true;
        POINT pt2 = {pt.x, pt.y};
        ScreenToClient(hwnd, &pt2);
        pos[0] = pt2.x;
        pos[1] = pt2.y;
        // fmt::println("DragOver {} {}", pt2.x, pt2.y);
        // *pdwEffect = CheckArea(pt.x, pt.y) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    STDMETHOD(DragLeave)(void)
    {
        fmt::println("DragLeave");
        isOver = false;
        return S_OK;
    }
    STDMETHOD(Drop)(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
    {
        isOver = false;
        POINT pt2 = {pt.x, pt.y};
        ScreenToClient(hwnd, &pt2);
        pos[0] = pt2.x;
        pos[1] = pt2.y;
        fmt::println("Drop {} {}", pt2.x, pt2.y);
        FORMATETC fmt = {CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM stg = {};
        if (SUCCEEDED(pDataObj->GetData(&fmt, &stg))) {
            HDROP hdrop = (HDROP)GlobalLock(stg.hGlobal);
            UINT count = DragQueryFileA(hdrop, 0xFFFFFFFF, NULL, 0);
            fmt::println("Drop count {}", count);
            dropped_files.clear();
            for (UINT i = 0; i < count; ++i) {
                wchar_t filenameW[MAX_PATH];
                if (DragQueryFileW(hdrop, i, filenameW, MAX_PATH)) {
                    std::string filename = wideToUtf8(filenameW);
                    fmt::println("dropped file: {}", filename);
                    isDropped = true;
                    dropped_files.push_back(filename);
                }
            }
            GlobalUnlock(stg.hGlobal);
            ReleaseStgMedium(&stg);
        }
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }
};

void DropHelper::registerDrop(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd || !IsWindow(hwnd)) {
        fmt::println("Invalid HWND");
        return;
    }

    fmt::println("HWND: {}", (uint64_t)hwnd);
    this->target = new DropTarget(hwnd);
    HRESULT hr = RegisterDragDrop(hwnd, this->target);
    if (FAILED(hr)) {
        DWORD err = GetLastError();
        char errMsg[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errMsg, sizeof(errMsg),
                       NULL);
        std::string msg = fmt::format("RegisterDragDrop failed hr({}) with error code: {}\nmsg: {}", long(hr), err, errMsg);
        messageBox("Internal Error", msg.c_str(), 2);
    }
}

void DropHelper::removeDrop()
{
    if (target) {
        RevokeDragDrop(target->hwnd);
        target->Release();
    }
    target = nullptr;
}

bool DropHelper::isOver()
{
    if (!target) return false;
    return target->isOver;
}
bool DropHelper::isDropped()
{
    if (!target) return false;
    return target->isDropped;
}

DropHelper::Pos DropHelper::getPos()
{
    if (!target) return {0, 0};
    Pos p = {0, 0};
    if (target) {
        p.x = target->pos[0];
        p.y = target->pos[1];
    }
    return p;
}
void DropHelper::reset()
{
    if (!target) return;
    target->isDropped = false;
}

const std::vector<std::string>& DropHelper::getDroppedFiles()
{
    if (!target) {
        static std::vector<std::string> empty;
        return empty;
    }
    return target->dropped_files;
}

#endif // _WIN32
