#include "subaru.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolSubaru"

static const SubGhzBlockConst subghz_protocol_subaru_const = {
    .te_short = 800,
    .te_long = 1600,
    .te_delta = 200,
    .min_count_bit_for_found = 64,
};

#define SUBARU_PREAMBLE_PAIRS 75
#define SUBARU_GAP_US 2800
#define SUBARU_SYNC_US 2800
#define SUBARU_UPLOAD_CAPACITY 400

/* ============================================================
 * STRUCT DEFINITIONS (NO typedef — matches subaru.h forward)
 * ============================================================ */

struct SubGhzProtocolDecoderSubaru {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;

    uint64_t key;
    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
};

struct SubGhzProtocolEncoderSubaru {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t key;
    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
};

/* ============================================================
 * HELPERS
 * ============================================================ */

static const char* subaru_get_button_name(uint8_t btn) {
    switch(btn & 0x03) {
    case 0: return "Lock";
    case 1: return "Unlock";
    case 2: return "Trunk";
    case 3: return "Panic";
    default: return "??";
    }
}

static void subaru_rotate_left_3(uint8_t* a, uint8_t* b, uint8_t* c, uint8_t count) {
    for(uint8_t i = 0; i < count; i++) {
        uint8_t t = *a;
        *a = (*a << 1) | (*b >> 7);
        *b = (*b << 1) | (*c >> 7);
        *c = (*c << 1) | (t >> 7);
    }
}

static void subaru_decode_fields(
    const uint8_t* kb,
    uint32_t* serial,
    uint8_t* btn,
    uint16_t* cnt) {

    *btn = kb[0] & 0x0F;
    *serial = ((uint32_t)kb[1] << 16) |
              ((uint32_t)kb[2] << 8) |
              kb[3];

    uint8_t lo = 0;

    if(!(kb[4] & 0x40)) lo |= 0x01;
    if(!(kb[4] & 0x80)) lo |= 0x02;
    if(!(kb[5] & 0x01)) lo |= 0x04;
    if(!(kb[5] & 0x02)) lo |= 0x08;
    if(!(kb[6] & 0x01)) lo |= 0x10;
    if(!(kb[6] & 0x02)) lo |= 0x20;
    if(!(kb[5] & 0x40)) lo |= 0x40;
    if(!(kb[5] & 0x80)) lo |= 0x80;

    uint8_t reg1 =
        ((kb[7] & 0x0F) << 4) |
        (kb[5] & 0x0C) |
        ((kb[6] >> 6) & 0x03);

    uint8_t reg2 =
        ((kb[6] & 0x3C) << 2) |
        ((kb[7] >> 4) & 0x0F);

    uint8_t s0 = kb[3];
    uint8_t s1 = kb[1];
    uint8_t s2 = kb[2];

    subaru_rotate_left_3(&s0, &s1, &s2, 4 + lo);

    uint8_t t1 = s1 ^ reg1;
    uint8_t t2 = s2 ^ reg2;

    uint8_t hi = 0;

    if(!(t1 & 0x10)) hi |= 0x04;
    if(!(t1 & 0x20)) hi |= 0x08;
    if(!(t2 & 0x80)) hi |= 0x02;
    if(!(t2 & 0x40)) hi |= 0x01;
    if(!(t1 & 0x01)) hi |= 0x40;
    if(!(t1 & 0x02)) hi |= 0x80;
    if(!(t2 & 0x08)) hi |= 0x20;
    if(!(t2 & 0x04)) hi |= 0x10;

    *cnt = ((uint16_t)hi << 8) | lo;
}

/* ============================================================
 * PROTOCOL TABLE
 * ============================================================ */

const SubGhzProtocolDecoder subghz_protocol_subaru_decoder;
const SubGhzProtocolEncoder subghz_protocol_subaru_encoder;

const SubGhzProtocol subghz_protocol_subaru = {
    .name = SUBGHZ_PROTOCOL_SUBARU_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_subaru_decoder,
    .encoder = &subghz_protocol_subaru_encoder,
};

/* ============================================================
 * DECODER IMPLEMENTATION
 * ============================================================ */

void* subghz_protocol_decoder_subaru_alloc(SubGhzEnvironment* env) {
    UNUSED(env);
    SubGhzProtocolDecoderSubaru* i = malloc(sizeof(SubGhzProtocolDecoderSubaru));
    i->base.protocol = &subghz_protocol_subaru;
    i->generic.protocol_name = i->base.protocol->name;
    return i;
}

void subghz_protocol_decoder_subaru_free(void* ctx) {
    free(ctx);
}

void subghz_protocol_decoder_subaru_reset(void* ctx) {
    SubGhzProtocolDecoderSubaru* i = ctx;
    i->decoder.parser_step = 0;
    i->decoder.decode_data = 0;
    i->decoder.decode_count_bit = 0;
    i->header_count = 0;
}

void subghz_protocol_decoder_subaru_feed(void* ctx, bool level, uint32_t dur) {
    SubGhzProtocolDecoderSubaru* i = ctx;

    const uint32_t te_short = subghz_protocol_subaru_const.te_short;
    const uint32_t te_long  = subghz_protocol_subaru_const.te_long;
    const uint32_t delta    = subghz_protocol_subaru_const.te_delta;

    switch(i->decoder.parser_step) {

    case 0:
        if(level && DURATION_DIFF(dur, te_long) < delta) {
            i->decoder.decode_data = 0;
            i->decoder.decode_count_bit = 0;
            i->header_count = 0;
            i->decoder.parser_step = 1;
        }
        break;

    case 1:
        if(!level && DURATION_DIFF(dur, te_long) < delta) {
            i->header_count++;
        } else if(!level && DURATION_DIFF(dur, SUBARU_GAP_US) < 800) {
            if(i->header_count > 20)
                i->decoder.parser_step = 2;
            else
                i->decoder.parser_step = 0;
        } else {
            i->decoder.parser_step = 0;
        }
        break;

    case 2:
        if(level && DURATION_DIFF(dur, SUBARU_SYNC_US) < 800) {
            i->decoder.parser_step = 3;
        } else {
            i->decoder.parser_step = 0;
        }
        break;

    case 3:
        if(!level) {
            i->decoder.te_last = dur;
            i->decoder.parser_step = 4;
        } else {
            i->decoder.parser_step = 0;
        }
        break;

    case 4:
        if(level) {
            bool bit;
            if(DURATION_DIFF(dur, te_long) < delta &&
               DURATION_DIFF(i->decoder.te_last, te_short) < delta) {
                bit = false;
            } else if(DURATION_DIFF(dur, te_short) < delta &&
                      DURATION_DIFF(i->decoder.te_last, te_long) < delta) {
                bit = true;
            } else {
                i->decoder.parser_step = 0;
                break;
            }

            subghz_protocol_blocks_add_bit(&i->decoder, bit);

            if(i->decoder.decode_count_bit >= 64) {

                i->generic.data = i->decoder.decode_data;
                i->generic.data_count_bit = 64;

                uint8_t b[8];
                for(int k = 0; k < 8; k++)
                    b[k] = (i->generic.data >> (56 - 8*k)) & 0xFF;

                subaru_decode_fields(b, &i->serial, &i->btn, &i->cnt);

                i->generic.serial = i->serial;
                i->generic.btn = i->btn;
                i->generic.cnt = i->cnt;

                if(i->base.callback)
                    i->base.callback(&i->base, i->base.context);

                i->decoder.parser_step = 0;
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_subaru_get_hash_data(void* ctx) {
    SubGhzProtocolDecoderSubaru* i = ctx;
    return subghz_protocol_blocks_get_hash_data(
        &i->decoder,
        (i->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_subaru_serialize(
    void* ctx,
    FlipperFormat* ff,
    SubGhzRadioPreset* preset) {

    SubGhzProtocolDecoderSubaru* i = ctx;
    return subghz_block_generic_serialize(&i->generic, ff, preset);
}

SubGhzProtocolStatus subghz_protocol_decoder_subaru_deserialize(
    void* ctx,
    FlipperFormat* ff) {

    SubGhzProtocolDecoderSubaru* i = ctx;
    return subghz_block_generic_deserialize_check_count_bit(
        &i->generic,
        ff,
        subghz_protocol_subaru_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_subaru_get_string(void* ctx, FuriString* out) {
    SubGhzProtocolDecoderSubaru* i = ctx;

    furi_string_cat_printf(
        out,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Sn:%06lX Btn:%01X [%s]\r\n"
        "Cnt:%04X",
        i->generic.protocol_name,
        i->generic.data_count_bit,
        i->generic.data,
        i->serial,
        i->btn,
        subaru_get_button_name(i->btn),
        i->cnt);
}

const SubGhzProtocolDecoder subghz_protocol_subaru_decoder = {
    .alloc = subghz_protocol_decoder_subaru_alloc,
    .free = subghz_protocol_decoder_subaru_free,
    .feed = subghz_protocol_decoder_subaru_feed,
    .reset = subghz_protocol_decoder_subaru_reset,
    .get_hash_data = subghz_protocol_decoder_subaru_get_hash_data,
    .serialize = subghz_protocol_decoder_subaru_serialize,
    .deserialize = subghz_protocol_decoder_subaru_deserialize,
    .get_string = subghz_protocol_decoder_subaru_get_string,
};
