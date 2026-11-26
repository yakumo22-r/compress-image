#ifndef YKM_EMBEDDED_APP_H
#define YKM_EMBEDDED_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YkmEmbeddedModule {
    const char* name;
    const unsigned char* data;
    uint32_t size;
} YkmEmbeddedModule;

typedef struct YkmEmbeddedSession {
    const char* name;
    const char* role;
    const char* module;
    uint32_t autostart;
} YkmEmbeddedSession;

typedef struct YkmEmbeddedAsset {
    const char* name;
    const unsigned char* data;
    uint32_t size;
} YkmEmbeddedAsset;

typedef struct YkmEmbeddedApp {
    uint32_t struct_size;
    const char* id;
    const char* name;
    const YkmEmbeddedModule* modules;
    uint32_t module_count;
    const YkmEmbeddedSession* sessions;
    uint32_t session_count;
    const YkmEmbeddedAsset* assets;
    uint32_t asset_count;
    const char* const* permissions;
    uint32_t permission_count;
    const char* version;
    const char* repository;
} YkmEmbeddedApp;

const YkmEmbeddedApp* ykm_get_embedded_app(void);

#ifdef __cplusplus
}
#endif

#endif
