#include "thermopro_tx4.h"

#define TAG "WSProtocolThermoPRO_TX4"

/*
 * Help
 * https://github.com/merbanan/rtl_433/blob/master/src/devices/thermopro_tx2.c
 *
  * The sensor sends 37 bits 6 times, before the first packet there is a sync pulse.
 * The packets are ppm modulated (distance coding) with a pulse of ~500 us
 * followed by a short gap of ~2000 us for a 0 bit or a long ~4000 us gap for a
 * 1 bit, the sync gap is ~9000 us.
 * The data is grouped in 9 nibbles
 *     [type] [id0] [id1] [flags] [temp0] [temp1] [temp2] [humi0] [humi1]
 * - type: 4 bit fixed 1001 (9) or 0110 (5)
 * - id: 8 bit a random id that is generated when the sensor starts, could include battery status
 *   the same batteries often generate the same id
 * - flags(3): is 1 when the battery is low, otherwise 0 (ok)
 * - flags(2): is 1 when the sensor sends a reading when pressing the button on the sensor
 * - flags(1,0): the channel number that can be set by the sensor (1, 2, 3, X)
 * - temp: 12 bit signed scaled by 10
 * - humi: 8 bit always 11001100 (0xCC) if no humidity sensor is available
 * 
 */

#define THERMO_PRO_TX4_TYPE_1 0b1001
#define THERMO_PRO_TX4_TYPE_2 0b0110

static const SubGhzBlockConst ws_protocol_thermopro_tx4_const = {
    .te_short = 500,
    .te_long = 2000,
    .te_delta = 150,
    .min_count_bit_for_found = 37,
};

struct WSProtocolDecoderThermoPRO_TX4 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    WSBlockGeneric generic;
};

struct WSProtocolEncoderThermoPRO_TX4 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    WSBlockGeneric generic;
};

// One repeat = 1 sync pair + 37 data-bit pairs = 38 LevelDuration pairs = 76 entries.
#define THERMOPRO_TX4_UPLOAD_PAIRS_PER_REPEAT (1 + 37)
#define THERMOPRO_TX4_UPLOAD_MAX_REPEAT       12
#define THERMOPRO_TX4_UPLOAD_MAX_SIZE \
    (THERMOPRO_TX4_UPLOAD_PAIRS_PER_REPEAT * 2 * THERMOPRO_TX4_UPLOAD_MAX_REPEAT)

typedef enum {
    ThermoPRO_TX4DecoderStepReset = 0,
    ThermoPRO_TX4DecoderStepSaveDuration,
    ThermoPRO_TX4DecoderStepCheckDuration,
} ThermoPRO_TX4DecoderStep;

const SubGhzProtocolDecoder ws_protocol_thermopro_tx4_decoder = {
    .alloc = ws_protocol_decoder_thermopro_tx4_alloc,
    .free = ws_protocol_decoder_thermopro_tx4_free,

    .feed = ws_protocol_decoder_thermopro_tx4_feed,
    .reset = ws_protocol_decoder_thermopro_tx4_reset,

    .get_hash_data = ws_protocol_decoder_thermopro_tx4_get_hash_data,
    .serialize = ws_protocol_decoder_thermopro_tx4_serialize,
    .deserialize = ws_protocol_decoder_thermopro_tx4_deserialize,
    .get_string = ws_protocol_decoder_thermopro_tx4_get_string,
};

const SubGhzProtocolEncoder ws_protocol_thermopro_tx4_encoder = {
    .alloc = ws_protocol_encoder_thermopro_tx4_alloc,
    .free = ws_protocol_encoder_thermopro_tx4_free,

    .deserialize = ws_protocol_encoder_thermopro_tx4_deserialize,
    .stop = ws_protocol_encoder_thermopro_tx4_stop,
    .yield = ws_protocol_encoder_thermopro_tx4_yield,
};

const SubGhzProtocol ws_protocol_thermopro_tx4 = {
    .name = WS_PROTOCOL_THERMOPRO_TX4_NAME,
    .type = SubGhzProtocolWeatherStation,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable,

    .decoder = &ws_protocol_thermopro_tx4_decoder,
    .encoder = &ws_protocol_thermopro_tx4_encoder,
};

void* ws_protocol_decoder_thermopro_tx4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    WSProtocolDecoderThermoPRO_TX4* instance = malloc(sizeof(WSProtocolDecoderThermoPRO_TX4));
    instance->base.protocol = &ws_protocol_thermopro_tx4;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void ws_protocol_decoder_thermopro_tx4_free(void* context) {
    furi_assert(context);
    WSProtocolDecoderThermoPRO_TX4* instance = context;
    free(instance);
}

void ws_protocol_decoder_thermopro_tx4_reset(void* context) {
    furi_assert(context);
    WSProtocolDecoderThermoPRO_TX4* instance = context;
    instance->decoder.parser_step = ThermoPRO_TX4DecoderStepReset;
}

static bool ws_protocol_thermopro_tx4_check(WSProtocolDecoderThermoPRO_TX4* instance) {
    uint8_t type = instance->decoder.decode_data >> 33;

    if((type == THERMO_PRO_TX4_TYPE_1) || (type == THERMO_PRO_TX4_TYPE_2)) {
        return true;
    } else {
        return false;
    }
}

/**
 * Analysis of received data
 * @param instance Pointer to a WSBlockGeneric* instance
 */
static void ws_protocol_thermopro_tx4_remote_controller(WSBlockGeneric* instance) {
    instance->id = (instance->data >> 25) & 0xFF;
    instance->battery_low = (instance->data >> 24) & 1;
    instance->btn = (instance->data >> 23) & 1;
    instance->channel = ((instance->data >> 21) & 0x03) + 1;

    if(!((instance->data >> 20) & 1)) {
        instance->temp = (float)((instance->data >> 9) & 0x07FF) / 10.0f;
    } else {
        instance->temp = (float)((~(instance->data >> 9) & 0x07FF) + 1) / -10.0f;
    }

    instance->humidity = (instance->data >> 1) & 0xFF;
}

void ws_protocol_decoder_thermopro_tx4_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    WSProtocolDecoderThermoPRO_TX4* instance = context;

    switch(instance->decoder.parser_step) {
    case ThermoPRO_TX4DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, ws_protocol_thermopro_tx4_const.te_short * 18) <
                        ws_protocol_thermopro_tx4_const.te_delta * 10)) {
            //Found sync
            instance->decoder.parser_step = ThermoPRO_TX4DecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case ThermoPRO_TX4DecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = ThermoPRO_TX4DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = ThermoPRO_TX4DecoderStepReset;
        }
        break;

    case ThermoPRO_TX4DecoderStepCheckDuration:
        if(!level) {
            if(DURATION_DIFF(duration, ws_protocol_thermopro_tx4_const.te_short * 18) <
               ws_protocol_thermopro_tx4_const.te_delta * 10) {
                //Found sync
                instance->decoder.parser_step = ThermoPRO_TX4DecoderStepReset;
                if((instance->decoder.decode_count_bit ==
                    ws_protocol_thermopro_tx4_const.min_count_bit_for_found) &&
                   ws_protocol_thermopro_tx4_check(instance)) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    ws_protocol_thermopro_tx4_remote_controller(&instance->generic);
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                    instance->decoder.parser_step = ThermoPRO_TX4DecoderStepCheckDuration;
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;

                break;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, ws_protocol_thermopro_tx4_const.te_short) <
                 ws_protocol_thermopro_tx4_const.te_delta) &&
                (DURATION_DIFF(duration, ws_protocol_thermopro_tx4_const.te_long) <
                 ws_protocol_thermopro_tx4_const.te_delta * 2)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = ThermoPRO_TX4DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, ws_protocol_thermopro_tx4_const.te_short) <
                 ws_protocol_thermopro_tx4_const.te_delta) &&
                (DURATION_DIFF(duration, ws_protocol_thermopro_tx4_const.te_long * 2) <
                 ws_protocol_thermopro_tx4_const.te_delta * 4)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = ThermoPRO_TX4DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = ThermoPRO_TX4DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = ThermoPRO_TX4DecoderStepReset;
        }
        break;
    }
}

uint8_t ws_protocol_decoder_thermopro_tx4_get_hash_data(void* context) {
    furi_assert(context);
    WSProtocolDecoderThermoPRO_TX4* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus ws_protocol_decoder_thermopro_tx4_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    WSProtocolDecoderThermoPRO_TX4* instance = context;
    return ws_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    ws_protocol_decoder_thermopro_tx4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    WSProtocolDecoderThermoPRO_TX4* instance = context;
    return ws_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        ws_protocol_thermopro_tx4_const.min_count_bit_for_found);
}

void ws_protocol_decoder_thermopro_tx4_get_string(void* context, FuriString* output) {
    furi_assert(context);
    WSProtocolDecoderThermoPRO_TX4* instance = context;
    furi_string_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "Sn:0x%lX Ch:%d  Bat:%d\r\n"
        "Temp:%3.1f C Hum:%d%%",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data),
        instance->generic.id,
        instance->generic.channel,
        instance->generic.battery_low,
        (double)instance->generic.temp,
        instance->generic.humidity);
}

void* ws_protocol_encoder_thermopro_tx4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    WSProtocolEncoderThermoPRO_TX4* instance = malloc(sizeof(WSProtocolEncoderThermoPRO_TX4));
    instance->base.protocol = &ws_protocol_thermopro_tx4;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = THERMOPRO_TX4_UPLOAD_MAX_SIZE;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void ws_protocol_encoder_thermopro_tx4_free(void* context) {
    furi_assert(context);
    WSProtocolEncoderThermoPRO_TX4* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/**
 * Build the PPM upload for one repeat of the 37-bit ThermoPRO-TX4 frame.
 * Pulse is always te_short; the following gap encodes the bit: te_long for a
 * 0 bit, te_long*2 for a 1 bit. Each repeat is preceded by a sync gap of
 * te_short*18, matching what ws_protocol_decoder_thermopro_tx4_feed expects.
 */
static bool
    ws_protocol_encoder_thermopro_tx4_get_upload(WSProtocolEncoderThermoPRO_TX4* instance) {
    furi_assert(instance);

    size_t index = 0;
    size_t size_upload = THERMOPRO_TX4_UPLOAD_PAIRS_PER_REPEAT * 2;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    }
    instance->encoder.size_upload = size_upload;

    // Sync
    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)ws_protocol_thermopro_tx4_const.te_short);
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)ws_protocol_thermopro_tx4_const.te_short * 18);

    // 37 data bits, MSB first
    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        bool bit = bit_read(instance->generic.data, i - 1);
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)ws_protocol_thermopro_tx4_const.te_short);
        instance->encoder.upload[index++] = level_duration_make(
            false,
            bit ? (uint32_t)ws_protocol_thermopro_tx4_const.te_long * 2 :
                  (uint32_t)ws_protocol_thermopro_tx4_const.te_long);
    }

    return true;
}

SubGhzProtocolStatus
    ws_protocol_encoder_thermopro_tx4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    WSProtocolEncoderThermoPRO_TX4* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint32_t temp_data = 0;
        if(!flipper_format_read_uint32(flipper_format, "Bit", &temp_data, 1)) {
            FURI_LOG_E(TAG, "Missing Bit");
            ret = SubGhzProtocolStatusErrorParserBitCount;
            break;
        }
        instance->generic.data_count_bit = (uint8_t)temp_data;
        if(instance->generic.data_count_bit !=
           ws_protocol_thermopro_tx4_const.min_count_bit_for_found) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }

        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Key");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        instance->generic.data = 0;
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data = (instance->generic.data << 8) | key_data[i];
        }

        // Optional value
        if(!flipper_format_read_uint32(
               flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1)) {
            instance->encoder.repeat = 10;
        }

        if(!ws_protocol_encoder_thermopro_tx4_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void ws_protocol_encoder_thermopro_tx4_stop(void* context) {
    WSProtocolEncoderThermoPRO_TX4* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration ws_protocol_encoder_thermopro_tx4_yield(void* context) {
    WSProtocolEncoderThermoPRO_TX4* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}
