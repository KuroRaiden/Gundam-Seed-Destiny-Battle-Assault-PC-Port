#pragma once
#include "local_vs.h"
#include <filesystem>
#include <map>
#include <vector>

namespace seed {
// Native compositor. The pack is built locally from the verified donor ROM;
// no JP simulation runs in the shipped game process.
class JpSelectionArt {
  public:
    void load(const std::filesystem::path& path);
    const std::vector<std::uint16_t>& compose(const LocalVsSelection& selection);

  private:
    struct Image {
        unsigned w, h;
        std::vector<std::uint16_t> pixels;
    };
    std::map<unsigned, Image> images_;
    std::vector<std::uint16_t> pixels_;
    void blit(unsigned id, int x, int y, bool flip = false);
    void rect(int x, int y, int w, int h, std::uint16_t color);
    void text(int x, int y, const std::string& value, std::uint16_t color);
};
} // namespace seed
