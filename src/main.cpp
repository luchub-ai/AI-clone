#include <iostream>
#include <string>
#include <optional>
#include <cstdlib>
#include "src/tools/input_tool.h"

// 1. Hàm giả lập CommandRunner
// Hàm này sẽ in ra lệnh ydotool thay vì chạy thật để an toàn khi test.
std::optional<std::string> mockCommandRunner(const std::string& cmd) {
    std::cout << "  [CommandRunner Thực thi] -> " << cmd << "\n";
    
    // NẾU BẠN MUỐN CHẠY LỆNH THẬT TRÊN HỆ ĐIỀU HÀNH, HÃY BỎ COMMENT DÒNG DƯỚI ĐÂY:
    int result = std::system(cmd.c_str());
    // if (result != 0) return std::nullopt;
    
    return "success";
}

// 2. Hàm chạy các Automated Tests tự động
void runAutomatedTests(InputTool& tool) {
    std::cout << "\n==================================================\n";
    std::cout << "          CHẠY AUTOMATED TESTS TỰ ĐỘNG            \n";
    std::cout << "==================================================\n";

    std::cout << "\n[Test 1] Move chuot (Toa do nguyen ban: x=100, y=200. He so scale: 1.5x)\n";
    tool.execute(R"({"action":"move", "x":100, "y":200})");

    std::cout << "\n[Test 2] Click chuot phai (Khong kem toa do)\n";
    tool.execute(R"({"action":"click", "button":"right"})");

    std::cout << "\n[Test 3] Double click chuot trai (Kem di chuyen den 50,50)\n";
    tool.execute(R"({"action":"double_click", "x":50, "y":50, "button":"left"})");

    std::cout << "\n[Test 4] Go chu (Type) kem ky tu dac biet (Dau nhay don)\n";
    tool.execute(R"({"action":"type", "text":"echo 'Hello World'"})");

    std::cout << "\n[Test 5] To hop phim (Key combo: ctrl+alt+t)\n";
    tool.execute(R"({"action":"key", "keys":"ctrl+alt+t"})");

    std::cout << "\n[Test 6] Cuon chuot (Scroll xuong dy=-10, dx=5)\n";
    tool.execute(R"({"action":"scroll", "dy":-10, "dx":5})");
    
    std::cout << "\n==================================================\n";
    std::cout << "             HOÀN TẤT AUTOMATED TESTS             \n";
    std::cout << "==================================================\n";
}

// 3. Hàm Menu tương tác
void runInteractiveMenu(InputTool& tool) {
    int choice = -1;
    while (choice != 0) {
        std::cout << "\n================ MENU TEST GUI INPUT ================\n";
        std::cout << "1. Test Move (Di chuyen chuot)\n";
        std::cout << "2. Test Click\n";
        std::cout << "3. Test Double Click\n";
        std::cout << "4. Test Type (Go van ban)\n";
        std::cout << "5. Test Key (Go to hop phim)\n";
        std::cout << "6. Test Scroll (Cuon chuot)\n";
        std::cout << "0. Thoat\n";
        std::cout << "Chon chuc nang: ";
        
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            continue;
        }
        std::cin.ignore(10000, '\n'); // Xoa buffer

        std::string json_payload = "";

        switch (choice) {
            case 1: {
                int x, y;
                std::cout << "Nhap toa do X: "; std::cin >> x;
                std::cout << "Nhap toa do Y: "; std::cin >> y;
                json_payload = "{\"action\":\"move\", \"x\":" + std::to_string(x) + ", \"y\":" + std::to_string(y) + "}";
                break;
            }
            case 2: {
                std::string btn;
                std::cout << "Nhap nut click (left/right/middle): "; 
                std::getline(std::cin, btn);
                if(btn.empty()) btn = "left";
                json_payload = "{\"action\":\"click\", \"button\":\"" + btn + "\"}";
                break;
            }
            case 3: {
                std::string btn;
                std::cout << "Nhap nut double click (left/right/middle): "; 
                std::getline(std::cin, btn);
                if(btn.empty()) btn = "left";
                json_payload = "{\"action\":\"double_click\", \"button\":\"" + btn + "\"}";
                break;
            }
            case 4: {
                std::string text;
                std::cout << "Nhap doan text can go: ";
                std::getline(std::cin, text);
                json_payload = "{\"action\":\"type\", \"text\":\"" + text + "\"}";
                break;
            }
            case 5: {
                std::string keys;
                std::cout << "Nhap to hop phim (vd: ctrl+c, enter): ";
                std::getline(std::cin, keys);
                json_payload = "{\"action\":\"key\", \"keys\":\"" + keys + "\"}";
                break;
            }
            case 6: {
                int dy;
                std::cout << "Nhap muc do cuon Y (dy): "; std::cin >> dy;
                json_payload = "{\"action\":\"scroll\", \"dy\":" + std::to_string(dy) + "}";
                break;
            }
            case 0:
                std::cout << "Dang thoat...\n";
                break;
            default:
                std::cout << "Lua chon khong hop le!\n";
                break;
        }

        if (choice >= 1 && choice <= 6 && !json_payload.empty()) {
            std::cout << "\n>>> Gui JSON: " << json_payload << "\n";
            auto result = tool.execute(json_payload);
            if (!result) {
                std::cout << "[LỖI] InputTool tra ve nullopt (Thuc thi that bai hoac sai cu phap JSON)\n";
            }
        }
    }
}

int main() {
    // do vấn đề wayland (máy ubuntu, nên khi ánh xạ 1920/1080 nó tự về 768/432 -> scale = 0.4)
    InputTool gui_tool(mockCommandRunner,0.4, 0.4);

    // 1. Chay test co dinh truoc
    // runAutomatedTests(gui_tool);

    // 2. Mo menu cho nguoi dung tuoc tac
    runInteractiveMenu(gui_tool);

    return 0;
}