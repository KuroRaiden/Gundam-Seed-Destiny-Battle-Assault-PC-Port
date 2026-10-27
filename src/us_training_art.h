#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
namespace seed {
// Word sprites are the actual US menu font. They are extracted locally;
// neither cartridge graphics nor generated caches are release inputs.
class UsTrainingArt {
  public:
    struct Image {
        int x = 0, y = 0;
        unsigned w = 0, h = 0;
        std::vector<std::uint8_t> pixels;
    };
    static void prepare(const std::filesystem::path& rom, const std::filesystem::path& cache);
    void load(const std::filesystem::path& cache);
    bool ready() const {
        return images_.size() == 19;
    }
    const Image& image(unsigned frame) const {
        return images_.at(frame);
    }

  private:
    std::vector<Image> images_;
};
} // namespace seed
