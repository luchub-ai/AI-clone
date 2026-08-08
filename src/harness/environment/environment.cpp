#include "environment.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <vector>

#ifdef _WIN32
    #define POPEN _popen
    #define PCLOSE _pclose
#else
    #define POPEN popen
    #define PCLOSE pclose
#endif

namespace fs = std::filesystem;

namespace {
// C++20: std::span<const char> doc mot "khung" (chunk) da duoc fgets() ghi
// vao, tim '\0' NGAY TRONG BIEN CUA SPAN roi moi cat thanh string_view.
// Khac voi goi buffer.data() truc tiep vao result += (dua vao strlen() mu
// quang tren toan bo mang, tin fgets() luon null-terminate dung cho), o
// day con tro VA kich thuoc bien di chung trong 1 gia tri span - neu sau
// nay ai do doi buffer sang kieu khac hoac truyen nham kich thuoc, loi se
// lo ra ngay o day thay vi doc tran ra ngoai vung nho da cap phat.
std::string_view rawChunkView(std::span<const char> chunk) {
    auto nul = std::find(chunk.begin(), chunk.end(), '\0');
    return std::string_view(chunk.data(), static_cast<std::size_t>(nul - chunk.begin()));
}
} // namespace

// ── path safety (dùng chung) ──────────────────────────────────
std::optional<fs::path> Environment::resolveSafePath(const std::string& relativePath) const {
    fs::path root = fs::weakly_canonical(fs::path(getWorkspace()));
    fs::path candidate = relativePath.empty() || relativePath == "."
                              ? root
                              : fs::weakly_canonical(root / relativePath);

    auto rootStr = root.string();
    auto candStr = candidate.string();
    if (candStr.compare(0, rootStr.size(), rootStr) != 0) return std::nullopt;
    if (candStr.size() > rootStr.size() &&
        candStr[rootStr.size()] != fs::path::preferred_separator) {
        return std::nullopt;
    }
    return candidate;
}

// ── file ops mặc định (Native/Sandbox dùng chung) ─────────────
std::optional<std::string> Environment::readFile(const std::string& relativePath) const {
    auto p = resolveSafePath(relativePath);
    if (!p || !fs::is_regular_file(*p)) return std::nullopt;
    std::ifstream ifs(*p, std::ios::binary);
    if (!ifs) return std::nullopt;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

std::optional<std::string> Environment::writeFile(const std::string& relativePath,
                                                     const std::string& content,
                                                     bool append) {
    auto p = resolveSafePath(relativePath);
    if (!p) return std::nullopt;
    std::error_code ec;
    fs::create_directories(p->parent_path(), ec);
    std::ofstream ofs(*p, append ? (std::ios::binary | std::ios::app) : std::ios::binary);
    if (!ofs) return std::nullopt;
    ofs << content;
    return std::string("OK");
}

std::optional<std::string> Environment::listDir(const std::string& relativePath) const {
    auto p = resolveSafePath(relativePath);
    if (!p || !fs::is_directory(*p)) return std::nullopt;
    std::ostringstream oss;
    for (const auto& entry : fs::directory_iterator(*p)) {
        oss << (entry.is_directory() ? "[DIR]  " : "[FILE] ")
            << entry.path().filename().string() << "\n";
    }
    return oss.str();
}

std::optional<std::string> Environment::deleteEntry(const std::string& relativePath) {
    auto p = resolveSafePath(relativePath);
    if (!p || !fs::exists(*p)) return std::nullopt;
    std::error_code ec;
    auto n = fs::remove_all(*p, ec);
    if (ec) return std::nullopt;
    return "Da xoa " + std::to_string(n) + " muc.";
}

std::optional<std::string> Environment::makeDir(const std::string& relativePath) {
    auto p = resolveSafePath(relativePath);
    if (!p) return std::nullopt;
    std::error_code ec;
    fs::create_directories(*p, ec);
    if (ec) return std::nullopt;
    return "OK";
}
    //[FIX - BUG]
std::string Environment::shellQuote(const std::string& command) {
    std::string quoted;
    quoted.reserve(command.size() + 2);
    quoted += '\'';
    for (char c : command) {
        if (c == '\'') quoted += "'\\''";  // dong quote, 1 quote literal, mo lai quote
        else quoted += c;
    }
    quoted += '\'';
    return quoted;
}

std::optional<std::string> Environment::popenCapture(const std::string& fullCommand) const {
    std::array<char, 4096> buffer{};
    std::span<char> buffer_view(buffer); // C++20: view co bien ro rang len std::array
    std::string result;
    std::unique_ptr<FILE, int(*)(FILE*)> pipe(POPEN(fullCommand.c_str(), "r"), PCLOSE);
    if (!pipe) return std::nullopt;
    while (fgets(buffer_view.data(), static_cast<int>(buffer_view.size()), pipe.get()) != nullptr) {
        result += rawChunkView(buffer_view);
    }
    return result;
}