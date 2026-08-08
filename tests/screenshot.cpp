#include "src/tools/screenshot_tool.h"
#include <iostream>
#include <filesystem>

int main() {
    // Giả lập Environment::getWorkspace() bằng 1 thư mục cố định cho lần test này.
    // Khi tích hợp thật, thay bằng: [env_ptr]() { return env_ptr->getWorkspace(); }
    std::filesystem::path workspace = "./workspace";

    ScreenshotTool tool(
        [workspace]() { return workspace; }
    );

    std::cout << "Tool name: " << tool.getName() << "\n";
    std::cout << "Tool description: " << tool.getDescription() << "\n";
    std::cout << "Dang chup man hinh (dung 1 lan)...\n";

    // execute() luôn trả về std::optional<std::string>:
    // - có giá trị  -> đúng 1 chuỗi duy nhất là đường dẫn file
    // - std::nullopt -> thất bại, không có chuỗi nào cả
    // Không bao giờ trả về nhiều chuỗi hay chuỗi rỗng hợp lệ.
    std::optional<std::string> result = tool.execute("");

    if (!result.has_value()) {
        std::cerr << "That bai: khong chup duoc man hinh.\n";
        std::cerr << "Kiem tra: xdg-desktop-portal co dang chay khong? "
                     "User co bam Allow tren dialog xin quyen khong? "
                     "Co bi timeout 60s khong?\n";
        return 1;
    }

    const std::string& path_str = result.value();

    // Ép buộc đúng 1 chuỗi hợp lệ: không rỗng, không chứa xuống dòng
    if (path_str.empty()) {
        std::cerr << "LOI: tool tra ve chuoi rong.\n";
        return 1;
    }
    if (path_str.find('\n') != std::string::npos) {
        std::cerr << "LOI: chuoi tra ve chua ky tu xuong dong, khong hop le.\n";
        return 1;
    }

    std::filesystem::path saved_path(path_str);
    if (!std::filesystem::exists(saved_path)) {
        std::cerr << "LOI: tool tra ve path nhung file khong ton tai: "
                   << path_str << "\n";
        return 1;
    }

    auto size = std::filesystem::file_size(saved_path);
    if (size == 0) {
        std::cerr << "CANH BAO: file rong, co the chup that bai im lang.\n";
        return 1;
    }

    std::cout << "Thanh cong! Chuoi ket qua duy nhat: " << path_str << "\n";
    std::cout << "Kich thuoc file: " << size << " bytes\n";

    return 0;
}