#pragma once

#include "definitions.h"

/* Cartridge with pluggable ROM page provider.
 *
 * Instead of holding the whole ROM in RAM (impossible on Flipper Zero for
 * anything above 32 KB), the cartridge asks the platform for pointers to
 * 8 KB ROM pages (page n = ROM offset n * 0x2000) whenever the game
 * switches banks. 8 KB granularity -- half a MBC bank -- doubles how many
 * cache slots fit in the same RAM and halves the SD stall of a cache miss,
 * which matters a lot for bank-switch heavy games (Pokemon switches banks
 * for music/code/data every frame). On the desktop test build the provider
 * just returns `rom + page * 0x2000`; on the Flipper it is backed by an
 * LRU cache streaming from the SD card.
 *
 * Supported mappers: ROM only, MBC1 (incl. upper bits / mode select),
 * MBC2 (built-in 512x4 RAM), MBC3 (no RTC), MBC5.
 */

using RomBankProvider = const u8* (*)(void* ctx, uint page);

enum class MBCType : u8 {
    None,
    MBC1,
    MBC2,
    MBC3,
    MBC5,
    Unsupported,
};

class Cartridge {
public:
    /* bank0 must stay valid for the lifetime of the cartridge */
    void init(
        const u8* bank0_data,
        uint rom_bank_count,
        MBCType mbc_type,
        u8* cart_ram,
        u32 cart_ram_size,
        RomBankProvider provider,
        void* provider_ctx);

    auto read(u16 addr) const -> u8 {
        if(addr < 0x4000) return bank0[addr];
        if(addr < 0x6000) return bankN_lo[addr - 0x4000];
        if(addr < 0x8000) {
            /* lazy: the hi half of a bank is only streamed in when the
             * game actually reads 0x6000-0x7FFF from it. Many switches
             * exist just to read a table at 0x4xxx; fetching both 8 KB
             * pages eagerly doubled the SD misses of streamed games. */
            if(!bankN_hi) fetch_hi_page();
            return bankN_hi[addr - 0x6000];
        }
        /* 0xA000 - 0xBFFF: cartridge RAM */
        return read_ram(addr);
    }

    void write(u16 addr, u8 value);

    auto get_ram() -> u8* { return ram; }
    auto get_ram_size() const -> u32 { return ram_size; }

    /* Header helpers (operate on the first bank) */
    static auto parse_mbc(u8 cartridge_type_byte) -> MBCType;
    static auto has_battery(u8 cartridge_type_byte) -> bool;
    static auto rom_bank_count_from_header(u8 rom_size_byte) -> uint;
    static auto ram_size_from_header(u8 ram_size_byte, MBCType mbc) -> u32;

private:
    auto read_ram(u16 addr) const -> u8;
    void write_ram(u16 addr, u8 value);
    void update_rom_bank();
    void fetch_hi_page() const;

    const u8* bank0 = nullptr;
    mutable const u8* bankN_lo = nullptr; /* 0x4000 - 0x5FFF */
    mutable const u8* bankN_hi = nullptr; /* 0x6000 - 0x7FFF, lazy (see read) */
    uint cur_bank = 0xFFFFFFFFu; /* currently mapped bank (memo) */

    u8* ram = nullptr;
    u32 ram_size = 0;

    RomBankProvider provider = nullptr;
    void* provider_ctx = nullptr;

    MBCType mbc = MBCType::None;
    uint bank_count = 2;

    bool ram_enabled = false;
    bool advanced_banking_mode = false; /* MBC1 mode 1 */
    uint bank_low = 1; /* MBC1: 5 bits, MBC3: 7 bits, MBC5: 8 bits */
    uint bank_high = 0; /* MBC1: 2 bits, MBC5: 9th bit */
    uint ram_bank = 0;
};
