#include "file_tool.h"
#include <nlohmann/json.hpp>
#include <span>
#include "src/common/json_utils.h"
using json = nlohmann::json;

namespace {
// C++20: std::span<const char> boc mot vung nho KHONG so huu tren du lieu
// cua std::string content - mang theo ca con tro LAN kich thuoc trong 1
// gia tri, thay vi truyen rieng le data()/size() nhu truoc (de sai neu 2
// tham so bi lech nhau khi sua code sau nay).
//
// TRUOC: content->size() > max_content_bytes_ la BO LUON toan bo, tra ve
// mot dong loi trong khong co gi huu ich cho agent doc tiep.
// GIO: cat mot "preview" gioi han byte tu dung phan DAU cua content bang
// span::first(), agent van co it nhat mot phan noi dung de lam viec tiep
// thay vi mat trang hoan toan.
std::string makePreview(std::span<const char> content, std::size_t limit) {
    auto bounded = content.first(std::min(limit, content.size()));
    return std::string(bounded.begin(), bounded.end());
}
} // namespace

FileTool::FileTool(Environment* env, std::size_t maxContentBytes)
    : env_(env), max_content_bytes_(maxContentBytes) {}

std::string FileTool::getName() const { return "file"; }

std::string FileTool::getDescription() const {
    return "Cong cu doc/ghi/liet ke/xoa file va thu muc trong workspace cua agent. "
           "args JSON: {\"action\": \"read|write|append|mkdir|list|delete\", "
           "\"path\": \"...\", \"content\": \"...\" (chi voi write/append)}. "
           "Duong dan luon tuong doi so voi workspace root, khong the thoat ra ngoai.";
}

std::optional<std::string> FileTool::execute(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } 
    // C++26: Dùng '_' thay vì 'e' để khai báo biến vô danh
    catch (const json::parse_error& _) { 
        // Ta không cần in chi tiết lỗi của nlohmann, chỉ cần báo cho LLM
        return "Loi parse JSON dau vao: ";
    }
    // C++20: requireField<T> (concept JsonScalar) thay "j.contains(...) &&
    // j[...].is_string()" lap lai — xem json_utils.h.
    auto action_opt = requireField<std::string>(j, "action");
    if (!action_opt)
        return std::string("Loi: JSON args thieu truong 'action' kieu string.");

    std::string action = *action_opt;
    std::string path = j.value("path", "");

    if (action == "read") {
        auto content = env_->readFile(path);
        if (!content) return std::string("Loi: khong doc duoc file.");
        if (content->size() > max_content_bytes_) {
            // C++20: std::span cat preview thay vi bo het noi dung — xem
            // ghi chu makePreview() o dau file.
            std::span<const char> view(content->data(), content->size());
            std::string preview = makePreview(view, max_content_bytes_);
            return std::string("Canh bao: file vuot qua gioi han ") +
                   std::to_string(max_content_bytes_) + " bytes (thuc te " +
                   std::to_string(content->size()) + " bytes). Hien thi " +
                   std::to_string(preview.size()) + " byte dau tien:\n" + preview;
        }
        return content;
    }
    if (action == "write" || action == "append") {
        auto content_opt = requireField<std::string>(j, "content");
        if (!content_opt)
            return std::string("Loi: thieu truong 'content' kieu string.");
        std::string content = *content_opt;
        if (content.size() > max_content_bytes_)
            return std::string("Loi: content vuot qua gioi han ") + std::to_string(max_content_bytes_) + " bytes.";
        auto result = env_->writeFile(path, content, action == "append");
        if (!result) return std::string("Loi: khong ghi duoc file (path khong hop le).");
        return std::string("Da ghi file thanh cong: ") + path;
    }
    if (action == "mkdir") {
        auto result = env_->makeDir(path);
        if (!result) return std::string("Loi: khong tao duoc thu muc.");
        return std::string("Da tao thu muc: ") + path;
    }
    if (action == "list") {
        auto result = env_->listDir(path);
        if (!result) return std::string("Loi: khong liet ke duoc.");
        return result->empty() ? std::string("(thu muc rong)") : *result;
    }
    if (action == "delete") {
        auto result = env_->deleteEntry(path);
        if (!result) return std::string("Loi: khong xoa duoc.");
        return result;
    }
    return std::string("Loi: 'action' khong hop le. Chi chap nhan read/write/append/mkdir/list/delete.");
}