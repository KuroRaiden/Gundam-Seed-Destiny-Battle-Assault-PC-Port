#include "../src/game_addresses.h"
#include "input_state.h"
#include "bus.h"
#include "cpu_state.h"
#include "interpreter.h"
#include "thumb_decode.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gba_address = seed::address::common;

// Execute the original cartridge's two input routines as an independent oracle.
// Timing and other hardware are irrelevant for these memory-only routines.
class OracleBus : public armv4t::Bus {
  public:
    std::vector<std::uint8_t> rom;
    std::array<std::uint8_t, 0x8000> ram{};
    std::uint8_t read8(std::uint32_t addr) override {
        if (addr >= gba_address::kRomBegin && addr - gba_address::kRomBegin < rom.size())
            return rom[addr - gba_address::kRomBegin];
        if (addr >= gba_address::kIwramBegin && addr < gba_address::kIwramEnd)
            return ram[addr - gba_address::kIwramBegin];
        throw std::runtime_error("Unexpected oracle read");
    }
    std::uint16_t read16(std::uint32_t a) override {
        return read8(a) | (read8(a + 1) << 8);
    }
    std::uint32_t read32(std::uint32_t a) override {
        return read16(a) | (std::uint32_t(read16(a + 2)) << 16);
    }
    void write8(std::uint32_t a, std::uint8_t value) override {
        if (a < gba_address::kIwramBegin || a >= gba_address::kIwramEnd)
            throw std::runtime_error("Unexpected oracle write");
        ram[a - gba_address::kIwramBegin] = value;
    }
    void write16(std::uint32_t a, std::uint16_t v) override {
        write8(a, v);
        write8(a + 1, v >> 8);
    }
    void write32(std::uint32_t a, std::uint32_t v) override {
        write16(a, v);
        write16(a + 2, v >> 16);
    }
    void advance(std::uint32_t routine, std::uint16_t buttons) {
        armv4t::CPUState cpu{};
        cpu.thumb = cpu.cpsr.t = true;
        cpu.cpsr.mode = static_cast<std::uint8_t>(armv4t::Mode::System);
        cpu.R[0] = buttons;
        cpu.R[13] = gba_address::kOracleStackTop;
        cpu.R[14] = gba_address::kOracleThumbReturnMarker;
        cpu.R[15] = routine;
        for (unsigned step = 0; step < 2000; ++step) {
            if (cpu.R[15] == gba_address::kOracleReturnMarker)
                return;
            if (!cpu.thumb)
                throw std::runtime_error("Oracle unexpectedly entered ARM state");
            auto instruction = armv4t::ThumbDecoder::decode(read16(cpu.R[15]), cpu.R[15]);
            auto result = armv4t::Interpreter::step(cpu, *this, instruction);
            if (result != armv4t::Interpreter::Result::Normal &&
                result != armv4t::Interpreter::Result::Branched)
                throw std::runtime_error("Oracle instruction failed");
        }
        throw std::runtime_error("Oracle failed to return");
    }
};

void check(bool ok, unsigned frame, unsigned player, const char* field) {
    if (!ok) {
        std::fprintf(stderr, "Mismatch at frame %u player %u field %s\n", frame, player + 1, field);
        throw std::runtime_error("Native input differs from cartridge");
    }
}

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: seed_input_oracle US_ROM.gba");
        OracleBus oracle;
        std::ifstream input(argv[1], std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot read ROM");
        oracle.rom.assign(std::istreambuf_iterator<char>(input), {});
        if (oracle.rom.size() != 0x400000 || oracle.rom[0xAC] != 'B' || oracle.rom[0xAD] != 'G' ||
            oracle.rom[0xAE] != 'N' || oracle.rom[0xAF] != 'E')
            throw std::runtime_error("Expected US BGNE cartridge");
        seed::MatchInputs native;
        std::uint32_t rng = 0x53454544;
        for (unsigned frame = 0; frame < 4096; ++frame) {
            for (unsigned player = 0; player < 2; ++player) {
                rng ^= rng << 13;
                rng ^= rng >> 17;
                rng ^= rng << 5;
                // Long holds cover repeat timing; staggered masks cover independent edges.
                const auto buttons = static_cast<std::uint16_t>(
                    frame < 1024 ? ((frame / (31 + player * 12)) * 37u) & 0x03FF : rng & 0xFFFF);
                native.players[player].advance(buttons);
                const auto& a = seed::kUsInputAddresses[player];
                oracle.advance(a.routine, buttons);
                const auto& s = native.players[player];
                for (const auto pair : std::array<std::pair<std::uint32_t, std::uint16_t>, 6>{
                         {{a.held, s.held},
                          {a.previous, s.previous},
                          {a.pressed, s.pressed},
                          {a.released, s.released},
                          {a.repeat, s.repeat},
                          {a.recent_repress, s.recent_repress}}})
                    check(oracle.read16(pair.first) == pair.second, frame, player, "input fields");
                for (unsigned i = 0; i < 10; ++i)
                    check(oracle.read8(a.repeat_ticks + i) == s.repeat_ticks[i], frame, player,
                          "repeat ticks");
                for (unsigned i = 0; i < 16; ++i)
                    check(oracle.read16(a.history + 2 * i) == s.history[i], frame, player,
                          "history");
            }
        }
        std::puts("PASS: 4096 frames x 2 players match original US Thumb routines (8192 calls).");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
