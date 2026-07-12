#include "subghz_history.h"
#include <lib/subghz/receiver.h>
#include <lib/toolbox/dir_walk.h>
#include <lib/toolbox/path.h>
#include <toolbox/strint.h>

#include <furi.h>

#define SUBGHZ_HISTORY_MAX       55
#define SUBGHZ_HISTORY_FREE_HEAP 20480
#define TAG                      "SubGhzHistory"

typedef struct {
    FuriString* item_str;
    FlipperFormat* flipper_string;
    uint8_t type;
    SubGhzRadioPreset* preset;
    DateTime datetime;
} SubGhzHistoryItem;

ARRAY_DEF(SubGhzHistoryItemArray, SubGhzHistoryItem, M_POD_OPLIST) //-V658

#define M_OPL_SubGhzHistoryItemArray_t() ARRAY_OPLIST(SubGhzHistoryItemArray, M_POD_OPLIST)

typedef struct {
    SubGhzHistoryItemArray_t data;
} SubGhzHistoryStruct;

struct SubGhzHistory {
    uint32_t last_update_timestamp;
    uint16_t last_index_write;
    uint8_t code_last_hash_data;
    FuriString* tmp_string;
    SubGhzHistoryStruct* history;
};

SubGhzHistory* subghz_history_alloc(void) {
    SubGhzHistory* instance = malloc(sizeof(SubGhzHistory));
    instance->tmp_string = furi_string_alloc();
    instance->history = malloc(sizeof(SubGhzHistoryStruct));
    SubGhzHistoryItemArray_init(instance->history->data);
    return instance;
}

void subghz_history_free(SubGhzHistory* instance) {
    furi_assert(instance);
    furi_string_free(instance->tmp_string);
    for
        M_EACH(item, instance->history->data, SubGhzHistoryItemArray_t) {
            furi_string_free(item->item_str);
            furi_string_free(item->preset->name);
            free(item->preset);
            flipper_format_free(item->flipper_string);
            item->type = 0;
        }
    SubGhzHistoryItemArray_clear(instance->history->data);
    free(instance->history);
    free(instance);
}

uint32_t subghz_history_get_frequency(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
    return item->preset->frequency;
}

SubGhzRadioPreset* subghz_history_get_radio_preset(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
    return item->preset;
}

const char* subghz_history_get_preset(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
    return furi_string_get_cstr(item->preset->name);
}

void subghz_history_reset(SubGhzHistory* instance) {
    furi_assert(instance);
    furi_string_reset(instance->tmp_string);
    for
        M_EACH(item, instance->history->data, SubGhzHistoryItemArray_t) {
            furi_string_free(item->item_str);
            furi_string_free(item->preset->name);
            free(item->preset);
            flipper_format_free(item->flipper_string);
            item->type = 0;
        }
    SubGhzHistoryItemArray_reset(instance->history->data);
    instance->last_index_write = 0;
    instance->code_last_hash_data = 0;
}

void subghz_history_delete_item(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);

    if(idx < SubGhzHistoryItemArray_size(instance->history->data)) {
        SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
        furi_string_free(item->item_str);
        furi_string_free(item->preset->name);
        free(item->preset);
        flipper_format_free(item->flipper_string);
        item->type = 0;
        SubGhzHistoryItemArray_remove_v(instance->history->data, idx, idx + 1);
        instance->last_index_write--;
    }
}

uint16_t subghz_history_get_item(SubGhzHistory* instance) {
    furi_assert(instance);
    return instance->last_index_write;
}

uint8_t subghz_history_get_type_protocol(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
    return item->type;
}

const char* subghz_history_get_protocol_name(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
    if(!item || !item->flipper_string) {
        FURI_LOG_E(TAG, "Missing Item");
        furi_string_reset(instance->tmp_string);
        return furi_string_get_cstr(instance->tmp_string);
    }
    flipper_format_rewind(item->flipper_string);
    if(!flipper_format_read_string(item->flipper_string, "Protocol", instance->tmp_string)) {
        FURI_LOG_E(TAG, "Missing Protocol");
        furi_string_reset(instance->tmp_string);
    }
    return furi_string_get_cstr(instance->tmp_string);
}

DateTime subghz_history_get_datetime(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
    if(item) {
        return item->datetime;
    } else {
        return (DateTime){};
    }
}

FlipperFormat* subghz_history_get_raw_data(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
    if(item->flipper_string) {
        return item->flipper_string;
    } else {
        return NULL;
    }
}
bool subghz_history_get_text_space_left(SubGhzHistory* instance, FuriString* output) {
    furi_assert(instance);
    if(memmgr_get_free_heap() < SUBGHZ_HISTORY_FREE_HEAP) {
        if(output != NULL) furi_string_printf(output, "  RAM almost FULL");
        return true;
    }
    if(instance->last_index_write == SUBGHZ_HISTORY_MAX) {
        if(output != NULL) furi_string_printf(output, "   Memory is FULL");
        return true;
    }
    if(output != NULL)
        furi_string_printf(output, "%02u/%02u", instance->last_index_write, SUBGHZ_HISTORY_MAX);
    return false;
}

uint16_t subghz_history_get_last_index(SubGhzHistory* instance) {
    return instance->last_index_write;
}
void subghz_history_get_text_item_menu(SubGhzHistory* instance, FuriString* output, uint16_t idx) {
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
    furi_string_set(output, item->item_str);
}

void subghz_history_get_time_item_menu(SubGhzHistory* instance, FuriString* output, uint16_t idx) {
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_get(instance->history->data, idx);
    DateTime* t = &item->datetime;
    furi_string_printf(output, "%.2d:%.2d:%.2d ", t->hour, t->minute, t->second);
}

static bool subghz_history_file_filter(const char* name, FileInfo* fileinfo, void* ctx) {
    UNUSED(ctx);
    if(file_info_is_dir(fileinfo)) return false;

    const char* ext = strrchr(name, '.');
    return ext && !strcmp(ext, SUBGHZ_APP_FILENAME_EXTENSION);
}

static bool subghz_history_read_string_field(
    FlipperFormat* fff,
    const char* key,
    FuriString* value) {
    flipper_format_rewind(fff);
    bool result = flipper_format_read_string(fff, key, value);
    if(result) {
        furi_string_trim(value);
    }
    return result;
}

static bool subghz_history_parse_u32(const char* value, uint32_t* out) {
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

static bool subghz_history_read_u32_field(FlipperFormat* fff, const char* key, uint32_t* value) {
    flipper_format_rewind(fff);
    if(flipper_format_read_uint32(fff, key, value, 1)) {
        return true;
    }

    FuriString* value_str = furi_string_alloc();
    bool result = false;
    if(subghz_history_read_string_field(fff, key, value_str)) {
        result = subghz_history_parse_u32(furi_string_get_cstr(value_str), value);
    }
    furi_string_free(value_str);
    return result;
}

static bool subghz_history_string_fields_match(
    FlipperFormat* a,
    FlipperFormat* b,
    const char* key,
    bool* both_present) {
    FuriString* a_value = furi_string_alloc();
    FuriString* b_value = furi_string_alloc();
    bool a_has = subghz_history_read_string_field(a, key, a_value);
    bool b_has = subghz_history_read_string_field(b, key, b_value);
    bool result = a_has && b_has && furi_string_equal(a_value, b_value);
    if(both_present) {
        *both_present = a_has && b_has;
    }
    furi_string_free(a_value);
    furi_string_free(b_value);
    return result;
}

static bool subghz_history_key_fields_match(FlipperFormat* saved, FlipperFormat* captured) {
    uint8_t saved_key[sizeof(uint64_t)] = {0};
    uint8_t captured_key[sizeof(uint64_t)] = {0};

    flipper_format_rewind(saved);
    bool saved_has_hex = flipper_format_read_hex(saved, "Key", saved_key, sizeof(saved_key));
    flipper_format_rewind(captured);
    bool captured_has_hex =
        flipper_format_read_hex(captured, "Key", captured_key, sizeof(captured_key));

    if(saved_has_hex && captured_has_hex) {
        return memcmp(saved_key, captured_key, sizeof(saved_key)) == 0;
    }

    return subghz_history_string_fields_match(saved, captured, "Key", NULL);
}

static bool subghz_history_saved_signal_matches(
    FlipperFormat* saved,
    FlipperFormat* captured) {
    if(!subghz_history_string_fields_match(saved, captured, "Protocol", NULL)) {
        return false;
    }

    bool both_manufacture = false;
    bool same_manufacture =
        subghz_history_string_fields_match(saved, captured, "Manufacture", &both_manufacture);
    if(both_manufacture && !same_manufacture) {
        return false;
    }

    uint32_t saved_serial = 0;
    uint32_t captured_serial = 0;
    bool saved_has_serial = subghz_history_read_u32_field(saved, "Serial", &saved_serial);
    bool captured_has_serial = subghz_history_read_u32_field(captured, "Serial", &captured_serial);
    if(saved_has_serial && captured_has_serial) {
        if(saved_serial == captured_serial && saved_serial != 0) {
            return true;
        }
        if(saved_serial != captured_serial) {
            return false;
        }
    }

    return subghz_history_key_fields_match(saved, captured);
}

static bool subghz_history_find_saved_signal_name(FlipperFormat* captured, FuriString* saved_name) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    DirWalk* dir_walk = dir_walk_alloc(storage);
    FlipperFormat* saved_fff = flipper_format_file_alloc(storage);
    FuriString* path = furi_string_alloc();
    FileInfo fileinfo;
    bool found = false;

    dir_walk_set_recursive(dir_walk, true);
    dir_walk_set_filter_cb(dir_walk, subghz_history_file_filter, NULL);
    if(dir_walk_open(dir_walk, SUBGHZ_APP_FOLDER)) {
        while(dir_walk_read(dir_walk, path, &fileinfo) == DirWalkOK) {
            if(file_info_is_dir(&fileinfo)) continue;

            if(flipper_format_file_open_existing(saved_fff, furi_string_get_cstr(path))) {
                if(subghz_history_saved_signal_matches(saved_fff, captured)) {
                    path_extract_filename(path, saved_name, true);
                    found = true;
                    flipper_format_file_close(saved_fff);
                    break;
                }
                flipper_format_file_close(saved_fff);
            }
        }
        dir_walk_close(dir_walk);
    }

    furi_string_free(path);
    flipper_format_free(saved_fff);
    dir_walk_free(dir_walk);
    furi_record_close(RECORD_STORAGE);
    return found;
}

static void subghz_history_append_saved_name(FlipperFormat* captured, FuriString* item_str) {
    if(furi_string_size(item_str) == 0) return;

    FuriString* saved_name = furi_string_alloc();
    if(subghz_history_find_saved_signal_name(captured, saved_name)) {
        furi_string_cat_printf(item_str, " Saved:%s", furi_string_get_cstr(saved_name));
    }
    furi_string_free(saved_name);
}

bool subghz_history_add_to_history(
    SubGhzHistory* instance,
    void* context,
    SubGhzRadioPreset* preset) {
    furi_assert(instance);
    furi_assert(context);

    if(memmgr_get_free_heap() < SUBGHZ_HISTORY_FREE_HEAP) return false;
    if(instance->last_index_write >= SUBGHZ_HISTORY_MAX) return false;

    SubGhzProtocolDecoderBase* decoder_base = context;
    if((instance->code_last_hash_data ==
        subghz_protocol_decoder_base_get_hash_data(decoder_base)) &&
       ((furi_get_tick() - instance->last_update_timestamp) < 500)) {
        instance->last_update_timestamp = furi_get_tick();
        return false;
    }

    instance->code_last_hash_data = subghz_protocol_decoder_base_get_hash_data(decoder_base);
    instance->last_update_timestamp = furi_get_tick();

    FuriString* text = furi_string_alloc();
    SubGhzHistoryItem* item = SubGhzHistoryItemArray_push_raw(instance->history->data);
    item->preset = malloc(sizeof(SubGhzRadioPreset));
    item->type = decoder_base->protocol->type;
    item->preset->frequency = preset->frequency;
    item->preset->name = furi_string_alloc();
    furi_string_set(item->preset->name, preset->name);
    item->preset->data = preset->data;
    item->preset->data_size = preset->data_size;
    furi_hal_rtc_get_datetime(&item->datetime);

    item->item_str = furi_string_alloc();
    item->flipper_string = flipper_format_string_alloc();
    subghz_protocol_decoder_base_serialize(decoder_base, item->flipper_string, preset);

    do {
        if(!flipper_format_rewind(item->flipper_string)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        if(!flipper_format_read_string(item->flipper_string, "Protocol", instance->tmp_string)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            break;
        }
        if(!strcmp(furi_string_get_cstr(instance->tmp_string), "KeeLoq")) {
            furi_string_set(instance->tmp_string, "KL ");
            if(!flipper_format_read_string(item->flipper_string, "Manufacture", text)) {
                FURI_LOG_E(TAG, "Missing Protocol");
                break;
            }
            furi_string_cat(instance->tmp_string, text);
        } else if(!strcmp(furi_string_get_cstr(instance->tmp_string), "Star Line")) {
            furi_string_set(instance->tmp_string, "SL ");
            if(!flipper_format_read_string(item->flipper_string, "Manufacture", text)) {
                FURI_LOG_E(TAG, "Missing Protocol");
                break;
            }
            furi_string_cat(instance->tmp_string, text);
        }
        if(!flipper_format_rewind(item->flipper_string)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(item->flipper_string, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_D(TAG, "No Key");
        }
        uint64_t data = 0;
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            data = (data << 8) | key_data[i];
        }
        if(data != 0) {
            if(!(uint32_t)(data >> 32)) {
                furi_string_printf(
                    item->item_str,
                    "%s %lX",
                    furi_string_get_cstr(instance->tmp_string),
                    (uint32_t)(data & 0xFFFFFFFF));
            } else {
                furi_string_printf(
                    item->item_str,
                    "%s %lX%08lX",
                    furi_string_get_cstr(instance->tmp_string),
                    (uint32_t)(data >> 32),
                    (uint32_t)(data & 0xFFFFFFFF));
            }
        } else {
            furi_string_printf(item->item_str, "%s", furi_string_get_cstr(instance->tmp_string));
        }

    } while(false);

    subghz_history_append_saved_name(item->flipper_string, item->item_str);

    furi_string_free(text);
    instance->last_index_write++;
    return true;
}
