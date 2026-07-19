#include "bresser_3ch.h"

#define TAG "WSProtocolBresser3ch"

/*
 * Help:
 * https://github.com/merbanan/rtl_433/blob/master/src/devices/bresser_3ch.c
 *
 * Bresser sensor protocol.
 *
 * The protocol is for the wireless Temperature/Humidity sensor
 * - Bresser Thermo-/Hygro-Sensor 3CH
 * - also works for Renkforce DM-7511
 *
 * The sensor sends 15 identical packages of 40 bits each ~60s.
 * The bits are PWM modulated with On Off Keying.
 *
 * A short pulse of 250 us followed by a 500 us gap is a 0 bit,
 * a long pulse of 500 us followed by a 250 us gap is a 1 bit,
 * there is a sync preamble of pulse, gap, 750 us each, repeated 4 times.
 * Actual received and demodulated timings might be 2% shorter.
 * 
 * The data is grouped in 5 bytes / 10 nibbles
 * 
 *     [id] [id] [flags] [temp] [temp] [temp] [humi] [humi] [chk] [chk]
 * 
 * - id is an 8 bit random id that is generated when the sensor starts
 * - flags are 4 bits battery low indicator, test button press and channel
 * - temp is 12 bit unsigned fahrenheit offset by 90 and scaled by 10
 * - humi is 8 bit relative humidity percentage
 * - chk is the sum of the four data bytes 
 * 
 * @m7i-org - because there's more stuff screaming in the ether than you might think
 *
 */

static const SubGhzBlockConst ws_protocol_bresser_3ch_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 150,
    .min_count_bit_for_found = 40,
};

struct WSProtocolDecoderBresser3ch {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    WSBlockGeneric generic;
};

struct WSProtocolEncoderBresser3ch {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    WSBlockGeneric generic;
};

// One repeat = 1 dummy pair (absorbs the previous repeat's finalize-on-next-
// event quirk, see get_upload) + 4 preamble pairs + 40 data-bit pairs = 45
// LevelDuration pairs = 90 entries.
#define BRESSER_3CH_UPLOAD_PAIRS_PER_REPEAT (1 + 4 + 40)
#define BRESSER_3CH_UPLOAD_MAX_REPEAT       8
#define BRESSER_3CH_UPLOAD_MAX_SIZE \
    (BRESSER_3CH_UPLOAD_PAIRS_PER_REPEAT * 2 * BRESSER_3CH_UPLOAD_MAX_REPEAT)

const SubGhzProtocolDecoder ws_protocol_bresser_3ch_decoder = {
    .alloc = ws_protocol_decoder_bresser_3ch_alloc,
    .free = ws_protocol_decoder_bresser_3ch_free,

    .feed = ws_protocol_decoder_bresser_3ch_feed,
    .reset = ws_protocol_decoder_bresser_3ch_reset,

    .get_hash_data = ws_protocol_decoder_bresser_3ch_get_hash_data,
    .serialize = ws_protocol_decoder_bresser_3ch_serialize,
    .deserialize = ws_protocol_decoder_bresser_3ch_deserialize,
    .get_string = ws_protocol_decoder_bresser_3ch_get_string,
};

const SubGhzProtocolEncoder ws_protocol_bresser_3ch_encoder = {
    .alloc = ws_protocol_encoder_bresser_3ch_alloc,
    .free = ws_protocol_encoder_bresser_3ch_free,

    .deserialize = ws_protocol_encoder_bresser_3ch_deserialize,
    .stop = ws_protocol_encoder_bresser_3ch_stop,
    .yield = ws_protocol_encoder_bresser_3ch_yield,
};

const SubGhzProtocol ws_protocol_bresser_3ch = {
    .name = WS_PROTOCOL_BRESSER_3CH_NAME,
    .type = SubGhzProtocolWeatherStation,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable,

    .decoder = &ws_protocol_bresser_3ch_decoder,
    .encoder = &ws_protocol_bresser_3ch_encoder,
};

typedef enum {
    Bresser3chDecoderStepReset = 0,
    Bresser3chDecoderStepPreambleDn,
    Bresser3chDecoderStepPreambleUp,
    Bresser3chDecoderStepSaveDuration,
    Bresser3chDecoderStepCheckDuration,
} Bresser3chDecoderStep;

void* ws_protocol_decoder_bresser_3ch_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    WSProtocolDecoderBresser3ch* instance = malloc(sizeof(WSProtocolDecoderBresser3ch));
    instance->base.protocol = &ws_protocol_bresser_3ch;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void ws_protocol_decoder_bresser_3ch_free(void* context) {
    furi_assert(context);
    WSProtocolDecoderBresser3ch* instance = context;
    free(instance);
}

void ws_protocol_decoder_bresser_3ch_reset(void* context) {
    furi_assert(context);
    WSProtocolDecoderBresser3ch* instance = context;
    instance->decoder.parser_step = Bresser3chDecoderStepReset;
}

static bool ws_protocol_bresser_3ch_check(WSProtocolDecoderBresser3ch* instance) {
    if(!instance->decoder.decode_data) return false;

    uint8_t sum = (((instance->decoder.decode_data >> 32) & 0xff) +
                   ((instance->decoder.decode_data >> 24) & 0xff) +
                   ((instance->decoder.decode_data >> 16) & 0xff) +
                   ((instance->decoder.decode_data >> 8) & 0xff)) &
                  0xff;

    return (instance->decoder.decode_data & 0xff) == sum;
}

/**
 * Analysis of received data
 * @param instance Pointer to a WSBlockGeneric* instance
 */
static void ws_protocol_bresser_3ch_extract_data(WSBlockGeneric* instance) {
    instance->id = (instance->data >> 32) & 0xff;
    instance->battery_low = ((instance->data >> 31) & 0x1);
    instance->btn = (instance->data >> 30) & 0x1;
    instance->channel = (instance->data >> 28) & 0x3;

    int16_t temp = (instance->data >> 16) & 0xfff;
    instance->temp = locale_fahrenheit_to_celsius((float)(temp - 900) / 10.0);

    instance->humidity = (instance->data >> 8) & 0xff;
}

void ws_protocol_decoder_bresser_3ch_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    WSProtocolDecoderBresser3ch* instance = context;

    switch(instance->decoder.parser_step) {
    case Bresser3chDecoderStepReset:
        if(level && DURATION_DIFF(duration, ws_protocol_bresser_3ch_const.te_short * 3) <
                        ws_protocol_bresser_3ch_const.te_delta) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = Bresser3chDecoderStepPreambleDn;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case Bresser3chDecoderStepPreambleDn:
        if((!level) && DURATION_DIFF(duration, ws_protocol_bresser_3ch_const.te_short * 3) <
                           ws_protocol_bresser_3ch_const.te_delta) {
            if(DURATION_DIFF(
                   instance->decoder.te_last, ws_protocol_bresser_3ch_const.te_short * 12) <
               ws_protocol_bresser_3ch_const.te_delta * 2) {
                // End of sync after 4*750 (12*250) high values, start reading the message
                instance->decoder.parser_step = Bresser3chDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = Bresser3chDecoderStepPreambleUp;
            }
        } else {
            instance->decoder.parser_step = Bresser3chDecoderStepReset;
        }
        break;

    case Bresser3chDecoderStepPreambleUp:
        if(level && DURATION_DIFF(duration, ws_protocol_bresser_3ch_const.te_short * 3) <
                        ws_protocol_bresser_3ch_const.te_delta) {
            instance->decoder.te_last = instance->decoder.te_last + duration;
            instance->decoder.parser_step = Bresser3chDecoderStepPreambleDn;
        } else {
            instance->decoder.parser_step = Bresser3chDecoderStepReset;
        }
        break;

    case Bresser3chDecoderStepSaveDuration:
        if(instance->decoder.decode_count_bit ==
           ws_protocol_bresser_3ch_const.min_count_bit_for_found) {
            if(ws_protocol_bresser_3ch_check(instance)) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                ws_protocol_bresser_3ch_extract_data(&instance->generic);

                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
            }
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = Bresser3chDecoderStepReset;
        } else if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = Bresser3chDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = Bresser3chDecoderStepReset;
        }
        break;

    case Bresser3chDecoderStepCheckDuration:
        if(!level) {
            if(DURATION_DIFF(instance->decoder.te_last, ws_protocol_bresser_3ch_const.te_short) <
                   ws_protocol_bresser_3ch_const.te_delta &&
               DURATION_DIFF(duration, ws_protocol_bresser_3ch_const.te_long) <
                   ws_protocol_bresser_3ch_const.te_delta) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Bresser3chDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(instance->decoder.te_last, ws_protocol_bresser_3ch_const.te_long) <
                    ws_protocol_bresser_3ch_const.te_delta &&
                DURATION_DIFF(duration, ws_protocol_bresser_3ch_const.te_short) <
                    ws_protocol_bresser_3ch_const.te_delta) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Bresser3chDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = Bresser3chDecoderStepReset;
        } else
            instance->decoder.parser_step = Bresser3chDecoderStepReset;
        break;
    }
}

uint8_t ws_protocol_decoder_bresser_3ch_get_hash_data(void* context) {
    furi_assert(context);
    WSProtocolDecoderBresser3ch* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus ws_protocol_decoder_bresser_3ch_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    WSProtocolDecoderBresser3ch* instance = context;
    return ws_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    ws_protocol_decoder_bresser_3ch_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    WSProtocolDecoderBresser3ch* instance = context;
    return ws_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, ws_protocol_bresser_3ch_const.min_count_bit_for_found);
}

void ws_protocol_decoder_bresser_3ch_get_string(void* context, FuriString* output) {
    furi_assert(context);
    WSProtocolDecoderBresser3ch* instance = context;
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

void* ws_protocol_encoder_bresser_3ch_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    WSProtocolEncoderBresser3ch* instance = malloc(sizeof(WSProtocolEncoderBresser3ch));
    instance->base.protocol = &ws_protocol_bresser_3ch;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 8;
    instance->encoder.size_upload = BRESSER_3CH_UPLOAD_MAX_SIZE;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void ws_protocol_encoder_bresser_3ch_free(void* context) {
    furi_assert(context);
    WSProtocolEncoderBresser3ch* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/**
 * Build the PWM upload for one repeat of the 40-bit Bresser-3CH frame.
 * Unlike the other WS protocols here, this one is PWM (fixed bit period,
 * duty cycle carries the value), not PPM: a 0 bit is pulse te_short/gap
 * te_long, a 1 bit is pulse te_long/gap te_short. The sync preamble is 4
 * pairs of te_short*3 pulse/gap.
 *
 * ws_protocol_decoder_bresser_3ch_feed finalizes a frame as soon as it has
 * 40 bits AND receives one more event, using that event only as a trigger
 * (its actual level/duration is never checked) - it does not get reused as
 * the start of the next preamble. So each repeat here is prefixed with one
 * short throwaway LOW that gets consumed as that trigger, keeping all 4 real
 * preamble pulses intact for the following repeat.
 */
static bool
    ws_protocol_encoder_bresser_3ch_get_upload(WSProtocolEncoderBresser3ch* instance) {
    furi_assert(instance);

    size_t index = 0;
    size_t size_upload = BRESSER_3CH_UPLOAD_PAIRS_PER_REPEAT * 2;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    }
    instance->encoder.size_upload = size_upload;

    // Throwaway pair, absorbed by the previous repeat's finalize-on-next-event.
    instance->encoder.upload[index++] = level_duration_make(true, 100);
    instance->encoder.upload[index++] = level_duration_make(false, 100);

    for(uint8_t i = 0; i < 4; i++) {
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)ws_protocol_bresser_3ch_const.te_short * 3);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)ws_protocol_bresser_3ch_const.te_short * 3);
    }

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        bool bit = bit_read(instance->generic.data, i - 1);
        instance->encoder.upload[index++] = level_duration_make(
            true,
            bit ? (uint32_t)ws_protocol_bresser_3ch_const.te_long :
                  (uint32_t)ws_protocol_bresser_3ch_const.te_short);
        instance->encoder.upload[index++] = level_duration_make(
            false,
            bit ? (uint32_t)ws_protocol_bresser_3ch_const.te_short :
                  (uint32_t)ws_protocol_bresser_3ch_const.te_long);
    }

    return true;
}

SubGhzProtocolStatus
    ws_protocol_encoder_bresser_3ch_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    WSProtocolEncoderBresser3ch* instance = context;
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
           ws_protocol_bresser_3ch_const.min_count_bit_for_found) {
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
            instance->encoder.repeat = 8;
        }

        if(!ws_protocol_encoder_bresser_3ch_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void ws_protocol_encoder_bresser_3ch_stop(void* context) {
    WSProtocolEncoderBresser3ch* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration ws_protocol_encoder_bresser_3ch_yield(void* context) {
    WSProtocolEncoderBresser3ch* instance = context;

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
