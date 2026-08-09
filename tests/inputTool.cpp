#include "src/tools/screenshot_tool.h"
#include "src/tools/input_tool.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

// In ket qua execute() ra terminal cho de nhin khi test tay.
void report(const std::string& label, const std::optional<std::string>& result) {
    if (result) {
        std::cout << "[OK]   " << label << "\n";
    } else {
        std::cout << "[FAIL] " << label << "\n";
    }
}

int main() {
    // CommandRunner tam thoi cho file test doc lap nay: chi can std::system,
    // KHONG can dung ca NativeEnvironment (Environment con doi hoi setup()
    // workspace, resolveSafePath(), v.v. - thua cho muc dich test 1 tool).
    // Khi wiring that vao AI_Agent, thay the_runner nay bang lambda goi
    // environment->execute(cmd, 5) nhu trong GUI_AGENT_SETUP.md muc 4.
    CommandRunner test_runner = [](const std::string& cmd) -> std::optional<std::string> {
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "  (command that bai, exit code " << ret << "): " << cmd << "\n";
            return std::nullopt;
        }
        return std::string(); // khong can noi dung stdout cho test nay
    };

    InputTool input(test_runner);  // <-- fix chinh: co dau () bao quanh
                                    //     tham so, KHONG phai "input()"

    std::cout << "Bat dau test InputTool - quan sat con tro chuot/man hinh...\n\n";

    // Di chuyen + click chuot trai tai (500, 300)
    report("move+click (500,300)", input.execute(R"({"action":"click","x":500,"y":300})"));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Go chu (se go vao bat ky o nao dang focus - mo san 1 text editor/
    // terminal truoc khi chay de thay ro ket qua)
    report("type 'hello world'", input.execute(R"({"action":"type","text":"hello world"})"));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Ctrl+S
    report("key ctrl+s", input.execute(R"({"action":"key","keys":"ctrl+s"})"));

    std::cout << "\nXong. Neu thay [FAIL] o dong nao, kiem tra:\n"
                 "  - ydotoold co dang chay khong: systemctl --user status ydotoold\n"
                 "  - YDOTOOL_SOCKET da duoc set trong terminal nay chua: echo $YDOTOOL_SOCKET\n";

    return 0;
}