#include "video.h"

#include "gameboy.h"
#include "cpu.h"
#include "bitwise.h"

using bitwise::check_bit;

/* ---------------------------------------------------------- tile decode */

/* s_tile_lut[b] spreads the 8 bits of a tile-data byte into 8 2-bit pixel
 * fields, leftmost pixel (bit 7) in the LOWEST field -- the same LSB-first
 * packing the framebuffer uses, so whole bytes can be emitted directly.
 * s_tile_lut_rev is the X-flipped variant for sprites. 1 KB total. */
static u16 s_tile_lut[256];
static u16 s_tile_lut_rev[256];
static bool s_luts_ready = false;

Video::Video(Gameboy& inGb)
    : gb(inGb) {
    if(!s_luts_ready) {
        for(uint b = 0; b < 256; b++) {
            u16 fwd = 0, rev = 0;
            for(uint k = 0; k < 8; k++) {
                u16 bit = (u16)((b >> (7 - k)) & 1);
                fwd |= (u16)(bit << (2 * k));
                rev |= (u16)(bit << (2 * (7 - k)));
            }
            s_tile_lut[b] = fwd;
            s_tile_lut_rev[b] = rev;
        }
        s_luts_ready = true;
    }
}

/* Palette LUT: maps a packed 2bpp byte (4 pixels) through BGP in one
 * lookup. Rebuilt only when the game writes a new BGP value. */
auto Video::bg_pal_lut() -> const u8* {
    u8 bgp = bg_palette.value();
    if(!bgp_lut_valid || bgp != bgp_lut_cached_for) {
        u8 shade[4] = {
            (u8)(bgp & 0x3),
            (u8)((bgp >> 2) & 0x3),
            (u8)((bgp >> 4) & 0x3),
            (u8)((bgp >> 6) & 0x3),
        };
        for(uint v = 0; v < 256; v++) {
            bgp_lut[v] = (u8)(
                shade[v & 3] | (shade[(v >> 2) & 3] << 2) | (shade[(v >> 4) & 3] << 4) |
                (shade[(v >> 6) & 3] << 6));
        }
        bgp_lut_cached_for = bgp;
        bgp_lut_valid = true;
    }
    return bgp_lut;
}

/* Video::tick is called once per emulated instruction; it lives as an
 * inline definition at the bottom of gameboy.h. Mode-transition work
 * (rendering, interrupts) stays out-of-line in this file. */

void Video::mode_transition_vram_end() {
    current_mode = VideoMode::HBLANK;

    bool hblank_interrupt = check_bit(lcd_status.value(), 3);

    if(hblank_interrupt) {
        gb.cpu.interrupt_flag.set_bit_to(1, true);
    }

    bool ly_coincidence_interrupt = check_bit(lcd_status.value(), 6);
    bool ly_coincidence = ly_compare.value() == line.value();
    if(ly_coincidence_interrupt && ly_coincidence) {
        gb.cpu.interrupt_flag.set_bit_to(1, true);
    }
    lcd_status.set_bit_to(2, ly_coincidence);

    lcd_status.set_bit_to(1, false);
    lcd_status.set_bit_to(0, false);
}

void Video::mode_transition_hblank_end() {
    if(!skip_render) write_scanline(line.value());
    line.increment();

    /* Line 145 (index 144) is the first line of VBLANK */
    if(line == 144) {
        current_mode = VideoMode::VBLANK;
        lcd_status.set_bit_to(1, false);
        lcd_status.set_bit_to(0, true);
        gb.cpu.interrupt_flag.set_bit_to(0, true);
    } else {
        lcd_status.set_bit_to(1, true);
        lcd_status.set_bit_to(0, false);
        current_mode = VideoMode::ACCESS_OAM;
    }
}

void Video::mode_transition_vblank_line() {
    line.increment();

    /* LY=LYC STAT interrupt must also fire for lines 144-153: games that
     * wait for a coincidence inside vblank hung without this (it was only
     * evaluated at the mode3->hblank transition of visible lines) */
    if(check_bit(lcd_status.value(), 6) && ly_compare.value() == line.value()) {
        gb.cpu.interrupt_flag.set_bit_to(1, true);
    }
    lcd_status.set_bit_to(2, ly_compare.value() == line.value());

    /* Line 155 (index 154) is the last line */
    if(line == 154) {
        if(!skip_render) {
            write_sprites();
            draw();
            buffer.reset();
        } else {
            draw(); /* still notify the frontend for pacing */
        }
        line.reset();
        current_mode = VideoMode::ACCESS_OAM;
        lcd_status.set_bit_to(1, true);
        lcd_status.set_bit_to(0, false);
    }
}

auto Video::display_enabled() const -> bool {
    return check_bit(control_byte, 7);
}
auto Video::window_tile_map() const -> bool {
    return check_bit(control_byte, 6);
}
auto Video::window_enabled() const -> bool {
    return check_bit(control_byte, 5);
}
auto Video::bg_window_tile_data() const -> bool {
    return check_bit(control_byte, 4);
}
auto Video::bg_tile_map_display() const -> bool {
    return check_bit(control_byte, 3);
}
auto Video::sprite_size() const -> bool {
    return check_bit(control_byte, 2);
}
auto Video::sprites_enabled() const -> bool {
    return check_bit(control_byte, 1);
}
auto Video::bg_enabled() const -> bool {
    return check_bit(control_byte, 0);
}

void Video::write_scanline(u8 current_line) {
    if(!display_enabled()) {
        return;
    }

    /* Lines the frontend never displays are not worth rendering */
    if(row_mask && current_line < GAMEBOY_HEIGHT && !row_mask[current_line]) {
        return;
    }

    if(bg_enabled()) {
        draw_bg_line(current_line);
    }

    if(window_enabled()) {
        draw_window_line(current_line);
    }
}

void Video::write_sprites() {
    if(!sprites_enabled()) {
        return;
    }

    for(uint sprite_n = 0; sprite_n < 40; sprite_n++) {
        draw_sprite(sprite_n);
    }
}

/* Byte-oriented tile-strip renderer: emits `count` pixels of one tile-map
 * row into the packed framebuffer row `dst_row`, starting at screen pixel
 * `dst_x`, sourcing map pixels from `src_px` (wraps at 256). Pixels flow
 * through a small bit-queue so the bulk of the line is written as whole
 * palette-mapped bytes (4 px per store) instead of per-pixel RMW packing:
 * ~7x fewer operations per line than the old per-pixel path. */
void Video::render_strip(
    u8* dst_row,
    uint dst_x,
    uint count,
    uint map_row_base, /* VRAM offset of the tile map row (32 entries) */
    uint src_px,
    uint tile_line_off, /* 2 * (row within the tile) */
    bool use_tile_set_zero,
    const u8* pal_lut) {
    uint tile_x = (src_px >> 3) & 31;
    uint fine = src_px & 7;
    uint tset_base = use_tile_set_zero ? 0x0000u : 0x0800u;

    /* bit-queue of pending 2bpp pixels, LSB = next pixel to emit */
    u32 acc = 0;
    uint nbits = 0;

#define FETCH_TILE_ROW() \
    do { \
        u8 tid = video_ram[map_row_base + tile_x]; \
        tile_x = (tile_x + 1) & 31; \
        uint toff = use_tile_set_zero ? \
                        (uint)tid * TILE_BYTES : \
                        (uint)((s8)tid + 128) * TILE_BYTES; \
        uint la = tset_base + toff + tile_line_off; \
        u16 v = (u16)(s_tile_lut[video_ram[la]] | (s_tile_lut[video_ram[la + 1]] << 1)); \
        acc |= (u32)v << nbits; \
        nbits += 16; \
    } while(0)

    /* prime the queue, discarding the fine-scroll pixels */
    FETCH_TILE_ROW();
    acc >>= fine * 2;
    nbits -= fine * 2;

    /* head: single pixels until the destination is byte-aligned */
    while(count && (dst_x & 3)) {
        if(nbits < 2) FETCH_TILE_ROW();
        uint sh = (dst_x & 3) * 2;
        u8* p = dst_row + (dst_x >> 2);
        *p = (u8)((*p & ~(3u << sh)) | ((pal_lut[acc & 3] & 3u) << sh));
        acc >>= 2;
        nbits -= 2;
        dst_x++;
        count--;
    }

    /* body: whole bytes, palette applied 4 pixels at a time */
    u8* out = dst_row + (dst_x >> 2);
    while(count >= 4) {
        if(nbits < 8) FETCH_TILE_ROW();
        *out++ = pal_lut[acc & 0xFF];
        acc >>= 8;
        nbits -= 8;
        count -= 4;
        dst_x += 4;
    }

    /* tail */
    while(count) {
        if(nbits < 2) FETCH_TILE_ROW();
        uint sh = (dst_x & 3) * 2;
        u8* p = dst_row + (dst_x >> 2);
        *p = (u8)((*p & ~(3u << sh)) | ((pal_lut[acc & 3] & 3u) << sh));
        acc >>= 2;
        nbits -= 2;
        dst_x++;
        count--;
    }
#undef FETCH_TILE_ROW
}

void Video::draw_bg_line(uint current_line) {
    /* Note: tileset two uses signed numbering to share half the tiles with
     * tileset one */
    bool use_tile_set_zero = bg_window_tile_data();
    bool use_tile_map_zero = !bg_tile_map_display();

    u8* dst = buffer.row_ptr(current_line);
    if(!dst) return;

    uint scrolled_y = (current_line + scroll_y.value()) % BG_MAP_SIZE;
    uint map_row_base = (use_tile_map_zero ? (TILE_MAP_ZERO_ADDRESS - 0x8000) :
                                             (TILE_MAP_ONE_ADDRESS - 0x8000)) +
                        (scrolled_y / TILE_HEIGHT_PX) * TILES_PER_LINE;

    render_strip(
        dst,
        0,
        GAMEBOY_WIDTH,
        map_row_base,
        scroll_x.value(),
        (scrolled_y % TILE_HEIGHT_PX) * 2,
        use_tile_set_zero,
        bg_pal_lut());
}

void Video::draw_window_line(uint current_line) {
    bool use_tile_set_zero = bg_window_tile_data();
    bool use_tile_map_zero = !window_tile_map();

    uint scrolled_y = current_line - window_y.value();
    if(scrolled_y >= GAMEBOY_HEIGHT) {
        return;
    }

    /* the window covers screen pixels from WX-7 onward, sourcing the
     * window map from column 0 */
    uint wx = window_x.value();
    uint start_x = wx >= 7 ? wx - 7 : 0;
    if(start_x >= GAMEBOY_WIDTH) return;

    u8* dst = buffer.row_ptr(current_line);
    if(!dst) return;

    uint map_row_base = (use_tile_map_zero ? (TILE_MAP_ZERO_ADDRESS - 0x8000) :
                                             (TILE_MAP_ONE_ADDRESS - 0x8000)) +
                        (scrolled_y / TILE_HEIGHT_PX) * TILES_PER_LINE;

    render_strip(
        dst,
        start_x,
        GAMEBOY_WIDTH - start_x,
        map_row_base,
        wx >= 7 ? 0 : 7 - wx,
        (scrolled_y % TILE_HEIGHT_PX) * 2,
        use_tile_set_zero,
        bg_pal_lut());
}

void Video::draw_sprite(const uint sprite_n) {
    /* Each sprite is represented by 4 bytes */
    u16 oam_start = static_cast<u16>(sprite_n * SPRITE_BYTES);

    u8 sprite_y = gb.mmu.oam_ram[oam_start];
    u8 sprite_x = gb.mmu.oam_ram[oam_start + 1];

    /* Offscreen sprites are not drawn */
    if(sprite_y == 0 || sprite_y >= 160) {
        return;
    }
    if(sprite_x == 0 || sprite_x >= 168) {
        return;
    }

    uint sprite_height = sprite_size() ? 16 : 8;

    u8 pattern_n = gb.mmu.oam_ram[oam_start + 2];
    u8 sprite_attrs = gb.mmu.oam_ram[oam_start + 3];

    /* Bits 0-3 are used only for CGB */
    bool use_palette_1 = check_bit(sprite_attrs, 4);
    bool flip_x = check_bit(sprite_attrs, 5);
    bool flip_y = check_bit(sprite_attrs, 6);
    bool obj_behind_bg = check_bit(sprite_attrs, 7);

    /* 4-entry shade table instead of a per-pixel palette switch */
    u8 obp = use_palette_1 ? sprite_palette_1.value() : sprite_palette_0.value();
    u8 shade[4] = {
        (u8)(obp & 0x3),
        (u8)((obp >> 2) & 0x3),
        (u8)((obp >> 4) & 0x3),
        (u8)((obp >> 6) & 0x3),
    };

    uint tile_offset = pattern_n * TILE_BYTES;

    int start_y = sprite_y - 16;
    int start_x = sprite_x - 8;

    const u16* lut = flip_x ? s_tile_lut_rev : s_tile_lut;

    for(uint y = 0; y < sprite_height; y++) {
        int screen_y = start_y + static_cast<int>(y);
        if(screen_y < 0 || screen_y >= static_cast<int>(GAMEBOY_HEIGHT)) continue;
        if(row_mask && !row_mask[screen_y]) continue;

        u8* row = buffer.row_ptr(static_cast<uint>(screen_y));
        if(!row) continue;

        uint src_y = !flip_y ? y : sprite_height - y - 1;

        uint line_addr = tile_offset + src_y * 2; /* relative to tile set zero */

        /* all 8 pixel colors of this sprite row, LSB-first left-to-right
         * (flip handled by the reversed LUT) */
        u16 v = (u16)(lut[video_ram[line_addr]] | (lut[video_ram[line_addr + 1]] << 1));
        if(v == 0) continue; /* fully transparent row: common, skip */

        for(uint x = 0; x < TILE_WIDTH_PX; x++, v >>= 2) {
            uint color = v & 3;
            if(color == 0) continue; /* color 0 is transparent */

            int screen_x = start_x + static_cast<int>(x);
            if(screen_x < 0 || screen_x >= static_cast<int>(GAMEBOY_WIDTH)) continue;

            uint sh = ((uint)screen_x & 3) * 2;
            u8* p = row + ((uint)screen_x >> 2);

            /* Note: same behaviour as upstream - the priority bit compares
             * the final shade rather than the logical color 0 */
            if(obj_behind_bg && ((*p >> sh) & 3) != SHADE_WHITE) {
                continue;
            }

            *p = (u8)((*p & ~(3u << sh)) | (shade[color] << sh));
        }
    }
}

void Video::draw() {
    if(vblank_callback) vblank_callback(vblank_ctx);
}
