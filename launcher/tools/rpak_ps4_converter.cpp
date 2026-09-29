#include "northstar_ps4/rpak_texture_converter.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;
using namespace northstar::ps4::mods;

static bool ReadFile(const fs::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) return false;
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    return bytes.empty() || static_cast<bool>(input.read(
        reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())));
}

static bool WriteFile(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    return output && (bytes.empty() || static_cast<bool>(output.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))));
}

static std::string StreamBasename(std::string path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) path.erase(0, slash + 1);
    return path;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: rpak_ps4_converter <input.rpak> <output-directory>\n";
        return 2;
    }
    const fs::path inputPath = fs::absolute(fs::path(argv[1]));
    const fs::path outputDirectory = fs::absolute(fs::path(argv[2]));
    std::vector<std::uint8_t> rpak;
    if (!ReadFile(inputPath, rpak)) {
        std::cerr << "could not read " << inputPath.string() << "\n";
        return 1;
    }
    std::vector<std::string> references;
    if (!ParseRpakStarpakReferences(rpak.data(), rpak.size(), references)) {
        std::cerr << "could not parse STARPak references in " << inputPath.string() << "\n";
        return 1;
    }
    std::vector<std::vector<std::uint8_t>> starpaks(references.size());
    std::vector<fs::path> streamNames;
    streamNames.reserve(references.size());
    for (std::size_t i = 0; i < references.size(); ++i) {
        const std::string basename = StreamBasename(references[i]);
        if (basename.empty()) {
            std::cerr << "empty STARPak basename\n";
            return 1;
        }
        const fs::path name(basename);
        if (std::find(streamNames.begin(), streamNames.end(), name) != streamNames.end()) {
            std::cerr << "duplicate STARPak basename " << basename << "\n";
            return 1;
        }
        const fs::path source = inputPath.parent_path() / name;
        if (!ReadFile(source, starpaks[i])) {
            // An RPak may name a stream set without using it. The converter
            // only requires a file when a texture descriptor points into it.
            starpaks[i].clear();
        }
        streamNames.push_back(name);
    }

    RpakPs4ConversionReport report;
    std::string error;
    if (!ConvertRpakTexturesToPs4(rpak, starpaks, report, error)) {
        std::cerr << "conversion refused: " << error << "\n";
        return 1;
    }
    std::error_code ec;
    fs::create_directories(outputDirectory, ec);
    if (ec) {
        std::cerr << "could not create output directory: " << ec.message() << "\n";
        return 1;
    }
    const fs::path rpakOutput = outputDirectory / inputPath.filename();
    if (!WriteFile(rpakOutput, rpak)) {
        std::cerr << "could not write " << rpakOutput.string() << "\n";
        return 1;
    }
    for (std::size_t i = 0; i < starpaks.size(); ++i) {
        if (starpaks[i].empty()) continue;
        const fs::path output = outputDirectory / streamNames[i];
        if (!WriteFile(output, starpaks[i])) {
            std::cerr << "could not write " << output.string() << "\n";
            return 1;
        }
    }
    std::cout << "converted=" << report.textures
              << " permanent_before=" << report.permanentBytesBefore
              << " permanent_after=" << report.permanentBytesAfter
              << " streamed_blocks=" << report.streamedBlocks << "\n";
    return 0;
}
