#include "mainmenu_settings_app.h"

#include <applications.h>
#include <toolbox/stream/file_stream.h>
#include <lib/flipper_application/flipper_application.h>
#include <lib/flipper_application/application_manifest.h>
#include <stdlib.h>
#include <string.h>

#define TAG "MainMenuSettings"

typedef enum {
    VarItemListIndexItem,
    VarItemListIndexAddItem,
    VarItemListIndexMoveItem,
    VarItemListIndexRemoveItem,
    VarItemListIndexResetMenu,
} VarItemListIndex;

/* ---- app list model ---- */

static void mainmenu_settings_push(MainMenuSettingsApp* app, const char* name, const char* exe) {
    if(app->items_count == app->items_capacity) {
        app->items_capacity = app->items_capacity ? app->items_capacity * 2 : 16;
        app->item_names = realloc(app->item_names, app->items_capacity * sizeof(char*)); //-V701
        app->item_exes = realloc(app->item_exes, app->items_capacity * sizeof(char*)); //-V701
    }
    app->item_names[app->items_count] = strdup(name);
    app->item_exes[app->items_count] = strdup(exe);
    app->items_count++;
}

static void mainmenu_settings_push_path(MainMenuSettingsApp* app, const char* path) {
    FuriString* fpath = furi_string_alloc_set_str(path);
    FuriString* fname = furi_string_alloc();
    uint8_t icon_buf[FAP_MANIFEST_MAX_ICON_SIZE];
    uint8_t* icon_ptr = icon_buf;

    if(!flipper_application_load_name_and_icon(fpath, app->storage, &icon_ptr, fname)) {
        const char* base = strrchr(path, '/');
        furi_string_set(fname, base ? base + 1 : path);
    }

    mainmenu_settings_push(app, furi_string_get_cstr(fname), path);

    furi_string_free(fpath);
    furi_string_free(fname);
}

static bool mainmenu_settings_find_builtin(const char* name, const char** canonical_name) {
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        if(strcmp(name, FLIPPER_APPS[i].name) == 0) {
            *canonical_name = FLIPPER_APPS[i].name;
            return true;
        }
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(strcmp(name, FLIPPER_EXTERNAL_APPS[i].name) == 0) {
            *canonical_name = FLIPPER_EXTERNAL_APPS[i].name;
            return true;
        }
    }
    return false;
}

static bool mainmenu_settings_is_included(MainMenuSettingsApp* app, const char* exe) {
    for(size_t i = 0; i < app->items_count; i++) {
        if(strcmp(app->item_exes[i], exe) == 0) return true;
    }
    return false;
}

static void mainmenu_settings_clear(MainMenuSettingsApp* app) {
    for(size_t i = 0; i < app->items_count; i++) {
        free(app->item_names[i]);
        free(app->item_exes[i]);
    }
    app->items_count = 0;
}

static void mainmenu_settings_load_defaults(MainMenuSettingsApp* app) {
    mainmenu_settings_clear(app);
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        mainmenu_settings_push(app, FLIPPER_APPS[i].name, FLIPPER_APPS[i].name);
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        mainmenu_settings_push(app, FLIPPER_EXTERNAL_APPS[i].name, FLIPPER_EXTERNAL_APPS[i].name);
    }
}

static void mainmenu_settings_load(MainMenuSettingsApp* app) {
    mainmenu_settings_clear(app);

    Stream* stream = file_stream_alloc(app->storage);
    FuriString* line = furi_string_alloc();
    bool loaded_any = false;

    if(file_stream_open(stream, MAINMENU_APPS_PATH, FSAM_READ, FSOM_OPEN_EXISTING) &&
       stream_read_line(stream, line) &&
       strncmp(
           furi_string_get_cstr(line), "MenuAppList Version", strlen("MenuAppList Version")) ==
           0) {
        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            if(furi_string_empty(line)) continue;

            if(furi_string_start_with(line, "/")) {
                mainmenu_settings_push_path(app, furi_string_get_cstr(line));
                loaded_any = true;
            } else {
                const char* canonical;
                if(mainmenu_settings_find_builtin(furi_string_get_cstr(line), &canonical)) {
                    mainmenu_settings_push(app, canonical, canonical);
                    loaded_any = true;
                }
            }
        }
    }

    furi_string_free(line);
    stream_free(stream);

    if(!loaded_any) {
        mainmenu_settings_load_defaults(app);
    }
}

static void mainmenu_settings_save(MainMenuSettingsApp* app) {
    Stream* stream = file_stream_alloc(app->storage);
    if(file_stream_open(stream, MAINMENU_APPS_PATH, FSAM_READ_WRITE, FSOM_CREATE_ALWAYS)) {
        stream_write_format(stream, "%s\n", MAINMENU_APPS_FILE_HEADER);
        for(size_t i = 0; i < app->items_count; i++) {
            stream_write_format(stream, "%s\n", app->item_exes[i]);
        }
    } else {
        FURI_LOG_E(TAG, "Failed to save %s", MAINMENU_APPS_PATH);
    }
    stream_free(stream);
}

/* ---- UI ---- */

// VariableItemList gives the label ~66px and the value only ~37px (plus
// arrows). The app name goes in the label so it gets the wide slot and
// scrolls if still too long; the narrow slot just holds "n/total".
static void mainmenu_settings_update_item_display(MainMenuSettingsApp* app) {
    VariableItem* item = variable_item_list_get(app->var_item_list, VarItemListIndexItem);
    char value[12];
    if(app->items_count) {
        if(app->selected_item >= app->items_count) app->selected_item = app->items_count - 1;
        variable_item_set_values_count(
            item, (uint8_t)(app->items_count > 255 ? 255 : app->items_count));
        variable_item_set_item_label(item, app->item_names[app->selected_item]);

        snprintf(
            value,
            sizeof(value),
            "%u/%u",
            (unsigned)(app->selected_item + 1),
            (unsigned)app->items_count);
        variable_item_set_current_value_text(item, value);

        variable_item_set_current_value_index(item, (uint8_t)app->selected_item);
    } else {
        app->selected_item = 0;
        variable_item_set_values_count(item, 1);
        variable_item_set_item_label(item, "Item");
        variable_item_set_current_value_text(item, "None");
    }

    VariableItem* move_item = variable_item_list_get(app->var_item_list, VarItemListIndexMoveItem);
    variable_item_set_locked(
        move_item, app->items_count < 2, "Can't move\nwith less\nthan 2 apps!");
}

static void mainmenu_settings_item_changed(VariableItem* item) {
    MainMenuSettingsApp* app = variable_item_get_context(item);
    app->selected_item = variable_item_get_current_value_index(item);

    variable_item_set_item_label(item, app->item_names[app->selected_item]);

    char value[12];
    snprintf(
        value,
        sizeof(value),
        "%u/%u",
        (unsigned)(app->selected_item + 1),
        (unsigned)app->items_count);
    variable_item_set_current_value_text(item, value);
}

static void mainmenu_settings_move_changed(VariableItem* item) {
    MainMenuSettingsApp* app = variable_item_get_context(item);
    uint8_t dir = variable_item_get_current_value_index(item);
    size_t idx = app->selected_item;

    if(app->items_count >= 2) {
        if(dir == 2 && idx != app->items_count - 1) {
            char* tmp_name = app->item_names[idx];
            char* tmp_exe = app->item_exes[idx];
            app->item_names[idx] = app->item_names[idx + 1];
            app->item_exes[idx] = app->item_exes[idx + 1];
            app->item_names[idx + 1] = tmp_name;
            app->item_exes[idx + 1] = tmp_exe;
            app->selected_item++;
            app->modified = true;
        } else if(dir == 0 && idx != 0) {
            char* tmp_name = app->item_names[idx];
            char* tmp_exe = app->item_exes[idx];
            app->item_names[idx] = app->item_names[idx - 1];
            app->item_exes[idx] = app->item_exes[idx - 1];
            app->item_names[idx - 1] = tmp_name;
            app->item_exes[idx - 1] = tmp_exe;
            app->selected_item--;
            app->modified = true;
        }
    }
    variable_item_set_current_value_index(item, 1);
    mainmenu_settings_update_item_display(app);
}

static void mainmenu_settings_clear_add_paths(MainMenuSettingsApp* app) {
    for(size_t i = 0; i < app->add_paths_count; i++) {
        free(app->add_paths[i]);
    }
    app->add_paths_count = 0;
}

// Recursively finds every installed .fap under /ext/apps that isn't already
// in the menu, so it can be listed directly (no separate "browse" step).
static void mainmenu_settings_scan_apps_folder(MainMenuSettingsApp* app) {
    DirWalk* dir_walk = dir_walk_alloc(app->storage);
    dir_walk_set_recursive(dir_walk, true);

    if(dir_walk_open(dir_walk, EXT_PATH("apps"))) {
        FuriString* path = furi_string_alloc();
        FileInfo file_info;
        while(dir_walk_read(dir_walk, path, &file_info) == DirWalkOK) {
            if(file_info.flags & FSF_DIRECTORY) continue;
            if(!furi_string_end_withi(path, ".fap")) continue;

            const char* path_cstr = furi_string_get_cstr(path);
            if(mainmenu_settings_is_included(app, path_cstr)) continue;

            if(app->add_paths_count == app->add_paths_capacity) {
                app->add_paths_capacity = app->add_paths_capacity ? app->add_paths_capacity * 2 :
                                                                     16;
                app->add_paths =
                    realloc(app->add_paths, app->add_paths_capacity * sizeof(char*)); //-V701
            }
            app->add_paths[app->add_paths_count++] = strdup(path_cstr);
        }
        furi_string_free(path);
    }
    dir_walk_close(dir_walk);
    dir_walk_free(dir_walk);
}

static void mainmenu_settings_add_submenu_callback(void* context, uint32_t index) {
    MainMenuSettingsApp* app = context;

    if(index < app->add_paths_count) {
        const char* path = app->add_paths[index];
        FuriString* fpath = furi_string_alloc_set_str(path);
        FuriString* fname = furi_string_alloc();
        uint8_t icon_buf[FAP_MANIFEST_MAX_ICON_SIZE];
        uint8_t* icon_ptr = icon_buf;

        if(flipper_application_load_name_and_icon(fpath, app->storage, &icon_ptr, fname)) {
            mainmenu_settings_push(app, furi_string_get_cstr(fname), path);
        } else {
            const char* base = strrchr(path, '/');
            mainmenu_settings_push(app, base ? base + 1 : path, path);
        }
        furi_string_free(fpath);
        furi_string_free(fname);
    } else {
        const char* name = (const char*)(uintptr_t)index;
        mainmenu_settings_push(app, name, name);
    }

    app->selected_item = app->items_count - 1;
    app->modified = true;

    submenu_reset(app->add_submenu);
    mainmenu_settings_clear_add_paths(app);
    mainmenu_settings_update_item_display(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, MainMenuSettingsViewVarItemList);
}

static void mainmenu_settings_build_add_submenu(MainMenuSettingsApp* app) {
    submenu_reset(app->add_submenu);
    submenu_set_header(app->add_submenu, "Add Menu Item:");

    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        if(!mainmenu_settings_is_included(app, FLIPPER_APPS[i].name)) {
            submenu_add_item(
                app->add_submenu,
                FLIPPER_APPS[i].name,
                (uint32_t)(uintptr_t)FLIPPER_APPS[i].name,
                mainmenu_settings_add_submenu_callback,
                app);
        }
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(!mainmenu_settings_is_included(app, FLIPPER_EXTERNAL_APPS[i].name)) {
            submenu_add_item(
                app->add_submenu,
                FLIPPER_EXTERNAL_APPS[i].name,
                (uint32_t)(uintptr_t)FLIPPER_EXTERNAL_APPS[i].name,
                mainmenu_settings_add_submenu_callback,
                app);
        }
    }

    mainmenu_settings_clear_add_paths(app);
    mainmenu_settings_scan_apps_folder(app);
    for(size_t i = 0; i < app->add_paths_count; i++) {
        FuriString* fpath = furi_string_alloc_set_str(app->add_paths[i]);
        FuriString* fname = furi_string_alloc();
        uint8_t icon_buf[FAP_MANIFEST_MAX_ICON_SIZE];
        uint8_t* icon_ptr = icon_buf;

        if(flipper_application_load_name_and_icon(fpath, app->storage, &icon_ptr, fname)) {
            submenu_add_item(
                app->add_submenu,
                furi_string_get_cstr(fname),
                (uint32_t)i,
                mainmenu_settings_add_submenu_callback,
                app);
        } else {
            const char* base = strrchr(app->add_paths[i], '/');
            submenu_add_item(
                app->add_submenu,
                base ? base + 1 : app->add_paths[i],
                (uint32_t)i,
                mainmenu_settings_add_submenu_callback,
                app);
        }
        furi_string_free(fpath);
        furi_string_free(fname);
    }
}

static void mainmenu_settings_reset_dialog_callback(DialogExResult result, void* context) {
    MainMenuSettingsApp* app = context;
    if(result == DialogExResultRight) {
        storage_common_remove(app->storage, MAINMENU_APPS_PATH);
        mainmenu_settings_load_defaults(app);
        app->selected_item = 0;
        app->modified = false;
        mainmenu_settings_update_item_display(app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, MainMenuSettingsViewVarItemList);
}

static void mainmenu_settings_enter_callback(void* context, uint32_t index) {
    MainMenuSettingsApp* app = context;
    switch(index) {
    case VarItemListIndexRemoveItem:
        if(app->items_count > 0) {
            free(app->item_names[app->selected_item]);
            free(app->item_exes[app->selected_item]);
            for(size_t i = app->selected_item; i + 1 < app->items_count; i++) {
                app->item_names[i] = app->item_names[i + 1];
                app->item_exes[i] = app->item_exes[i + 1];
            }
            app->items_count--;
            app->modified = true;
            mainmenu_settings_update_item_display(app);
        }
        break;
    case VarItemListIndexAddItem:
        mainmenu_settings_build_add_submenu(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, MainMenuSettingsViewAddSubmenu);
        break;
    case VarItemListIndexResetMenu:
        view_dispatcher_switch_to_view(app->view_dispatcher, MainMenuSettingsViewResetDialog);
        break;
    default:
        break;
    }
}

static uint32_t mainmenu_settings_back_to_list(void* context) {
    UNUSED(context);
    return MainMenuSettingsViewVarItemList;
}

static uint32_t mainmenu_settings_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static MainMenuSettingsApp* mainmenu_settings_app_alloc(void) {
    MainMenuSettingsApp* app = malloc(sizeof(MainMenuSettingsApp));
    memset(app, 0, sizeof(MainMenuSettingsApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);

    mainmenu_settings_load(app);

    app->var_item_list = variable_item_list_alloc();
    app->add_submenu = submenu_alloc();
    app->reset_dialog = dialog_ex_alloc();

    variable_item_list_add(
        app->var_item_list,
        "Item",
        app->items_count ? (uint8_t)(app->items_count > 255 ? 255 : app->items_count) : 1,
        mainmenu_settings_item_changed,
        app);

    variable_item_list_add(app->var_item_list, "Add Item", 0, NULL, app);

    VariableItem* move_item = variable_item_list_add(
        app->var_item_list, "Move Item", 3, mainmenu_settings_move_changed, app);
    variable_item_set_current_value_text(move_item, "");
    variable_item_set_current_value_index(move_item, 1);

    variable_item_list_add(app->var_item_list, "Remove Item", 0, NULL, app);
    variable_item_list_add(app->var_item_list, "Reset Menu", 0, NULL, app);

    mainmenu_settings_update_item_display(app);

    variable_item_list_set_enter_callback(
        app->var_item_list, mainmenu_settings_enter_callback, app);

    dialog_ex_set_header(app->reset_dialog, "Reset Menu Items?", 64, 10, AlignCenter, AlignCenter);
    dialog_ex_set_text(
        app->reset_dialog, "Your edits will be lost!", 64, 32, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->reset_dialog, "Cancel");
    dialog_ex_set_right_button_text(app->reset_dialog, "Reset");
    dialog_ex_set_context(app->reset_dialog, app);
    dialog_ex_set_result_callback(app->reset_dialog, mainmenu_settings_reset_dialog_callback);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    View* list_view = variable_item_list_get_view(app->var_item_list);
    view_set_previous_callback(list_view, mainmenu_settings_exit);
    view_dispatcher_add_view(app->view_dispatcher, MainMenuSettingsViewVarItemList, list_view);

    View* add_view = submenu_get_view(app->add_submenu);
    view_set_previous_callback(add_view, mainmenu_settings_back_to_list);
    view_dispatcher_add_view(app->view_dispatcher, MainMenuSettingsViewAddSubmenu, add_view);

    View* reset_view = dialog_ex_get_view(app->reset_dialog);
    view_set_previous_callback(reset_view, mainmenu_settings_back_to_list);
    view_dispatcher_add_view(app->view_dispatcher, MainMenuSettingsViewResetDialog, reset_view);

    view_dispatcher_switch_to_view(app->view_dispatcher, MainMenuSettingsViewVarItemList);

    return app;
}

static void mainmenu_settings_app_free(MainMenuSettingsApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, MainMenuSettingsViewVarItemList);
    view_dispatcher_remove_view(app->view_dispatcher, MainMenuSettingsViewAddSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, MainMenuSettingsViewResetDialog);
    view_dispatcher_free(app->view_dispatcher);

    variable_item_list_free(app->var_item_list);
    submenu_free(app->add_submenu);
    dialog_ex_free(app->reset_dialog);

    mainmenu_settings_clear(app);
    free(app->item_names);
    free(app->item_exes);
    mainmenu_settings_clear_add_paths(app);
    free(app->add_paths);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t mainmenu_settings_app(void* p) {
    UNUSED(p);
    MainMenuSettingsApp* app = mainmenu_settings_app_alloc();

    view_dispatcher_run(app->view_dispatcher);

    if(app->modified) {
        mainmenu_settings_save(app);
    }

    mainmenu_settings_app_free(app);
    return 0;
}
