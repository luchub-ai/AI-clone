// Doc kich thuoc (width x height) truc tiep tu header cua file PNG, KHONG
// can decode toan bo anh / khong can thu vien ngoai (libpng, stb_image...).
// Dua theo dac ta PNG: 8 byte signature + chunk IHDR dau tien luon la
// "width(4 byte big-endian) + height(4 byte big-endian)" ngay sau 4 byte
// length + 4 byte type "IHDR" cua chunk do -> chi can doc dung 24 byte
// dau file la du, khong phu thuoc kich thuoc anh thuc te.
//
// Muc dich: GUIAgentLoop dung ham nay de biet CHINH XAC kich thuoc anh
// chup man hinh vua gui cho model, roi noi vao text observation - de
// model co 1 he quy chieu toa do ro rang khi quyet dinh bam vao dau,
// thay vi doan mu theo % vi tri tren anh.

#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

namespace ImageUtils {

// Tra ve {width, height} (pixel) neu doc thanh cong, nullopt neu file
// khong mo duoc hoac khong phai PNG hop le.
inline std::optional<std::pair<int, int>> readPngDimensions(const std::string& imagePath) {
    std::ifstream file(imagePath, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    unsigned char header[24];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || file.gcount() != static_cast<std::streamsize>(sizeof(header))) {
        return std::nullopt; // file qua ngan, khong the la PNG hop le
    }

    // 8 byte signature chuan cua PNG (RFC 2083 / ISO/IEC 15948)
    static const unsigned char kPngSignature[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'
    };
    if (std::memcmp(header, kPngSignature, sizeof(kPngSignature)) != 0) {
        return std::nullopt; // khong phai file PNG
    }

    // header[8..11]  = length cua chunk dau tien (phai la 13 cho IHDR)
    // header[12..15] = type cua chunk ("IHDR")
    // header[16..19] = width  (uint32, big-endian)
    // header[20..23] = height (uint32, big-endian)
    if (std::memcmp(header + 12, "IHDR", 4) != 0) {
        return std::nullopt; // chunk dau tien khong phai IHDR - file la hong
    }

    auto readBigEndianU32 = [](const unsigned char* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |
               static_cast<uint32_t>(p[3]);
    };

    const uint32_t width  = readBigEndianU32(header + 16);
    const uint32_t height = readBigEndianU32(header + 20);

    if (width == 0 || height == 0) {
        return std::nullopt;
    }

    return std::make_pair(static_cast<int>(width), static_cast<int>(height));
}

}  // namespace ImageUtils