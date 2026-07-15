#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/menu.h>
#include <gui/modules/submenu.h>
#include <assets_icons.h>
#include <applications.h>
#include <archive/helpers/archive_favorites.h>
#include <storage/storage.h>
#include <toolbox/stream/file_stream.h>
#include <core/dangerous_defines.h>
#include <gui/icon_i.h>
#include <lib/flipper_application/flipper_application.h>
#include <lib/flipper_application/application_manifest.h>

#include "loader.h"
#include "loader_menu.h"

#define TAG "LoaderMenu"

struct LoaderMenu {
    FuriThread* thread;
    void (*closed_cb)(void*);
    void* context;
};

static int32_t loader_menu_thread(void* p);

LoaderMenu* loader_menu_alloc(void (*closed_cb)(void*), void* context) {
    LoaderMenu* loader_menu = malloc(sizeof(LoaderMenu));
    loader_menu->closed_cb = closed_cb;
    loader_menu->context = context;
    loader_menu->thread = furi_thread_alloc_ex(TAG, 1024, loader_menu_thread, loader_menu);
    furi_thread_start(loader_menu->thread);
    return loader_menu;
}

void loader_menu_free(LoaderMenu* loader_menu) {
    furi_assert(loader_menu);
    furi_thread_join(loader_menu->thread);
    furi_thread_free(loader_menu->thread);
    free(loader_menu);
}

typedef enum {
    LoaderMenuViewPrimary,
    LoaderMenuViewSettings,
} LoaderMenuView;

// A menu entry built from a "/ext/..." path in the user-customized menu file
// (rather than a compiled-in FLIPPER_APPS/FLIPPER_EXTERNAL_APPS entry). Its
// name/icon/path are all heap-allocated and must outlive the Menu widget, so
// they're tracked here and freed together in loader_menu_app_free.
typedef struct {
    char* name;
    const Icon* icon;
    bool icon_owned;
    char* path;
} LoaderMenuDynamicEntry;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Menu* primary_menu;
    Submenu* settings_menu;

    LoaderMenuDynamicEntry* dynamic_entries;
    size_t dynamic_count;
    size_t dynamic_capacity;
} LoaderMenuApp;

static void loader_menu_start(const char* name) {
    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_start_with_gui_error(loader, name, NULL);
    furi_record_close(RECORD_LOADER);
}

// Combined index space: [0, FLIPPER_APPS_COUNT) are FLIPPER_APPS,
// [FLIPPER_APPS_COUNT, FLIPPER_APPS_COUNT + FLIPPER_EXTERNAL_APPS_COUNT) are
// FLIPPER_EXTERNAL_APPS. Lets a user-customized, freely-ordered/mixed menu
// use a single callback instead of one per source array.
static void loader_menu_combined_callback(void* context, uint32_t index) {
    UNUSED(context);
    const char* name = (index < FLIPPER_APPS_COUNT) ?
                            FLIPPER_APPS[index].name :
                            FLIPPER_EXTERNAL_APPS[index - FLIPPER_APPS_COUNT].name;
    loader_menu_start(name);
}

// Looks up a compiled-in app by name and returns its icon plus combined index
// (see loader_menu_combined_callback). Used to resolve entries from the
// user-customized menu file back to their compiled icon/callback index.
static bool
    loader_menu_find_app(const char* name, const Icon** icon, uint32_t* combined_index) {
    for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
        if(strcmp(name, FLIPPER_APPS[i].name) == 0) {
            *icon = FLIPPER_APPS[i].icon;
            *combined_index = i;
            return true;
        }
    }
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(strcmp(name, FLIPPER_EXTERNAL_APPS[i].name) == 0) {
            *icon = FLIPPER_EXTERNAL_APPS[i].icon;
            *combined_index = FLIPPER_APPS_COUNT + i;
            return true;
        }
    }
    return false;
}

static void loader_menu_path_callback(void* context, uint32_t index) {
    UNUSED(index);
    loader_menu_start((const char*)context);
}

static LoaderMenuDynamicEntry* loader_menu_dynamic_push(LoaderMenuApp* app) {
    if(app->dynamic_count == app->dynamic_capacity) {
        app->dynamic_capacity = app->dynamic_capacity ? app->dynamic_capacity * 2 : 8;
        app->dynamic_entries = realloc(
            app->dynamic_entries, app->dynamic_capacity * sizeof(LoaderMenuDynamicEntry)); //-V701
    }
    return &app->dynamic_entries[app->dynamic_count++];
}

// Loads name + icon for an arbitrary .fap path (a menu entry added via the
// "Main Menu" settings app's file browser). Falls back to the filename and a
// generic icon if the file isn't a valid/inspectable FAP.
static void loader_menu_add_path_entry(LoaderMenuApp* app, Storage* storage, const char* path) {
    LoaderMenuDynamicEntry* entry = loader_menu_dynamic_push(app);
    entry->path = strdup(path);

    FuriString* fpath = furi_string_alloc_set_str(path);
    FuriString* fname = furi_string_alloc();
    uint8_t* icon_buf = malloc(FAP_MANIFEST_MAX_ICON_SIZE);

    if(flipper_application_load_name_and_icon(fpath, storage, &icon_buf, fname)) {
        Icon* icon = malloc(sizeof(Icon));
        FURI_CONST_ASSIGN(icon->frame_count, 1);
        FURI_CONST_ASSIGN(icon->frame_rate, 1);
        FURI_CONST_ASSIGN(icon->width, 10);
        FURI_CONST_ASSIGN(icon->height, 10);
        FURI_CONST_ASSIGN_PTR(icon->frames, malloc(sizeof(const uint8_t*)));
        FURI_CONST_ASSIGN_PTR(icon->frames[0], icon_buf);
        entry->icon = icon;
        entry->icon_owned = true;
        entry->name = strdup(furi_string_get_cstr(fname));
    } else {
        free(icon_buf);
        entry->icon = &I_unknown_10px;
        entry->icon_owned = false;
        const char* base = strrchr(path, '/');
        entry->name = strdup(base ? base + 1 : path);
    }

    furi_string_free(fpath);
    furi_string_free(fname);

    menu_add_item(
        app->primary_menu, entry->name, entry->icon, 0, loader_menu_path_callback, entry->path);
}

// Tries to populate the primary menu from a user-customized app list
// (written by the "Main Menu" settings app). Returns false if no valid
// customization file exists, so the caller can fall back to the default.
static bool loader_menu_build_menu_custom(LoaderMenuApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();
    bool loaded_any = false;

    if(file_stream_open(stream, MAINMENU_APPS_PATH, FSAM_READ, FSOM_OPEN_EXISTING) &&
       stream_read_line(stream, line) &&
       strncmp(
           furi_string_get_cstr(line), MAINMENU_APPS_FILE_HEADER, strlen("MenuAppList Version")) ==
           0) {
        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            if(furi_string_empty(line)) continue;

            if(furi_string_start_with(line, "/")) {
                loader_menu_add_path_entry(app, storage, furi_string_get_cstr(line));
                loaded_any = true;
            } else {
                const Icon* icon;
                uint32_t combined_index;
                if(loader_menu_find_app(furi_string_get_cstr(line), &icon, &combined_index)) {
                    const char* name =
                        (combined_index < FLIPPER_APPS_COUNT) ?
                            FLIPPER_APPS[combined_index].name :
                            FLIPPER_EXTERNAL_APPS[combined_index - FLIPPER_APPS_COUNT].name;
                    menu_add_item(
                        app->primary_menu,
                        name,
                        icon,
                        combined_index,
                        loader_menu_combined_callback,
                        NULL);
                    loaded_any = true;
                }
            }
        }
    }

    furi_string_free(line);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return loaded_any;
}

static void loader_menu_applications_callback(void* context, uint32_t index) {
    UNUSED(index);
    UNUSED(context);
    const char* name = LOADER_APPLICATIONS_NAME;
    loader_menu_start(name);
}

static void
    loader_menu_settings_menu_callback(void* context, InputType input_type, uint32_t index) {
    UNUSED(context);
    if(input_type == InputTypeShort) {
        loader_menu_start((const char*)index);
    } else if(input_type == InputTypeLong) {
        archive_favorites_handle_setting_pin_unpin((const char*)index, NULL);
    }
}

static void loader_menu_switch_to_settings(void* context, uint32_t index) {
    UNUSED(index);
    LoaderMenuApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewSettings);
}

static uint32_t loader_menu_switch_to_primary(void* context) {
    UNUSED(context);
    return LoaderMenuViewPrimary;
}

static uint32_t loader_menu_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void loader_menu_build_menu(LoaderMenuApp* app, LoaderMenu* menu) {
    size_t i = 0;

    menu_add_item(
        app->primary_menu,
        LOADER_APPLICATIONS_NAME,
        &A_Plugins_14,
        i++,
        loader_menu_applications_callback,
        (void*)menu);

    if(!loader_menu_build_menu_custom(app)) {
        for(i = 0; i < FLIPPER_APPS_COUNT; i++) {
            menu_add_item(
                app->primary_menu,
                FLIPPER_APPS[i].name,
                FLIPPER_APPS[i].icon,
                i,
                loader_menu_combined_callback,
                NULL);
        }

        for(i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
            menu_add_item(
                app->primary_menu,
                FLIPPER_EXTERNAL_APPS[i].name,
                FLIPPER_EXTERNAL_APPS[i].icon,
                FLIPPER_APPS_COUNT + i,
                loader_menu_combined_callback,
                NULL);
        }
    }

    menu_add_item(
        app->primary_menu, "Settings", &A_Settings_14, i++, loader_menu_switch_to_settings, app);
}

static void loader_menu_build_submenu(LoaderMenuApp* app, LoaderMenu* loader_menu) {
    for(size_t i = 0; i < FLIPPER_EXTSETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            app->settings_menu,
            FLIPPER_EXTSETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_EXTSETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
    for(size_t i = 0; i < FLIPPER_SETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            app->settings_menu,
            FLIPPER_SETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_SETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
}

static LoaderMenuApp* loader_menu_app_alloc(LoaderMenu* loader_menu) {
    LoaderMenuApp* app = malloc(sizeof(LoaderMenuApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->primary_menu = menu_alloc();
    app->settings_menu = submenu_alloc();
    app->dynamic_entries = NULL;
    app->dynamic_count = 0;
    app->dynamic_capacity = 0;

    loader_menu_build_menu(app, loader_menu);
    loader_menu_build_submenu(app, loader_menu);

    // Primary menu
    View* primary_view = menu_get_view(app->primary_menu);
    view_set_context(primary_view, app->primary_menu);
    view_set_previous_callback(primary_view, loader_menu_exit);
    view_dispatcher_add_view(app->view_dispatcher, LoaderMenuViewPrimary, primary_view);

    // Settings menu
    View* settings_view = submenu_get_view(app->settings_menu);
    view_set_context(settings_view, app->settings_menu);
    view_set_previous_callback(settings_view, loader_menu_switch_to_primary);
    view_dispatcher_add_view(app->view_dispatcher, LoaderMenuViewSettings, settings_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewPrimary);

    return app;
}

static void loader_menu_app_free(LoaderMenuApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, LoaderMenuViewPrimary);
    view_dispatcher_remove_view(app->view_dispatcher, LoaderMenuViewSettings);
    view_dispatcher_free(app->view_dispatcher);

    menu_free(app->primary_menu);
    submenu_free(app->settings_menu);

    for(size_t i = 0; i < app->dynamic_count; i++) {
        LoaderMenuDynamicEntry* entry = &app->dynamic_entries[i];
        free(entry->name);
        free(entry->path);
        if(entry->icon_owned) {
            free((void*)entry->icon->frames[0]);
            free((void*)entry->icon->frames);
            free((void*)entry->icon);
        }
    }
    free(app->dynamic_entries);

    furi_record_close(RECORD_GUI);
    free(app);
}

static int32_t loader_menu_thread(void* p) {
    LoaderMenu* loader_menu = p;
    furi_assert(loader_menu);

    LoaderMenuApp* app = loader_menu_app_alloc(loader_menu);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_run(app->view_dispatcher);

    if(loader_menu->closed_cb) {
        loader_menu->closed_cb(loader_menu->context);
    }

    loader_menu_app_free(app);

    return 0;
}
