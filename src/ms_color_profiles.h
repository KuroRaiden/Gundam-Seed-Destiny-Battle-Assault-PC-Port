#pragma once
#include "ms_palette.h"
#include <filesystem>
#include <map>
#include <string>
namespace seed {
class MsColorProfiles {
  public:
    bool load(const std::filesystem::path& path, std::string& error);
    bool save(const std::filesystem::path& path, std::string& error) const;
    CustomPalette get(unsigned player, unsigned fighter, unsigned palette) const;
    bool set(unsigned player, unsigned fighter, unsigned palette, const CustomPalette& value);
    bool operator==(const MsColorProfiles&) const = default;

  private:
    std::map<unsigned, CustomPalette> values_;
};
} // namespace seed
