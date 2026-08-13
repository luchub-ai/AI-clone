#include "input_tool.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

InputTool::InputTool(CommandRunner command_runner, double scale_x, double scale_y)
    : command_runner_(std::move(command_runner)),
      scale_x_(scale_x),
      scale_y_(scale_y) {
}

std::string InputTool::getName() const {
    return "gui_input";
}

std::string InputTool::getDescription() const {
    return
        "Dieu khien chuot/ban phim tren man hinh desktop that (qua ydotool). "
        "Khong hien hop thoai xin quyen. Args la JSON, field 'action' bat buoc:\n"
        "  {\"action\":\"move\",\"x\":<int>,\"y\":<int>}\n"
        "  {\"action\":\"click\",\"x\":<int>?,\"y\":<int>?,\"button\":\"left|right|middle\"?}\n"
        "  {\"action\":\"double_click\",\"x\":<int>?,\"y\":<int>?,\"button\":\"left|right|middle\"?}\n"
        "  {\"action\":\"type\",\"text\":<string>}\n"
        "  {\"action\":\"key\",\"keys\":<string>}  (vi du \"enter\", \"ctrl+c\", \"ctrl+alt+t\")\n"
        "  {\"action\":\"scroll\",\"dy\":<int>,\"dx\":<int>?}\n"
        "x,y trong click/double_click la optional: co thi di chuyen chuot toi truoc khi click.";
}

std::optional<std::string> InputTool::execute(const std::string& args) {
    // Thay vi luon sleep_for(2s) co dinh, dung std::chrono::steady_clock
    // de CHI cho phan thoi gian con THIEU ke tu lan InputTool thuc thi
    // gan nhat. Neu 2s da troi qua tu nhien giua 2 lan goi (vd LLM mat
    // thoi gian suy luan/goi API o think()), khong can cho them nua -
    // tranh lam agent cham vo ich khi khong thuc su can.
    constexpr auto kMinGap = std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();
    if (last_action_time_) {
        const auto elapsed = now - *last_action_time_;
        if (elapsed < kMinGap) {
            std::this_thread::sleep_for(kMinGap - elapsed);
        }
    }
    last_action_time_ = std::chrono::steady_clock::now();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(args);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    if (!j.contains("action") || !j["action"].is_string()) {
        return std::nullopt;
    }
    const std::string action = j["action"].get<std::string>();

    std::optional<std::string> raw_result;
    if (action == "move") {
        raw_result = handleMove(j);
    } else if (action == "click") {
        raw_result = handleClick(j, /*double_click=*/false);
    } else if (action == "double_click") {
        raw_result = handleClick(j, /*double_click=*/true);
    } else if (action == "type") {
        raw_result = handleType(j);
    } else if (action == "key") {
        raw_result = handleKey(j);
    } else if (action == "scroll") {
        raw_result = handleScroll(j);
    } else {
        return std::nullopt; // action khong duoc ho tro
    }

    if (!raw_result) {
        return std::nullopt; // lenh that su that bai (vd ydotoold khong chay, JSON thieu field)
    }

    // QUAN TRONG: ydotool/uinput KHONG BAO GIO bao loi "click sai vi tri"
    // hay "khong trung dich" - no chi bao loi khi khong goi duoc lenh he
    // thong. "Thanh cong" o day CHI co nghia la DA GUI duoc su kien input
    // toi kernel, KHONG co nghia la man hinh da thay doi dung y muon.
    // Tra ve 1 cau text RO RANG thay vi chuoi rong - chuoi rong de bi
    // model doc thanh "im lang = thanh cong hoan toan" (nguyen nhan
    // hallucination da xac dinh duoc qua phan tich trajectory that te).
    return "[Da gui lenh input: " + action + "] Luu y: ket qua nay CHUA "
           "xac nhan man hinh da thay doi dung nhu mong doi - BAT BUOC "
           "phai doi chieu voi anh chup man hinh o buoc quan sat tiep "
           "theo truoc khi ket luan hanh dong nay thanh cong.";
}

std::optional<std::string> InputTool::handleMove(const nlohmann::json& j) {
    if (!j.contains("x") || !j.contains("y")) return std::nullopt;
    if (!j["x"].is_number_integer() || !j["y"].is_number_integer()) return std::nullopt;

    const int raw_x = j["x"].get<int>();
    const int raw_y = j["y"].get<int>();

    // Ap he so scale_x_/scale_y_ de bu tru sai lech giua toa do model dua
    // ra (theo dung kich thuoc anh chup, model duoc GUIAgentLoop::observe()
    // bao ro) va toa do THUC ma ydotool --absolute nhan dien tren may nay.
    const int x = static_cast<int>(std::lround(raw_x * scale_x_));
    const int y = static_cast<int>(std::lround(raw_y * scale_y_));

    std::ostringstream cmd;
    cmd << "ydotool mousemove --absolute -x " << x << " -y " << y;
    return command_runner_(cmd.str());
}

std::optional<std::string> InputTool::handleClick(const nlohmann::json& j, bool double_click) {
    std::string button = "left";
    if (j.contains("button") && j["button"].is_string()) {
        button = j["button"].get<std::string>();
    }
    auto click_code = buttonToClickCode(button);
    if (!click_code) return std::nullopt;

    // Neu co x,y thi di chuyen chuot truoc (dung chung logic voi handleMove).
    if (j.contains("x") && j.contains("y")) {
        auto move_result = handleMove(j);
        if (!move_result) return std::nullopt;
    }

    std::ostringstream cmd;
    cmd << "ydotool click ";
    if (double_click) {
        cmd << "--repeat 2 --next-delay 80 ";
    }
    cmd << *click_code;

    return command_runner_(cmd.str());
}

std::optional<std::string> InputTool::handleType(const nlohmann::json& j) {
    if (!j.contains("text") || !j["text"].is_string()) return std::nullopt;
    const std::string text = j["text"].get<std::string>();

    std::ostringstream cmd;
    cmd << "ydotool type " << shellEscape(text);
    return command_runner_(cmd.str());
}

std::optional<std::string> InputTool::handleKey(const nlohmann::json& j) {
    if (!j.contains("keys") || !j["keys"].is_string()) return std::nullopt;
    const std::string keys = j["keys"].get<std::string>();

    auto sequence = buildKeySequence(keys);
    if (!sequence) return std::nullopt; // co ten phim khong nhan dien duoc

    std::ostringstream cmd;
    cmd << "ydotool key " << *sequence;
    return command_runner_(cmd.str());
}

std::optional<std::string> InputTool::handleScroll(const nlohmann::json& j) {
    if (!j.contains("dy") || !j["dy"].is_number_integer()) return std::nullopt;
    const int dy = j["dy"].get<int>();
    const int dx = (j.contains("dx") && j["dx"].is_number_integer()) ? j["dx"].get<int>() : 0;

    std::ostringstream cmd;
    cmd << "ydotool mousemove --wheel -x " << dx << " -y " << dy;
    return command_runner_(cmd.str());
}

std::string InputTool::shellEscape(const std::string& raw) {
    std::string escaped = "'";
    for (char c : raw) {
        if (c == '\'') {
            escaped += "'\\''"; // dong quote - escaped quote - mo quote lai
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

std::optional<int> InputTool::keyNameToCode(const std::string& name_in) {
    std::string name = name_in;
    std::transform(name.begin(), name.end(), name.begin(),
                    [](unsigned char c) { return std::tolower(c); });

    // Subset key phu bien theo Linux input-event-codes.h (ban phim US QWERTY).
    // Du dung cho GUI agent: go text (dung "type", khong dung "key" tung chu),
    // dieu huong, phim tat co ban.
    static const std::unordered_map<std::string, int> kKeyMap = {
        {"esc", 1}, {"escape", 1},
        {"1", 2}, {"2", 3}, {"3", 4}, {"4", 5}, {"5", 6},
        {"6", 7}, {"7", 8}, {"8", 9}, {"9", 10}, {"0", 11},
        {"backspace", 14}, {"tab", 15},
        {"q", 16}, {"w", 17}, {"e", 18}, {"r", 19}, {"t", 20},
        {"y", 21}, {"u", 22}, {"i", 23}, {"o", 24}, {"p", 25},
        {"enter", 28}, {"return", 28},
        {"ctrl", 29}, {"control", 29}, {"leftctrl", 29},
        {"a", 30}, {"s", 31}, {"d", 32}, {"f", 33}, {"g", 34},
        {"h", 35}, {"j", 36}, {"k", 37}, {"l", 38},
        {"shift", 42}, {"leftshift", 42},
        {"z", 44}, {"x", 45}, {"c", 46}, {"v", 47}, {"b", 48},
        {"n", 49}, {"m", 50},
        {"alt", 56}, {"leftalt", 56},
        {"space", 57}, {"spacebar", 57},
        {"capslock", 58},
        {"f1", 59}, {"f2", 60}, {"f3", 61}, {"f4", 62}, {"f5", 63},
        {"f6", 64}, {"f7", 65}, {"f8", 66}, {"f9", 67}, {"f10", 68},
        {"f11", 87}, {"f12", 88},
        {"rightctrl", 97},
        {"rightalt", 100}, {"altgr", 100},
        {"home", 102}, {"up", 103}, {"pageup", 104},
        {"left", 105}, {"right", 106},
        {"end", 107}, {"down", 108}, {"pagedown", 109},
        {"insert", 110}, {"delete", 111}, {"del", 111},
        {"super", 125}, {"meta", 125}, {"win", 125}, {"cmd", 125},
    };

    auto it = kKeyMap.find(name);
    if (it == kKeyMap.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> InputTool::buildKeySequence(const std::string& combo) {
    // Tach theo dau '+', vi du "ctrl+alt+t" -> ["ctrl", "alt", "t"]
    std::vector<std::string> tokens;
    std::stringstream ss(combo);
    std::string token;
    while (std::getline(ss, token, '+')) {
        // trim khoang trang thua
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        tokens.push_back(token.substr(start, end - start + 1));
    }
    if (tokens.empty()) return std::nullopt;

    std::vector<int> codes;
    codes.reserve(tokens.size());
    for (const auto& t : tokens) {
        auto code = keyNameToCode(t);
        if (!code) return std::nullopt; // ten phim khong nhan dien duoc
        codes.push_back(*code);
    }

    // Nhan tat ca theo thu tu (:1), roi tha nguoc lai (:0) - dung pattern
    // ydotool dung cho to hop phim, vi du ctrl+alt+F1: 29:1 56:1 59:1 59:0 56:0 29:0
    std::ostringstream out;
    for (int code : codes) out << code << ":1 ";
    for (auto it = codes.rbegin(); it != codes.rend(); ++it) out << *it << ":0 ";

    std::string result = out.str();
    if (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

std::optional<std::string> InputTool::buttonToClickCode(const std::string& button_in) {
    std::string button = button_in;
    std::transform(button.begin(), button.end(), button.begin(),
                    [](unsigned char c) { return std::tolower(c); });

    // Ma hex da gop san down+up (bit 0x40=down, 0x80=up -> ca 2 = click hoan chinh)
    if (button == "left") return "0xC0";
    if (button == "right") return "0xC1";
    if (button == "middle") return "0xC2";
    return std::nullopt;
}