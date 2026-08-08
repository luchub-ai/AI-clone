#pragma once

#include "src/tools/tool.h"

#include <functional>
#include <optional>
#include <string>

// CodeInterpreterTool: ghi code (mac dinh Python) ra 1 file tam trong
// workspace, chay bang mot interpreter co dinh trong subprocess co gioi
// han thoi gian + bo nho, tra ve stdout/stderr gop lai.
//
// Khac voi ExecTool o cho: ExecTool chay BAT KY dong lenh shell nao LLM
// sinh ra (pham vi ca he dieu hanh). Tool nay chi chay MOT runtime co
// dinh, va code duoc truyen vao nhu DATA - ghi ra file that - chu khong
// noi vao command string nhu ExecTool lam voi `cmd`. Day la 1 trong 3
// tool "tu them" (muc 3.2 de bai, tham khao OpenClaw/Hermes).
class CodeInterpreterTool : public Tool {
public:
    // pathProvider: giong het ExecTool/FileTool - lay workspace hien tai
    //   tu Environment (NativeEnvironment hoac SandboxEnvironment) qua
    //   lambda kieu [env_ptr]() { return env_ptr->getWorkspace(); }.
    // timeoutSeconds: wall-clock timeout, ep bang lenh `timeout` cua
    //   Linux (giong ExecTool) - tranh vong lap vo han lam AgentLoop bi
    //   block. Gia tri <= 0 se tu dong duoc ep ve 1 (GNU `timeout` coi 0
    //   la "tat han timeout", nguoc voi muc dich cua tham so nay).
    // memoryLimitKb: gioi han virtual memory (KB) cho subprocess qua
    //   `ulimit -v`. Gia tri <= 0 = khong gioi han (co chu y). Best-effort
    //   tren Linux, bo qua tren Windows (khong co khai niem ulimit).
    // interpreter: ten binary interpreter se goi, mac dinh "python3".
    // fileExtension: duoi file tam se ghi code vao, mac dinh ".py". Tach
    //   rieng khoi `interpreter` (thay vi hard-code ".py") vi 2 thu nay
    //   doc lap nhau - vd interpreter="python3.12" van dung duoi ".py".
    explicit CodeInterpreterTool(std::function<std::string()> pathProvider,
                                  int timeoutSeconds = 10,
                                  long memoryLimitKb = 512 * 1024,
                                  std::string interpreter = "python3",
                                  std::string fileExtension = ".py");

    std::string getName() const override;
    std::string getDescription() const override;

    // args phai la JSON hop le: {"code": "code can chay"}
    std::optional<std::string> execute(const std::string& args) override;

private:
    std::function<std::string()> path_provider_;
    int timeout_seconds_;
    long memory_limit_kb_;
    std::string interpreter_;
    std::string file_extension_;

    // Ghi `code` ra 1 file tam (ten random) trong `workspace`, tra ve
    // duong dan file vua tao. Nem std::runtime_error neu khong tao/ghi
    // duoc file (vd workspace khong co quyen ghi).
    std::string writeCodeToTempFile(const std::string& workspace,
                                     const std::string& code) const;

    // Chay interpreter_ tren file da ghi, gioi han timeout + memory (Linux),
    // tra ve stdout+stderr gop lai (2>&1). nullopt neu popen() that bai
    // (khong mo duoc pipe) - phan biet voi "chay xong nhung khong co output".
    std::optional<std::string> runInterpreter(const std::string& filePath,
                                               const std::string& workspace) const;
};
