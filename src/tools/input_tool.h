#pragma once
#include "tool.h"  // TODO: doi ten include cho khop voi header Tool that su trong repo cua ban

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

// CommandRunner: 1 ham chay lenh shell va tra ve stdout khi thanh cong,
// nullopt khi that bai/timeout. Day la "adapter" mong de InputTool KHONG
// phu thuoc truc tiep vao chu ky ham Environment::execute(...) that su
// (vi minh chua biet chinh xac signature/struct tra ve cua Environment.h).
//
// Cach wiring o noi khoi tao ToolRegistry (vi du):
//
//   auto env = std::make_shared<NativeEnvironment>(...);
//   CommandRunner runner = [env](const std::string& cmd) -> std::optional<std::string> {
//       auto result = env->execute(cmd, /*timeoutSeconds=*/5);
//       if (result.exitCode != 0) return std::nullopt;   // doi ten field neu can
//       return result.stdout_;                            // doi ten field neu can
//   };
//   registry.registerTool(std::make_unique<InputTool>(runner));
//
using CommandRunner = std::function<std::optional<std::string>(const std::string&)>;

// InputTool: dieu khien chuot/ban phim tren desktop that (chi chay duoc
// khi Environment la NativeEnvironment - xem GUI_AGENT_SETUP.md).
// Backend: ydotool (uinput), khong qua xdg-desktop-portal nen KHONG co
// hop thoai xin quyen o bat ky lan goi nao, phu hop de AgentLoop tu dong
// goi lien tuc.
//
// Args (JSON), field "action" la bat buoc:
//   {"action": "move",         "x": <int>, "y": <int>}
//   {"action": "click",        "x": <int>?, "y": <int>?, "button": "left|right|middle"?}
//   {"action": "double_click", "x": <int>?, "y": <int>?, "button": "left|right|middle"?}
//   {"action": "type",         "text": <string>}
//   {"action": "key",          "keys": <string>}   // vi du: "enter", "ctrl+c", "ctrl+alt+t"
//   {"action": "scroll",       "dy": <int>, "dx": <int>?}
//
// (x, y trong click/double_click la optional: neu co se di chuyen chuot
//  toi truoc khi click; neu khong se click tai vi tri con tro hien tai)
class InputTool : public Tool {
public:
    using ScaleProvider = std::function<double()>;

    // scale_x/scale_y: he so nhan them vao truoc khi goi ydotool, dung de
    // BU TRU sai lech giua toa do model dua ra (theo dung kich thuoc anh
    // chup man hinh, xem GUIAgentLoop::observe()) va toa do THUC TE ma
    // `ydotool mousemove --absolute` nhan dien tren may nay. Day KHONG
    // phai loi ly thuyet - la bug co that cua ydotool (vd co may bi lech
    // dung 1/2 do phan giai). Truyen dang HAM (khong phai so co dinh) vi
    // gia tri nay co the doi giua cac lan chup neu ScreenshotTool dang
    // resize anh (xem ScreenshotTool::getLastResizeRatio) - ham duoc goi
    // lai o MOI lan click/move, khong chi luc khoi tao InputTool (luc do
    // chua co screenshot nao de biet ti le). Mac dinh: luon tra ve 1.0.
    explicit InputTool(CommandRunner command_runner,
                        ScaleProvider scale_x = []{ return 1.0; },
                        ScaleProvider scale_y = []{ return 1.0; });

    [[nodiscard]] std::string getName() const override;
    [[nodiscard]] std::string getDescription() const override;

    std::optional<std::string> execute(const std::string& args) override;

private:
    CommandRunner command_runner_;
    ScaleProvider scale_x_;
    ScaleProvider scale_y_;

    // Thoi diem InputTool thuc thi gan nhat - dung de tinh khoang cach
    // toi lan goi tiep theo (xem execute() trong .cpp). nullopt o lan
    // goi dau tien (chua co gi de so sanh, khong can cho).
    std::optional<std::chrono::steady_clock::time_point> last_action_time_;

    std::optional<std::string> handleMove(const nlohmann::json& j);
    std::optional<std::string> handleClick(const nlohmann::json& j, bool double_click);
    std::optional<std::string> handleType(const nlohmann::json& j);
    std::optional<std::string> handleKey(const nlohmann::json& j);
    std::optional<std::string> handleScroll(const nlohmann::json& j);

    // Bao mat: bao chuoi text/keys thanh single-quoted shell-safe truoc khi
    // noi vao command string (vi Environment::execute cuoi cung chay qua shell).
    static std::string shellEscape(const std::string& raw);

    // Tra ve keycode (Linux input-event-codes.h) cho 1 ten phim (khong phan
    // biet hoa/thuong). Chi ho tro subset pho bien cho ban phim US QWERTY -
    // du dung cho GUI agent (mo app, go text, dieu huong, Enter/Tab/Esc...).
    static std::optional<int> keyNameToCode(const std::string& name);

    // Chuyen "ctrl+alt+t" -> chuoi tham so cho `ydotool key`, vi du:
    // "29:1 56:1 20:1 20:0 56:1 29:0" (nhan tat ca theo thu tu, tha nguoc lai).
    static std::optional<std::string> buildKeySequence(const std::string& combo);

    // Mouse button name -> ma hex cho `ydotool click` (da gop san down+up).
    static std::optional<std::string> buttonToClickCode(const std::string& button);
};