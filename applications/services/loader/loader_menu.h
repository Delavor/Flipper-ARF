#pragma once
#include <furi.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

// Optional user-customized main menu app list (order + visible subset).
// One app name per line, matching FLIPPER_APPS[]/FLIPPER_EXTERNAL_APPS[] names.
// If this file is absent, the menu falls back to the full compiled-in list.
#define MAINMENU_APPS_PATH        INT_PATH(".mainmenu_apps.txt")
#define MAINMENU_APPS_FILE_HEADER "MenuAppList Version 1"

typedef struct LoaderMenu LoaderMenu;

LoaderMenu* loader_menu_alloc(void (*closed_cb)(void*), void* context);

void loader_menu_free(LoaderMenu* loader_menu);

#ifdef __cplusplus
}
#endif
