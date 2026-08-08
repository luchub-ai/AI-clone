#include "exec_tool.h"
#include <nlohmann/json.hpp>
#include "src/common/json_utils.h"
using json = nlohmann::json;

ExecTool::ExecTool(Environment* env, int timeoutSeconds, bool allowShellMeta)
    : env_(env), timeout_seconds_(timeoutSeconds), allow_shell_meta_(allowShellMeta) {}

std::string ExecTool::getName() const { return "exec"; }

std::string ExecTool::getDescription() const {
    return "Cong cu thuc thi lenh shell va tra ve ket qua (stdout/stderr gop chung). "
           "Chi dung cho lenh non-interactive. Lenh se bi huy sau " +
           std::to_string(timeout_seconds_) +
           " giay neu chay qua lau. "
           "Tham so dau vao (args) phai la JSON hop le: {\"command\": \"lenh shell can chay\"}";
}

bool ExecTool::isCommandSafe(const std::string& cmd) const {
    if (allow_shell_meta_) return true;
    static const std::vector<std::string> dangerous = {
        "rm -rf /", "rm -rf /*", ":(){ :|:& };:", "mkfs",
        "dd if=", "> /dev/sda", "shutdown", "reboot",
        " nano ", " vim ", " vi ", "/bin/nano", "/usr/bin/vim", "nano ", "vim ",
    };
    for (const auto& pattern : dangerous)
        if (cmd.find(pattern) != std::string::npos) return false;
    return true;
}

std::optional<std::string> ExecTool::execute(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } 
    // C++26: Dùng '_' thay vì 'e' để khai báo biến vô danh
    catch (const json::parse_error& _) { 
        // Ta không cần in chi tiết lỗi của nlohmann, chỉ cần báo cho LLM
        return "Lỗi: parse json đầu vào";
    }
    // C++20: requireField<T> dung concept JsonScalar de rang buoc T ngay
    // luc bien dich, thay the "j.contains(...) && j[...].is_string()" lap
    // lai o moi Tool (xem json_utils.h).
    auto command_opt = requireField<std::string>(j, "command");
    if (!command_opt)
        return std::string("Loi: JSON args thieu truong 'command' kieu string.");

    std::string command = *command_opt;
    if (command.empty()) return std::string("Loi: 'command' rong.");
    if (!isCommandSafe(command)) return std::string("Loi: lenh bi chan boi ExecTool safety policy.");

    auto output = env_->execute(command, timeout_seconds_);
    if (!output.has_value())
        return std::string("Loi: khong the thuc thi lenh (Environment tra ve loi ha tang).");
    if (output->empty())
        return std::string("Lenh da thuc thi thanh cong (khong co output tra ve).");
    return output;
}