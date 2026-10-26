#pragma once
namespace seed {
// Import into a local cache from user-supplied files. No cartridge/art bytes
// are compiled into the importer or included in the release package.
using ImportJpSelectionArt = int (*)(const char* rom, const char* bios, const char* output);
} // namespace seed
