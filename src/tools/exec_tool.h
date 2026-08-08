#pragma once

#include "src/tools/tool.h"

#include <optional>
#include "src/harness/environment/environment.h"
#include <string>
#include <vector>
#include <functional>

// ExecTool: chay mot lenh shell va tra ve stdout/stderr gop lai.
// Args dau vao la JSON: {"command": "lenh shell can chay"}
// Day la 1 trong 5 tool bat buoc (muc 3.2 de bai).
class ExecTool : public Tool {
public:
    // timeoutSeconds: gioi han thoi gian chay lenh, ep buoc bang lenh
    //   `timeout` cua Linux -> tranh AgentLoop bi block vo han o mot lenh
    //   cho input hoac chay qua lau (vi du: sleep 9999, read).
    // allowShellMeta: neu false, ExecTool se chan cac pattern nguy hiem ro
    //   rang trong isCommandSafe(). Day la lop bao ve toi thieu, KHONG phai
    //   sandbox that su - xem ghi chu trong exec_tool.cpp.
    explicit ExecTool(Environment* env, int timeoutSeconds = 10 , bool allowShellMeta = false);  // workspace = dir, timeout = 10, allowshellmeta = false

    std::string getName() const override;
    std::string getDescription() const override;

    // args phai la JSON hop le: {"command": "..."}
    std::optional<std::string> execute(const std::string& args) override;

private:
    int timeout_seconds_;
    bool allow_shell_meta_;
    Environment* env_ ; // biên môi trường

    // Kiem tra so bo lenh co nam trong danh sach pattern bi chan hay khong.
    bool isCommandSafe(const std::string& cmd) const;

    // Chay lenh qua popen(), doc toan bo stdout+stderr (da duoc redirect
    // "2>&1"), tra ve nullopt neu khong mo duoc pipe.
    std::optional<std::string> runCommand(const std::string& cmd,
                                            int timeoutSeconds);
};