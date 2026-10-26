#include "../src/game_addresses.h"
#include "arm_decode.h"
#include "cpu_state.h"
#include "interpreter.h"
#include "gba_bios.h"
#include "gba_bus.h"
#include "bios_hle.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <algorithm>

namespace gba_address = seed::address::common;

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: seed_bios_fast_set_tests BIOS_FILE");
        gba::GbaBios bios;
        std::string error;
        require(bios.load_from_file(argv[1], std::string{}, &error), "BIOS load failed");
        auto bus = std::make_unique<gba::GbaBus>();
        bus->set_bios(&bios);
        constexpr unsigned src = gba_address::kEwramBegin,
                           dst = gba_address::kOracleCopyDestination,
                           stop = gba_address::kHookReturnMarker;
        unsigned cases = 0;
        for (const auto count :
             {0u, 1u, 4u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u})
            for (bool fill : {false, true}) {
                bus->set_bios_access_enabled(true);
                for (unsigned i = 0; i < 320; i += 4)
                    bus->write32(src + i, 0x17263540u + i);
                for (unsigned i = 0; i < 320; i += 4)
                    bus->write32(dst + i, 0xA5A5A5A5u);
                armv4t::CPUState cpu{};
                cpu.cpsr.mode = static_cast<std::uint8_t>(armv4t::Mode::System);
                cpu.R[0] = src;
                cpu.R[1] = dst;
                cpu.R[2] = count | (fill ? 1u << 24 : 0);
                cpu.R[13] = gba_address::kOracleStackTop;
                cpu.R[14] = stop;
                cpu.R[15] = gba_address::kBiosCpuFastSet;
                unsigned steps = 0;
                while (cpu.R[15] != stop && steps++ < 20000) {
                    const auto insn = armv4t::ArmDecoder::decode(bus->read32(cpu.R[15]), cpu.R[15]);
                    const auto result = armv4t::Interpreter::step(cpu, *bus, insn);
                    require(result == armv4t::Interpreter::Result::Normal ||
                                result == armv4t::Interpreter::Result::Branched,
                            "Original BIOS execution failed");
                }
                require(cpu.R[15] == stop, "Original BIOS did not return");
                std::array<std::uint32_t, 80> expected{};
                for (unsigned i = 0; i < 80; ++i) {
                    expected[i] = bus->read32(dst + 4 * i);
                    bus->write32(dst + 4 * i, 0xA5A5A5A5u);
                }
                runtime_init(bus.get());
                gbarecomp::set_active_bus(bus.get());
                g_cpu = {};
                g_cpu.cpsr = 0x1F;
                g_cpu.R[0] = src;
                g_cpu.R[1] = dst;
                g_cpu.R[2] = count | (fill ? 1u << 24 : 0);
                gba::bios_hle_set_mode(gba::BiosHleMode::On);
                require(g_bios_hle_hook && g_bios_hle_hook(0xC), "HLE service missing");
                for (unsigned i = 0; i < 80; ++i)
                    if (bus->read32(dst + 4 * i) != expected[i]) {
                        std::fprintf(stderr,
                                     "CpuFastSet mismatch count=%u fill=%u at word %u "
                                     "expected=%08X actual=%08X\n",
                                     count, unsigned(fill), i, expected[i],
                                     bus->read32(dst + 4 * i));
                        throw std::runtime_error("HLE differs from supplied BIOS");
                    }
                gba::bios_hle_set_mode(gba::BiosHleMode::Off);
                gbarecomp::set_active_bus(nullptr);
                runtime_shutdown();
                ++cases;
            }
        std::printf("PASS: %u CpuFastSet copy/fill cases matched original BIOS instructions, "
                    "including partial eight-word blocks.\n",
                    cases);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
