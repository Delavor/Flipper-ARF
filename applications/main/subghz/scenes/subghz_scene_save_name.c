#include "../subghz_i.h"
#include "subghz/types.h"
#include "../helpers/subghz_custom_event.h"
#include <lib/subghz/protocols/raw.h>
#include <gui/modules/validators.h>
#include <dolphin/dolphin.h>
#include <toolbox/name_generator.h>
#include <lib/toolbox/dir_walk.h>
#include <toolbox/strint.h>
#include <string.h>

typedef struct {
    FuriString* protocol;
    FuriString* manufacture;
    bool has_manufacture;
    uint32_t serial;
    bool has_serial;
    uint32_t btn;
    bool has_btn;
    uint8_t key[sizeof(uint64_t)];
    bool has_key;
} SubGhzSaveNameSignalSignature;

static void subghz_scene_save_name_signature_init(SubGhzSaveNameSignalSignature* signature) {
    signature->protocol = furi_string_alloc();
    signature->manufacture = furi_string_alloc();
    signature->has_manufacture = false;
    signature->serial = 0;
    signature->has_serial = false;
    signature->btn = 0;
    signature->has_btn = false;
    memset(signature->key, 0, sizeof(signature->key));
    signature->has_key = false;
}

static void subghz_scene_save_name_signature_free(SubGhzSaveNameSignalSignature* signature) {
    furi_string_free(signature->protocol);
    furi_string_free(signature->manufacture);
}

static bool
    subghz_scene_save_name_read_string(FlipperFormat* fff, const char* key, FuriString* value) {
    flipper_format_rewind(fff);
    bool result = flipper_format_read_string(fff, key, value);
    if(result) {
        furi_string_trim(value);
    }
    return result;
}

static bool subghz_scene_save_name_parse_u32(const char* value, uint32_t* out) {
    char* end = NULL;
    uint32_t parsed = 0;

    if(strint_to_uint32(value, &end, &parsed, 10) == StrintParseNoError && end && *end == '\0') {
        *out = parsed;
        return true;
    }

    end = NULL;
    if(strint_to_uint32(value, &end, &parsed, 16) == StrintParseNoError && end && *end == '\0') {
        *out = parsed;
        return true;
    }

    return false;
}

static bool subghz_scene_save_name_read_u32(FlipperFormat* fff, const char* key, uint32_t* value) {
    flipper_format_rewind(fff);
    if(flipper_format_read_uint32(fff, key, value, 1)) {
        return true;
    }

    FuriString* value_str = furi_string_alloc();
    bool result = false;
    if(subghz_scene_save_name_read_string(fff, key, value_str)) {
        result = subghz_scene_save_name_parse_u32(furi_string_get_cstr(value_str), value);
    }
    furi_string_free(value_str);
    return result;
}

static bool subghz_scene_save_name_signature_read(
    FlipperFormat* fff,
    SubGhzSaveNameSignalSignature* signature) {
    if(!subghz_scene_save_name_read_string(fff, "Protocol", signature->protocol)) {
        return false;
    }

    if(furi_string_equal_str(signature->protocol, "RAW")) {
        return false;
    }

    signature->has_manufacture =
        subghz_scene_save_name_read_string(fff, "Manufacture", signature->manufacture);
    signature->has_serial = subghz_scene_save_name_read_u32(fff, "Serial", &signature->serial);
    signature->has_btn = subghz_scene_save_name_read_u32(fff, "Btn", &signature->btn);

    flipper_format_rewind(fff);
    signature->has_key = flipper_format_read_hex(fff, "Key", signature->key, sizeof(signature->key));

    return signature->has_serial || signature->has_key;
}

static bool subghz_scene_save_name_signatures_match(
    const SubGhzSaveNameSignalSignature* saved,
    const SubGhzSaveNameSignalSignature* captured) {
    if(!furi_string_equal(saved->protocol, captured->protocol)) {
        return false;
    }

    if(saved->has_manufacture && captured->has_manufacture &&
       !furi_string_equal(saved->manufacture, captured->manufacture)) {
        return false;
    }

    if(saved->has_serial && captured->has_serial) {
        if(saved->serial != captured->serial) {
            return false;
        }
        if(saved->has_btn && captured->has_btn && saved->btn != captured->btn) {
            return false;
        }
        if(saved->serial != 0) {
            return true;
        }
    }

    return saved->has_key && captured->has_key &&
           memcmp(saved->key, captured->key, sizeof(saved->key)) == 0;
}

static bool subghz_scene_save_name_file_filter(const char* name, FileInfo* fileinfo, void* ctx) {
    UNUSED(ctx);
    if(file_info_is_dir(fileinfo)) return false;

    const char* ext = strrchr(name, '.');
    return ext && !strcmp(ext, SUBGHZ_APP_FILENAME_EXTENSION);
}

static bool subghz_scene_save_name_find_duplicate(
    FlipperFormat* captured,
    const char* target_path,
    FuriString* duplicate_name) {
    SubGhzSaveNameSignalSignature captured_signature;
    subghz_scene_save_name_signature_init(&captured_signature);
    bool found = false;

    if(!subghz_scene_save_name_signature_read(captured, &captured_signature)) {
        subghz_scene_save_name_signature_free(&captured_signature);
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    DirWalk* dir_walk = dir_walk_alloc(storage);
    FlipperFormat* saved_fff = flipper_format_file_alloc(storage);
    FuriString* path = furi_string_alloc();
    FileInfo fileinfo;

    dir_walk_set_recursive(dir_walk, true);
    dir_walk_set_filter_cb(dir_walk, subghz_scene_save_name_file_filter, NULL);
    if(dir_walk_open(dir_walk, SUBGHZ_APP_FOLDER)) {
        while(dir_walk_read(dir_walk, path, &fileinfo) == DirWalkOK) {
            if(file_info_is_dir(&fileinfo)) continue;
            if(target_path && !strcmp(furi_string_get_cstr(path), target_path)) continue;

            if(flipper_format_file_open_existing(saved_fff, furi_string_get_cstr(path))) {
                SubGhzSaveNameSignalSignature saved_signature;
                subghz_scene_save_name_signature_init(&saved_signature);
                if(subghz_scene_save_name_signature_read(saved_fff, &saved_signature) &&
                   subghz_scene_save_name_signatures_match(
                       &saved_signature, &captured_signature)) {
                    path_extract_filename(path, duplicate_name, true);
                    found = true;
                }
                subghz_scene_save_name_signature_free(&saved_signature);
                flipper_format_file_close(saved_fff);

                if(found) break;
            }
        }
        dir_walk_close(dir_walk);
    }

    furi_string_free(path);
    flipper_format_free(saved_fff);
    dir_walk_free(dir_walk);
    furi_record_close(RECORD_STORAGE);
    subghz_scene_save_name_signature_free(&captured_signature);

    return found;
}

static void subghz_scene_save_name_set_duplicate_message(
    SubGhz* subghz,
    FlipperFormat* signal_data) {
    FuriString* duplicate_name = furi_string_alloc();

    furi_string_reset(subghz->error_str);
    if(signal_data && subghz_scene_save_name_find_duplicate(
           signal_data, furi_string_get_cstr(subghz->file_path), duplicate_name)) {
        furi_string_printf(
            subghz->error_str,
            "Same signal\nsaved in file\n%s",
            furi_string_get_cstr(duplicate_name));
    }

    furi_string_free(duplicate_name);
}

void subghz_scene_save_name_text_input_callback(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventSceneSaveName);
}

void subghz_scene_save_name_on_enter(void* context) {
    SubGhz* subghz = context;

    // Setup view
    TextInput* text_input = subghz->text_input;
    bool dev_name_empty = false;

    FuriString* file_name = furi_string_alloc();
    FuriString* dir_name = furi_string_alloc();

    char file_name_buf[SUBGHZ_MAX_LEN_NAME] = {0};
    DateTime* datetime = subghz->save_datetime_set ? &subghz->save_datetime : NULL;
    subghz->save_datetime_set = false;
    if(!subghz_path_is_file(subghz->file_path)) {
        SubGhzProtocolDecoderBase* decoder_result = subghz_txrx_get_decoder(subghz->txrx);

        bool skip_dec_is_present = false;
        if(decoder_result != 0x0) {
            if(decoder_result != NULL) {
                if(strlen(decoder_result->protocol->name) != 0 &&
                   subghz->last_settings->protocol_file_names) {
                    if(!scene_manager_has_previous_scene(
                           subghz->scene_manager, SubGhzSceneSetType)) {
                        name_generator_make_auto_datetime(
                            file_name_buf,
                            SUBGHZ_MAX_LEN_NAME,
                            decoder_result->protocol->name,
                            datetime);
                        skip_dec_is_present = true;
                    }
                }
            }
        }
        if(!skip_dec_is_present) {
            name_generator_make_auto_datetime(file_name_buf, SUBGHZ_MAX_LEN_NAME, NULL, datetime);
        }
        furi_string_set(file_name, file_name_buf);
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        //highlighting the entire filename by default
        dev_name_empty = true;
    } else {
        furi_string_reset(subghz->file_path_tmp);
        furi_string_set(subghz->file_path_tmp, subghz->file_path);
        path_extract_dirname(furi_string_get_cstr(subghz->file_path), dir_name);
        path_extract_filename(subghz->file_path, file_name, true);
        if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) !=
           SubGhzCustomEventManagerNoSet) {
            if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) ==
               SubGhzCustomEventManagerSetRAW) {
                dev_name_empty = true;
                name_generator_make_auto_datetime(
                    file_name_buf, SUBGHZ_MAX_LEN_NAME, "RAW", datetime);
                furi_string_set(file_name, file_name_buf);
            }
        }
        furi_string_set(subghz->file_path, dir_name);
    }

    strncpy(subghz->file_name_tmp, furi_string_get_cstr(file_name), SUBGHZ_MAX_LEN_NAME);
    text_input_set_header_text(text_input, "Name signal");
    text_input_set_result_callback(
        text_input,
        subghz_scene_save_name_text_input_callback,
        subghz,
        subghz->file_name_tmp,
        SUBGHZ_MAX_LEN_NAME,
        dev_name_empty);

    ValidatorIsFile* validator_is_file = validator_is_file_alloc_init(
        furi_string_get_cstr(subghz->file_path), SUBGHZ_APP_FILENAME_EXTENSION, "");
    text_input_set_validator(text_input, validator_is_file_callback, validator_is_file);

    furi_string_free(file_name);
    furi_string_free(dir_name);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdTextInput);
}

bool subghz_scene_save_name_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeBack) {
        // Set file path to default
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        //
        if(!(strcmp(subghz->file_name_tmp, "") == 0) ||
           scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) !=
               SubGhzCustomEventManagerNoSet) {
            if(!scene_manager_has_previous_scene(subghz->scene_manager, SubGhzSceneDecodeRAW)) {
                furi_string_set(subghz->file_path, subghz->file_path_tmp);
            }
        }

        scene_manager_previous_scene(subghz->scene_manager);

        return true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneSaveName) {
            if(strcmp(subghz->file_name_tmp, "") != 0) {
                furi_string_reset(subghz->error_str);
                furi_string_cat_printf(
                    subghz->file_path,
                    "/%s%s",
                    subghz->file_name_tmp,
                    SUBGHZ_APP_FILENAME_EXTENSION);
                if(subghz_path_is_file(subghz->file_path_tmp)) {
                    if(!subghz_rename_file(subghz)) {
                        return false;
                    }
                } else {
                    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneSetType) !=
                       SubGhzCustomEventManagerNoSet) {
                        FlipperFormat* signal_data = subghz_txrx_get_fff_data(subghz->txrx);
                        subghz_scene_save_name_set_duplicate_message(subghz, signal_data);
                        subghz_save_protocol_to_file(
                            subghz,
                            signal_data,
                            furi_string_get_cstr(subghz->file_path));
                        scene_manager_set_scene_state(
                            subghz->scene_manager,
                            SubGhzSceneSetType,
                            SubGhzCustomEventManagerNoSet);
                    } else {
                        FlipperFormat* signal_data =
                            subghz_history_get_raw_data(subghz->history, subghz->idx_menu_chosen);
                        subghz_scene_save_name_set_duplicate_message(subghz, signal_data);
                        subghz_save_protocol_to_file(
                            subghz,
                            signal_data,
                            furi_string_get_cstr(subghz->file_path));
                    }
                }

                if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) !=
                   SubGhzCustomEventManagerNoSet) {
                    subghz_protocol_raw_gen_fff_data(
                        subghz_txrx_get_fff_data(subghz->txrx),
                        furi_string_get_cstr(subghz->file_path),
                        subghz_txrx_radio_device_get_name(subghz->txrx));
                    scene_manager_set_scene_state(
                        subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerNoSet);
                } else {
                    subghz_file_name_clear(subghz);
                }

                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveSuccess);
                if(scene_manager_has_previous_scene(subghz->scene_manager, SubGhzSceneSavedMenu)) {
                    // Nothing, do not count editing as saving
                } else if(scene_manager_has_previous_scene(
                              subghz->scene_manager, SubGhzSceneMoreRAW)) {
                    // Ditto, for RAW signals
                } else if(scene_manager_has_previous_scene(
                              subghz->scene_manager, SubGhzSceneSetType)) {
                    dolphin_deed(DolphinDeedSubGhzAddManually);
                } else {
                    dolphin_deed(DolphinDeedSubGhzSave);
                }
                return true;
            } else {
                furi_string_set(subghz->error_str, "No name file");
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
                return true;
            }
        }
    }
    return false;
}

void subghz_scene_save_name_on_exit(void* context) {
    SubGhz* subghz = context;

    // Clear validator
    void* validator_context = text_input_get_validator_callback_context(subghz->text_input);
    text_input_set_validator(subghz->text_input, NULL, NULL);
    validator_is_file_free(validator_context);

    // Clear view
    text_input_reset(subghz->text_input);
}
