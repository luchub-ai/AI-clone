#include "evaluator.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace {

// Hạ thanh điệu tiếng Việt (UTF-8) về chữ cái Latin không dấu, đồng thời
// hạ chữ ASCII thường về lowercase. Gộp 2 việc vào 1 hàm để tránh gọi
// ::tolower trực tiếp lên từng byte của chuỗi UTF-8 gốc — nếu 1 ký tự
// tiếng Việt chiếm nhiều byte (VD "ỗ" = 3 byte) mà ::tolower nhận từng
// byte >= 0x80 riêng lẻ, hành vi là UNDEFINED BEHAVIOR theo chuẩn C
// (tolower chỉ nhận giá trị biểu diễn được bằng unsigned char hoặc EOF).
// Bản gốc (dùng std::transform + ::tolower thẳng trên toàn chuỗi) mắc
// đúng lỗi này với text tiếng Việt có dấu — chỉ "may mắn" không crash
// vì libc thường chỉ trả lại byte nguyên vẹn cho input ngoài phạm vi,
// nhưng vẫn không phải hành vi được đảm bảo bởi chuẩn.
std::string normalizeVietnamese(const std::string& utf8_text) {
    static const std::unordered_map<std::string, char> diacritic_map = {
        {"à",'a'},{"á",'a'},{"ả",'a'},{"ã",'a'},{"ạ",'a'},
        {"ă",'a'},{"ằ",'a'},{"ắ",'a'},{"ẳ",'a'},{"ẵ",'a'},{"ặ",'a'},
        {"â",'a'},{"ầ",'a'},{"ấ",'a'},{"ẩ",'a'},{"ẫ",'a'},{"ậ",'a'},
        {"è",'e'},{"é",'e'},{"ẻ",'e'},{"ẽ",'e'},{"ẹ",'e'},
        {"ê",'e'},{"ề",'e'},{"ế",'e'},{"ể",'e'},{"ễ",'e'},{"ệ",'e'},
        {"ì",'i'},{"í",'i'},{"ỉ",'i'},{"ĩ",'i'},{"ị",'i'},
        {"ò",'o'},{"ó",'o'},{"ỏ",'o'},{"õ",'o'},{"ọ",'o'},
        {"ô",'o'},{"ồ",'o'},{"ố",'o'},{"ổ",'o'},{"ỗ",'o'},{"ộ",'o'},
        {"ơ",'o'},{"ờ",'o'},{"ớ",'o'},{"ở",'o'},{"ỡ",'o'},{"ợ",'o'},
        {"ù",'u'},{"ú",'u'},{"ủ",'u'},{"ũ",'u'},{"ụ",'u'},
        {"ư",'u'},{"ừ",'u'},{"ứ",'u'},{"ử",'u'},{"ữ",'u'},{"ự",'u'},
        {"ỳ",'y'},{"ý",'y'},{"ỷ",'y'},{"ỹ",'y'},{"ỵ",'y'},
        {"đ",'d'},
        // Bản chữ HOA — cần liệt kê riêng vì đây vẫn là ký tự Unicode
        // nhiều byte khác hẳn bản thường, ::tolower không đụng tới được.
        {"À",'a'},{"Á",'a'},{"Ả",'a'},{"Ã",'a'},{"Ạ",'a'},
        {"Ă",'a'},{"Ằ",'a'},{"Ắ",'a'},{"Ẳ",'a'},{"Ẵ",'a'},{"Ặ",'a'},
        {"Â",'a'},{"Ầ",'a'},{"Ấ",'a'},{"Ẩ",'a'},{"Ẫ",'a'},{"Ậ",'a'},
        {"È",'e'},{"É",'e'},{"Ẻ",'e'},{"Ẽ",'e'},{"Ẹ",'e'},
        {"Ê",'e'},{"Ề",'e'},{"Ế",'e'},{"Ể",'e'},{"Ễ",'e'},{"Ệ",'e'},
        {"Ì",'i'},{"Í",'i'},{"Ỉ",'i'},{"Ĩ",'i'},{"Ị",'i'},
        {"Ò",'o'},{"Ó",'o'},{"Ỏ",'o'},{"Õ",'o'},{"Ọ",'o'},
        {"Ô",'o'},{"Ồ",'o'},{"Ố",'o'},{"Ổ",'o'},{"Ỗ",'o'},{"Ộ",'o'},
        {"Ơ",'o'},{"Ờ",'o'},{"Ớ",'o'},{"Ở",'o'},{"Ỡ",'o'},{"Ợ",'o'},
        {"Ù",'u'},{"Ú",'u'},{"Ủ",'u'},{"Ũ",'u'},{"Ụ",'u'},
        {"Ư",'u'},{"Ừ",'u'},{"Ứ",'u'},{"Ử",'u'},{"Ữ",'u'},{"Ự",'u'},
        {"Ỳ",'y'},{"Ý",'y'},{"Ỷ",'y'},{"Ỹ",'y'},{"Ỵ",'y'},
        {"Đ",'d'},
    };

    std::string result;
    result.reserve(utf8_text.size());
    size_t i = 0;
    while (i < utf8_text.size()) {
        unsigned char c = static_cast<unsigned char>(utf8_text[i]);
        size_t char_len = 1;
        if      ((c & 0xE0) == 0xC0) char_len = 2;   // 2-byte UTF-8
        else if ((c & 0xF0) == 0xE0) char_len = 3;   // 3-byte UTF-8 (hầu hết ký tự Việt)
        else if ((c & 0xF8) == 0xF0) char_len = 4;   // 4-byte UTF-8

        if (i + char_len > utf8_text.size()) char_len = 1;  // phòng chuỗi UTF-8 lỗi/cắt cụt

        std::string ch = utf8_text.substr(i, char_len);
        auto it = diacritic_map.find(ch);
        if (it != diacritic_map.end()) {
            result += it->second;
        } else if (char_len == 1) {
            // ASCII thuần -> tolower an toàn (unsigned char, đúng chuẩn)
            result += static_cast<char>(std::tolower(c));
        } else {
            // Ký tự Unicode khác không phải dấu tiếng Việt (VD dấu câu
            // kiểu Unicode, emoji...) -> giữ nguyên, không hạ được nữa.
            result += ch;
        }
        i += char_len;
    }
    return result;
}

} // namespace

std::vector<std::string> KeywordEvaluator::parseKeywords(const std::string& script) const {
    std::vector<std::string> kws;
    std::istringstream ss(script);
    std::string token;

    while (std::getline(ss, token, ',')) {
        auto l = token.find_first_not_of(" \t\r\n");
        auto r = token.find_last_not_of(" \t\r\n");
        if (l != std::string::npos) {
            kws.push_back(token.substr(l, r - l + 1));
        }
    }
    return kws;
}

bool KeywordEvaluator::findKeyword(const std::string& text, const std::string& kw) const {
    // [SỬA]: trước dùng ::tolower thẳng trên chuỗi UTF-8 -> không so
    // khớp được "lỗi" (có dấu) với "loi" (không dấu) VÌ chúng khác hẳn
    // nhau về byte, không phải vì khác hoa/thường. Giờ chuẩn hoá cả 2 về
    // dạng không dấu + lowercase trước khi so, nên "Lỗi", "lỗi", "loi",
    // "LOI" đều khớp lẫn nhau.
    return normalizeVietnamese(text).find(normalizeVietnamese(kw)) != std::string::npos;
}

std::expected<float, EvalError> KeywordEvaluator::evaluate(const Trajectory& traj,
                                                             const Environment& /*env*/,
                                                             const Task&        task) {
    auto kws = parseKeywords(task.eval_script);
    if (kws.empty()) {
        std::cerr << "[KeywordEval] eval_script khong co keyword nao -> khong the cham diem.\n";
        return std::unexpected(EvalError::NoKeywords);
    }

    std::string corpus;
    for (const auto& step : traj.steps) {
        corpus += step.thought     + ' ';
        corpus += step.tool_result.value_or("") + ' ';
    }

    int found = 0;
    for (const auto& kw : kws) {
        if (findKeyword(corpus, kw)) {
            std::cout << "[KeywordEval] \u2713 \"" << kw << "\"\n";
            ++found;
        } else {
            std::cout << "[KeywordEval] \u2717 \"" << kw << "\"\n";
        }
    }

    float score = static_cast<float>(found) / static_cast<float>(kws.size());
    std::cout << "[KeywordEval] Score: " << score
              << "  (" << found << "/" << kws.size() << " keywords)\n";
    return score;
}