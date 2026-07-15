#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/submenu.h>
#include <gui/modules/dialog_ex.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>

// Same contract as applications/services/loader/loader_menu.h. Duplicated
// locally (instead of including the loader-internal header) so this app
// doesn't depend on loader's private headers being exported to the app SDK.
#define MAINMENU_APPS_PATH        INT_PATH(".mainmenu_apps.txt")
#define MAINMENU_APPS_FILE_HEADER "MenuAppList Version 1"

typedef enum {
    MainMenuSettingsViewVarItemList,
    MainMenuSettingsViewAddSubmenu,
    MainMenuSettingsViewResetDialog,
} MainMenuSettingsView;

typedef struct {
    Gui* gui;
    Storage* storage;
    DialogsApp* dialogs;
    ViewDispatcher* view_dispatcher;
    VariableItemList* var_item_list;
    Submenu* add_submenu;
    DialogEx* reset_dialog;

    // Parallel arrays, both heap-owned (strdup'd):
    // - item_names: what's shown to the user.
    // - item_exes: what actually gets written to MAINMENU_APPS_PATH and
    //   matched against FLIPPER_APPS/FLIPPER_EXTERNAL_APPS -- either a
    //   canonical compiled-in app name, or a full "/ext/..." path for a
    //   catalog app found under /ext/apps.
    char** item_names;
    char** item_exes;
    size_t items_count;
    size_t items_capacity;
    size_t selected_item;

    bool modified;
} MainMenuSettingsApp;

int32_t mainmenu_settings_app(void* p);
