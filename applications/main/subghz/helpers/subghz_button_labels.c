#include "subghz_button_labels.h"

#include <furi.h>
#include <lib/subghz/blocks/custom_btn.h>
#include <string.h>

static const char* const button_default_labels[SUBGHZ_BUTTON_LABEL_COUNT] = {
    "Original",
    "Up",
    "Down",
    "Left",
    "Right",
    "Button 5",
    "Button 6",
    "Button 7",
};

typedef struct {
    const char* protocol;
    const char* labels[SUBGHZ_BUTTON_LABEL_COUNT];
} SubGhzProtocolButtonLabels;

static const SubGhzProtocolButtonLabels protocol_button_labels[] = {
    {"VAG GROUP", {"Original", "Lock", "Unlock", "Trunk", "Panic"}},
    {"Porsche AG", {"Original", "Lock", "Unlock", "Trunk", "Open"}},
    {"FORD V0", {"Original", "Lock", "Unlock", "Trunk"}},
    {"Ford V2", {"Unlock", "Lock", "Trunk", "Panic", "Remote Start"}},
    {"PSA GROUP", {"Original", "Lock", "Unlock", "Trunk", "Trunk"}},
    {"PSA OLD", {"Original", "Lock", "Unlock", "Trunk", "Trunk"}},
    {"KIA/HYU V0", {"Original", "Lock", "Unlock", "Trunk", "Horn"}},
    {"KIA/HYU V1", {"Original", "Lock", "Unlock", "Trunk", "Panic"}},
    {"KIA/HYU V2", {"Original", "Lock", "Unlock", "Trunk", "Panic"}},
    {"KIA/HYU V3", {"Original", "Lock", "Unlock", "Trunk", "Panic", "Horn"}},
    {"KIA/HYU V4", {"Original", "Lock", "Unlock", "Trunk", "Panic", "Horn"}},
    {"KIA/HYU V3/V4", {"Original", "Lock", "Unlock", "Trunk", "Panic", "Horn"}},
    {"KIA/HYU V5", {"Original", "Unlock", "Lock", "Trunk", "Horn"}},
    {"KIA/HYU V6", {"Original", "Lock", "Unlock", "Trunk", "Panic"}},
    {"SUBARU", {"Original", "Lock", "Unlock", "Trunk", "Panic", "Extra"}},
    {"SUZUKI", {"Original", "Lock", "Unlock", "Trunk", "Panic"}},
    {"Star Line", {"Original", "Lock", "Unlock", "Trunk", "Start"}},
    {"Scher-Khan", {"Original", "Lock", "Unlock", "Trunk", "Start"}},
    {"Sheriff CFM", {"Original", "Lock", "Unlock", "Trunk", "Panic"}},
    {"Nice FloR-S", {"Original", "Btn 1", "Btn 2", "Btn 3", "Btn 4"}},
    {"CAME Atomo", {"Original", "Btn 1", "Btn 2", "Btn 3"}},
    {"Alutech AT-4N", {"Original", "Btn 1", "Btn 2", "Btn 3", "Btn 4"}},
    {"KeeLoq", {"Original", "Btn 1", "Btn 2", "Btn 3", "Btn 4"}},
    {"Phoenix_V2", {"Original", "Btn 1", "Btn 2", "Btn 3", "Btn 4"}},
    {"Beninca ARC", {"Original", "Btn 1", "Btn 2"}},
    {"GangQi", {"Original", "Btn 1", "Btn 2", "Btn 3"}},
    {"Hay21", {"Original", "Btn 1", "Btn 2"}},
    {"Hollarm", {"Original", "Btn 1", "Btn 2", "Btn 3"}},
    {"Jarolift", {"Original", "Btn 1", "Btn 2", "Btn 3"}},
    {"KingGates Stylo4k", {"Original", "Btn 1", "Btn 2", "Btn 3"}},
    {"Princeton", {"Original", "Btn 1", "Btn 2", "Btn 3", "Btn 4"}},
    {"Roger", {"Original", "Btn 1", "Btn 2", "Btn 3"}},
    {"Security+ 2.0", {"Original", "Btn 1", "Btn 2", "Btn 3", "Btn 4"}},
    {"Somfy Telis", {"Original", "Btn 1", "Btn 2", "Btn 3"}},
    {"Faac SLH", {"Original", "Btn 1"}},
};

void subghz_button_labels_reset(const char* labels[SUBGHZ_BUTTON_LABEL_COUNT]) {
    for(uint8_t i = 0; i < SUBGHZ_BUTTON_LABEL_COUNT; i++) {
        labels[i] = button_default_labels[i];
    }
}

void subghz_button_labels_apply_protocol(
    const char* protocol,
    const char* labels[SUBGHZ_BUTTON_LABEL_COUNT]) {
    if(!protocol) return;

    for(uint8_t i = 0; i < COUNT_OF(protocol_button_labels); i++) {
        if(strcmp(protocol, protocol_button_labels[i].protocol) == 0) {
            for(uint8_t btn = 0; btn < SUBGHZ_BUTTON_LABEL_COUNT; btn++) {
                if(protocol_button_labels[i].labels[btn]) {
                    labels[btn] = protocol_button_labels[i].labels[btn];
                }
            }
            break;
        }
    }
}

const char* subghz_button_labels_get(
    const char* const labels[SUBGHZ_BUTTON_LABEL_COUNT],
    uint8_t custom_btn_id,
    uint8_t original_custom_btn) {
    if(custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) {
        if((original_custom_btn != SUBGHZ_CUSTOM_BTN_OK) &&
           (original_custom_btn < SUBGHZ_BUTTON_LABEL_COUNT)) {
            return labels[original_custom_btn];
        }
    }

    if(custom_btn_id < SUBGHZ_BUTTON_LABEL_COUNT) {
        return labels[custom_btn_id];
    }

    return "Button";
}
