#include "skill_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <unordered_map>
#include <algorithm>
#include <cstddef>

namespace fs = std::filesystem;

namespace {
// ════════════════════════════════════════════════════════════════
//  C++26 #embed (P1967): nhúng THẲNG các byte của skills/calculator.md
//  vào binary lúc BIÊN DỊCH — không đọc từ đĩa lúc chạy.
//
//  ĐỘNG LỰC: loadSkillsFromDisk() bên dưới dùng đường dẫn TƯƠNG ĐỐI
//  "skills/" (xem SkillLoader::SkillLoader mặc định trong skill_loader.h)
//  — nếu binary chạy từ working directory khác thư mục gốc repo (rất dễ
//  xảy ra, ví dụ chạy từ build/ hoặc từ IDE), fs::exists(skill_dir) trả
//  false và TOÀN BỘ skill biến mất, agent mất luôn hướng dẫn dùng
//  calculator dù binary chứa đủ code để chạy calculator tool. Trước đây
//  nhánh này chỉ log cảnh báo rồi thôi, không có gì dự phòng.
//
//  Đường dẫn tương đối "../../skills/calculator.md" ở đây được #embed
//  phân giải giống #include "..." -> LUÔN tính từ vị trí file
//  src/agent/skill_loader.cpp trên đĩa nguồn, KHÔNG phụ thuộc working
//  directory lúc chạy hay thư mục build của CMake.
constexpr unsigned char kFallbackCalculatorSkillBytes[] = {
#embed "../../skills/calculator.md"
    , 0   // dam bao co '\0' ket thuc de dung nhu C-string ben duoi
};
} // namespace

// Hàm nhận prompt thô và trả về danh sách từ khóa skill chuẩn
    static std::vector<std::string> extract(const std::string& raw_prompt) {
        // 1. Chuyển prompt sang chữ thường để dễ xử lý
        std::string lower_prompt = raw_prompt;
        std::transform(lower_prompt.begin(), lower_prompt.end(), lower_prompt.begin(), ::tolower);

        std::vector<std::string> matched_skills;

        // 2. Từ điển Mapping: { "dấu hiệu nhận biết", "tên skill chuẩn" }
        // Bạn có thể mở rộng danh sách này tùy theo các skill file bạn viết
        std::unordered_map<std::string, std::string> keyword_map = {
            // Nhóm skill: Tính toán (calculator.md)
            {"tính", "calculator"},
            {"tĩnh", "calculator"}, // Bắt luôn lỗi chính tả thường gặp
            {"toán", "calculator"},
            {"+", "calculator"},
            {"-", "calculator"},
            {"*", "calculator"},
            {"/", "calculator"},
            {"cộng", "calculator"},

            // Nhóm skill: Chạy code (code_interpreter.md)
            {"viết code", "code_interpreter"},
            {"viết chương trình", "code_interpreter"},
            {"vòng lặp", "code_interpreter"},
            {"thuật toán", "code_interpreter"},
            {"đoạn code", "code_interpreter"},

            // Nhóm skill: Lên kế hoạch (task_planner.md)
            {"kế hoạch", "task_planner"},
            {"plan", "task_planner"},
            {"bước", "task_planner"},
            {"step", "task_planner"},
            {"phức tạp", "task_planner"},

            // Nhóm skill: Xử lý lỗi (error_recovery.md)
            {"lỗi", "error_recovery"},
            {"error", "error_recovery"},
            {"fail", "error_recovery"},

            {"memory","memory"},
            {"bộ nhớ","memory"},
            {"kí ức","memory"},
            {"kiếm lại","memory"},

            // nhóm skill: mở app bằng exec tool (open_file.md)
            {"execTool","open_app"},
            {"mở app","open_app"},
            {"open file","open_app"},
            {"trình duyệt","open_app"},
            {"google","open_app"},
            {"chrome","open_app"},

            // nhóm skill: mở app bằng exec tool (open_file.md)
            {"gủi mail","send_mail"},
            {"soạn mail","send_mail"},
            {"gửi thư","send_mail"},
            {"soạn thư","send_mail"},
            
            
        };

        // 3. Quét prompt xem có chứa dấu hiệu nào không
        for (const auto& [trigger_word, target_skill] : keyword_map) {
            if (lower_prompt.find(trigger_word) != std::string::npos) {
                // Nếu tìm thấy, kiểm tra xem skill này đã được thêm vào mảng chưa (tránh trùng lặp)
                if (std::find(matched_skills.begin(), matched_skills.end(), target_skill) == matched_skills.end()) {
                    matched_skills.push_back(target_skill);
                }
            }
        }

        return matched_skills;
    }

SkillLoader::SkillLoader(fs::path dir) : skill_dir(std::move(dir)) {}

void SkillLoader::loadSkillsFromDisk() {
    skills.clear();
    if (!fs::exists(skill_dir) || !fs::is_directory(skill_dir)) {
        std::cerr << "[SkillLoader] Canh bao: Thu muc skill khong ton tai -> " << skill_dir
                  << ". Dung ban 'calculator' du phong duoc nhung san luc bien dich "
                     "(C++26 #embed) thay vi mat trang toan bo skill.\n";
        skills["calculator"] = std::string(
            reinterpret_cast<const char*>(kFallbackCalculatorSkillBytes),
            sizeof(kFallbackCalculatorSkillBytes) - 1);   // -1: bo byte '\0' vua them ben tren
        return;
    }

    // Kỹ thuật C++17: Duyệt file trong thư mục
    for (const auto& entry : fs::directory_iterator(skill_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".md") {
            std::ifstream ifs(entry.path());
            if (ifs) {
                std::ostringstream oss;
                oss << ifs.rdbuf();
                std::string stem_name = entry.path().stem().string(); // Cắt đuôi .md
                skills[stem_name] = oss.str();
            }
        }
    }
}

std::string SkillLoader::selectSkill(const std::string& keyword) const {
    const std::vector<std::string>& keywords = extract(keyword); // keyword != keywords
    if (keywords.empty()) return "";

    std::string accumulated_skills = ""; // Biến cộng dồn các kĩ năng tìm được
    
    // Duyệt qua từng từ khóa (VD: "calculator", "plan")
    for (const auto& kw : keywords) {
        if (kw.empty()) continue;
        
        std::regex word_regex("\\b" + kw + "\\b", std::regex_constants::icase);

        for (const auto& [name, content] : skills) {
            // Kiểm tra xem skill này đã được nạp vào accumulated_skills chưa 
            // (tránh trùng lặp nếu 2 từ khóa cùng trỏ về 1 file)
            if (accumulated_skills.find("[SPECIAL SKILL INSTRUCTION: " + name + "]") != std::string::npos) {
                continue; 
            }

            // Quét tên file và nội dung
            if (std::regex_search(name, word_regex) || std::regex_search(content, word_regex)) {
                accumulated_skills += "\n=== [SPECIAL SKILL INSTRUCTION: " + name + "] ===\n" 
                                    + content 
                                    + "\n===================================\n";
            }
        }
    }

    return accumulated_skills; // Trả về 1 chuỗi dài chứa nhiều kĩ năng
}