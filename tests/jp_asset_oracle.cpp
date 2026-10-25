#include "arm_decode.h"
#include "bus.h"
#include "cpu_state.h"
#include "interpreter.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class AssetBus : public armv4t::Bus {
public:
    std::vector<std::uint8_t> rom;
    std::array<std::uint8_t, 0x40000> ewram{};
    std::array<std::uint8_t, 0x8000> iwram{};
    std::uint8_t read8(std::uint32_t a) override {
        if (a >= 0x08000000 && a - 0x08000000 < rom.size()) return rom[a-0x08000000];
        if (a >= 0x02000000 && a < 0x02040000) return ewram[a-0x02000000];
        if (a >= 0x03000000 && a < 0x03008000) return iwram[a-0x03000000];
        throw std::runtime_error("Unexpected decoder read");
    }
    std::uint16_t read16(std::uint32_t a) override { return read8(a) | (read8(a+1) << 8); }
    std::uint32_t read32(std::uint32_t a) override { return read16(a) | (std::uint32_t(read16(a+2)) << 16); }
    void write8(std::uint32_t a, std::uint8_t v) override {
        if (a >= 0x02000000 && a < 0x02040000) ewram[a-0x02000000]=v;
        else if (a >= 0x03000000 && a < 0x03008000) iwram[a-0x03000000]=v;
        else throw std::runtime_error("Unexpected decoder write");
    }
    void write16(std::uint32_t a, std::uint16_t v) override { write8(a,v); write8(a+1,v>>8); }
    void write32(std::uint32_t a, std::uint32_t v) override { write16(a,v); write16(a+2,v>>16); }
    std::uint32_t decode(std::uint32_t source, std::uint32_t size) {
        if (size > ewram.size()) throw std::runtime_error("Oracle output is too large");
        ewram.fill(0xA5);
        iwram.fill(0);
        armv4t::CPUState cpu{};
        cpu.cpsr.mode=static_cast<std::uint8_t>(armv4t::Mode::System);
        cpu.R[0]=source+6; cpu.R[1]=0x02000000; cpu.R[2]=size;
        cpu.R[13]=0x03007E00; cpu.R[14]=0x03007FF0; cpu.R[15]=0x08084C18;
        for (unsigned step=0; step<20000000; ++step) {
            if (cpu.R[15]==0x03007FF0) return cpu.R[0];
            if (cpu.thumb) throw std::runtime_error("Decoder unexpectedly entered Thumb state");
            const auto insn=armv4t::ArmDecoder::decode(read32(cpu.R[15]),cpu.R[15]);
            const auto result=armv4t::Interpreter::step(cpu,*this,insn);
            if (result!=armv4t::Interpreter::Result::Normal && result!=armv4t::Interpreter::Result::Branched)
                throw std::runtime_error("Original decoder instruction failed");
        }
        throw std::runtime_error("Original decoder did not return");
    }
};

int main(int argc, char** argv) {
    try {
        if (argc!=3) throw std::runtime_error("Usage: seed_jp_asset_oracle JP_ROM.gba IMPORT_DIRECTORY");
        auto bus=std::make_unique<AssetBus>();
        std::ifstream rom(argv[1],std::ios::binary);
        if (!rom) throw std::runtime_error("Cannot read ROM");
        bus->rom.assign(std::istreambuf_iterator<char>(rom),{});
        if (bus->rom.size()!=0x800000 || bus->rom[0xAC]!='B' || bus->rom[0xAD]!='4' ||
            bus->rom[0xAE]!='2' || bus->rom[0xAF]!='J') throw std::runtime_error("Expected Japanese B42J ROM");
        const std::filesystem::path root=argv[2];
        std::ifstream cases(root/"oracle-cases.tsv");
        if (!cases) throw std::runtime_error("Run import_jp_assets.py first");
        unsigned count=0;
        std::uint64_t bytes=0;
        std::uint32_t address,size,consumed;
        std::string filename;
        while (cases >> std::hex >> address >> std::dec >> size >> consumed >> filename) {
            std::ifstream expected_file(root/filename,std::ios::binary);
            if (!expected_file) throw std::runtime_error("Missing imported resource");
            std::vector<std::uint8_t> expected(std::istreambuf_iterator<char>(expected_file),{});
            const auto end=bus->decode(address,size);
            if (expected.size()!=size || end!=address+consumed ||
                !std::equal(expected.begin(),expected.end(),bus->ewram.begin())) {
                std::fprintf(stderr,"Decoder mismatch: %s at 0x%08X\n",filename.c_str(),address);
                return 1;
            }
            ++count; bytes+=size;
        }
        if (!count || !cases.eof()) throw std::runtime_error("Invalid or empty oracle cases");
        std::printf("PASS: %u resources (%llu bytes) match the original JP ARM decoder.\n",count,
                    static_cast<unsigned long long>(bytes));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"%s\n",e.what()); return 1;
    }
}
