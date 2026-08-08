#include "src/tools/calculator_tool.h"
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <algorithm>

[[nodiscard]] std::string CalculatorTool::getName() const {
    return "calculator";
}

[[nodiscard]] std::string CalculatorTool::getDescription() const {
    return "Công cụ tính toán biểu thức số học Latex chuẩn. "
           "Ho tro +, -, *, /, ^, \\frac{a}{b}, \\sqrt{x}, \\sqrt[n]{x}, "
           "cac ham (\\log, \\ln, \\sin, \\cos, \\tan), hang so \\pi, e, và dấu ngoặc. "
           "ví dụ: \\frac{\\sqrt{5^2+12^2}}{13}";
}

namespace {

constexpr int kMaxRecursionDepth = 64;

// =====================================================================
// 1. LATEX-AWARE PREPROCESSOR
// Thay vi regex phang, ta duyet ky tu va xu ly \frac{}{} , \sqrt[]{} , cac
// hang so va ham truoc khi dua vao tokenizer. Xu ly dung ngoac nhon long nhau.
// =====================================================================

// Tim vi tri dong cua ngoac nhon bat dau tai `open` (open phai tro toi '{')
size_t matchBrace(const std::string& s, size_t open) {
    if (open >= s.size() || s[open] != '{') {
        throw std::runtime_error("Thieu dau '{' sau lenh LaTeX");
    }
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        if (s[i] == '{') depth++;
        else if (s[i] == '}') {
            depth--;
            if (depth == 0) return i;
        }
    }
    throw std::runtime_error("Thieu dau '}' dong ngoac LaTeX");
}

// Doc phan noi dung ben trong { ... } (khong bao gom ngoac), tra ve string
// va cap nhat vi tri con tro `pos` ra sau dau '}'.
std::string readBraceGroup(const std::string& s, size_t& pos) {
    // bo qua khoang trang
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) pos++;
    if (pos >= s.size() || s[pos] != '{') {
        throw std::runtime_error("Ky vong '{' tai vi tri " + std::to_string(pos));
    }
    size_t close = matchBrace(s, pos);
    std::string inner = s.substr(pos + 1, close - pos - 1);
    pos = close + 1;
    return inner;
}

// Đệ quy tiền xử lý một chuỗi LaTeX thành biểu thức "phẳng" dùng ()  * /
// dấu ngoặc thường, để tokenizer/parser phía sau xử lý tiếp.
std::string preprocessLatex(const std::string& raw, int depth = 0) {
    if (depth > kMaxRecursionDepth) {
        throw std::runtime_error("Bieu thuc qua phuc tap (long nhau qua sau)");
    }

    std::string out;
    out.reserve(raw.size());

    for (size_t i = 0; i < raw.size(); ) {
        char c = raw[i];

        if (c == '\\') {
            // Đọc tên lệnh (chỉ chữ cái) ngay sau backslash
            size_t j = i + 1;
            while (j < raw.size() && std::isalpha(static_cast<unsigned char>(raw[j]))) j++;
            std::string cmd = raw.substr(i + 1, j - (i + 1));
            i = j;

            if (cmd == "left" || cmd == "right") {
                // \left( \right) -> chỉ lấy ký tự ngoặc theo sau, bỏ qua khoảng trắng
                while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) i++;
                if (i < raw.size() && raw[i] == '.') { i++; /* \left. vô hình */ }
                else if (i < raw.size()) { out += raw[i]; i++; }
                continue;
            }
            if (cmd == "cdot" || cmd == "times") { out += "*"; continue; }
            if (cmd == "div") { out += "/"; continue; }
            if (cmd == "pi") { out += "(3.14159265358979323846)"; continue; }
            if (cmd == "ln") { out += "ln"; continue; }
            if (cmd == "log") { out += "log"; continue; }
            if (cmd == "sin" || cmd == "cos" || cmd == "tan") { out += cmd; continue; }
            if (cmd == "frac") {
                size_t p = i;
                std::string num = readBraceGroup(raw, p);
                std::string den = readBraceGroup(raw, p);
                out += "((" + preprocessLatex(num, depth + 1) + ")/(" +
                       preprocessLatex(den, depth + 1) + "))";
                i = p;
                continue;
            }
            if (cmd == "sqrt") {
                size_t p = i;
                // Kiểm tra căn bậc n: \sqrt[n]{x}
                while (p < raw.size() && std::isspace(static_cast<unsigned char>(raw[p]))) p++;
                std::string rootExpr = "2"; // mặc định căn bậc 2
                if (p < raw.size() && raw[p] == '[') {
                    size_t close = raw.find(']', p);
                    if (close == std::string::npos)
                        throw std::runtime_error("Thieu ']' trong \\sqrt[n]{}");
                    rootExpr = raw.substr(p + 1, close - p - 1);
                    p = close + 1;
                }
                std::string radicand = readBraceGroup(raw, p);
                // dùng dạng x^(1/n) để tái sử dụng cơ chế lũy thừa có sẵn
                out += "((" + preprocessLatex(radicand, depth + 1) + ")^(1/(" +
                       preprocessLatex(rootExpr, depth + 1) + ")))";
                i = p;
                continue;
            }
            // Lệnh không rõ: bỏ qua tên lệnh, giữ nguyên phần còn lại
            continue;
        }

        if (c == '{') {
            // Nhóm { ... } không đi kèm lệnh -> coi như dấu ngoặc thường
            size_t close = matchBrace(raw, i);
            std::string inner = raw.substr(i + 1, close - i - 1);
            out += "(" + preprocessLatex(inner, depth + 1) + ")";
            i = close + 1;
            continue;
        }

        if (c == '}') {
            // Không nên gặp riêng lẻ, bỏ qua an toàn
            i++;
            continue;
        }

        out += c;
        i++;
    }

    return out;
}

// =====================================================================
// 2. TOKENIZER
// =====================================================================
enum TokenType { OP, NUM, ID, LPAREN, RPAREN, NONE };

std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    std::string clean;
    clean.reserve(expr.size());
    for (char c : expr) {
        if (!std::isspace(static_cast<unsigned char>(c))) clean += c;
    }

    TokenType last_type = NONE;
    size_t i = 0;

    while (i < clean.size()) {
        char c = clean[i];

        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
            tokens.push_back(std::string(1, c));
            last_type = OP;
            i++;
        }
        else if (c == '(' || c == ')') {
            if (c == '(') {
                if (last_type == NUM || last_type == RPAREN) {
                    tokens.push_back("*"); // 2(x) -> 2*(x) hoặc (x)(y) -> (x)*(y)
                } else if (last_type == ID) {
                    // Chỉ chèn dấu '*' nếu ID trước đó KHÔNG phải là một tên hàm (ví dụ hằng số 'e')
                    std::string last_token = tokens.back();
                    if (last_token != "sin" && last_token != "cos" && last_token != "tan" &&
                        last_token != "ln" && last_token != "log" && last_token != "log10" && 
                        last_token != "sqrt") {
                        tokens.push_back("*");
                    }
                }
            }
            tokens.push_back(std::string(1, c));
            last_type = (c == '(') ? LPAREN : RPAREN;
            i++;
        }
        else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            if (last_type == RPAREN || last_type == ID) tokens.push_back("*");
            std::string num;
            int dotCount = 0;
            while (i < clean.size() &&
                   (std::isdigit(static_cast<unsigned char>(clean[i])) || clean[i] == '.')) {
                if (clean[i] == '.') {
                    if (++dotCount > 1)
                        throw std::runtime_error("So thap phan khong hop le: " + num + ".");
                }
                num += clean[i++];
            }
            tokens.push_back(num);
            last_type = NUM;
        }
        else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            if (last_type == NUM || last_type == RPAREN) {
                tokens.push_back("*");
            }
            std::string name;
            while (i < clean.size() &&
                   (std::isalnum(static_cast<unsigned char>(clean[i])) || clean[i] == '_')) {
                name += clean[i++];
            }
            tokens.push_back(name);
            last_type = ID;
        }
        else {
            // Ký tự lạ sau khi đã tiền xử lý LaTeX -> báo lỗi rõ ràng thay vì
            // âm thầm bỏ qua (an toàn hơn cho một "calculator chính xác").
            throw std::runtime_error(std::string("Ky tu khong hop le: '") + c + "'");
        }
    }
    return tokens;
}

// =====================================================================
// 3. RECURSIVE DESCENT PARSER
// Độ ưu tiên chuẩn: unary (+/-) < power (^) và power bên trái KHÔNG được
// "nuốt" dấu trừ phía trước nó, để -2^2 == -(2^2) == -4.
// =====================================================================
class MathParser {
    const std::vector<std::string>& tokens;
    size_t pos = 0;
    int depth = 0;

    std::string peek() const { return pos < tokens.size() ? tokens[pos] : ""; }
    std::string get() { return pos < tokens.size() ? tokens[pos++] : ""; }

    void enter() {
        if (++depth > kMaxRecursionDepth)
            throw std::runtime_error("Bieu thuc qua phuc tap (qua nhieu ngoac long nhau)");
    }
    void leave() { depth--; }

public:
    explicit MathParser(const std::vector<std::string>& tk) : tokens(tk) {}

    bool isAtEnd() const { return pos >= tokens.size(); }

    double parseExpression() {
        enter();
        double left = parseTerm();
        while (peek() == "+" || peek() == "-") {
            std::string op = get();
            double right = parseTerm();
            left = (op == "+") ? left + right : left - right;
        }
        leave();
        return left;
    }

    double parseTerm() {
        enter();
        double left = parseUnary();
        while (peek() == "*" || peek() == "/") {
            std::string op = get();
            double right = parseUnary();
            if (op == "*") {
                left *= right;
            } else {
                if (right == 0.0) throw std::runtime_error("Phep chia cho 0");
                left /= right;
            }
        }
        leave();
        return left;
    }

    // Unary nằm TRÊN power trong độ ưu tiên: -2^2 => -(2^2)
    double parseUnary() {
        if (peek() == "-") {
            get();
            return -parseUnary();
        }
        if (peek() == "+") {
            get();
            return parseUnary();
        }
        return parsePower();
    }

    // Cấp 3: Lũy thừa — RIGHT-ASSOCIATIVE qua đệ quy.
    // Vế trái dùng parseFactor (không phải parseUnary) để "-2^2" vẫn ra -4
    // (dấu trừ được xử lý ở tầng parseUnary phía trên, bao ngoài parsePower).
    double parsePower() {
        enter();
        double left = parseFactor();
        if (peek() == "^") {
            get();
            double right = parsePower(); // đệ quy phải: 2^3^2^2 -> 2^(3^(2^2))
            left = std::pow(left, right);
        }
        leave();
        return left;
    }

    double parseFactor() {
        std::string t = get();
        if (t.empty()) throw std::runtime_error("Cu phap thieu toan hang");

        if (t == "(") {
            double val = parseExpression();
            if (get() != ")") throw std::runtime_error("Thieu dau ngoac dong ')'");
            return val;
        }

        if (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '.') {
            try {
                size_t consumed = 0;
                double v = std::stod(t, &consumed);
                if (consumed != t.size())
                    throw std::runtime_error("So khong hop le: " + t);
                return v;
            } catch (const std::out_of_range&) {
                throw std::runtime_error("So qua lon: " + t);
            } catch (const std::invalid_argument&) {
                throw std::runtime_error("So khong hop le: " + t);
            }
        }

        if (std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_') {
            if (t == "e") return 2.718281828459045235360287471352;

            if (peek() == "(") {
                get();
                double arg = parseExpression();
                if (get() != ")")
                    throw std::runtime_error("Thieu ngoac dong cho ham '" + t + "'");

                if (t == "sqrt") {
                    if (arg < 0) throw std::runtime_error("Khong the can bac hai so am");
                    return std::sqrt(arg);
                }
                if (t == "log" || t == "log10") {
                    if (arg <= 0) throw std::runtime_error("log cua so <= 0 khong xac dinh");
                    return std::log10(arg);
                }
                if (t == "ln") {
                    if (arg <= 0) throw std::runtime_error("ln cua so <= 0 khong xac dinh");
                    return std::log(arg);
                }
                if (t == "sin") return std::sin(arg);
                if (t == "cos") return std::cos(arg);
                if (t == "tan") return std::tan(arg);
                throw std::runtime_error("Ham khong duoc ho tro: " + t);
            }
            throw std::runtime_error("Chua dinh nghia gia tri cho bien: " + t);
        }
        throw std::runtime_error("Cu phap khong hop le tai: " + t);
    }
};

// Format số: giữ độ chính xác cao nhưng bỏ số 0 thừa ở cuối.
std::string formatResult(double result) {
    if (std::isnan(result)) throw std::runtime_error("Ket qua khong xac dinh (NaN)");
    if (std::isinf(result)) throw std::runtime_error("Ket qua vo cung (Infinity)");

    std::ostringstream oss;
    oss << std::setprecision(15) << std::fixed << result;
    std::string s = oss.str();

    // Xóa số 0 thừa sau dấu thập phân, và dấu '.' nếu không cần.
    if (s.find('.') != std::string::npos) {
        size_t lastNonZero = s.find_last_not_of('0');
        if (s[lastNonZero] == '.') lastNonZero--;
        s.erase(lastNonZero + 1);
    }
    return s;
}

} // namespace

// =====================================================================
// 4. MAIN EXECUTE INTERFACE
// =====================================================================
std::optional<std::string> CalculatorTool::execute(const std::string& args) {
#ifdef CALC_TOOL_DEBUG
    std::cerr << "[TOOL DEBUG] Input: " << args << "\n";
#endif
    try {
        std::string flat = preprocessLatex(args);
        std::vector<std::string> tokens = tokenize(flat);

        if (tokens.empty()) {
            return "Loi tinh toan: bieu thuc rong";
        }

        MathParser parser(tokens);
        double result = parser.parseExpression();

        if (!parser.isAtEnd()) {
            throw std::runtime_error("Con du thua ky tu sau bieu thuc hop le");
        }

        std::string final_res = formatResult(result);

#ifdef CALC_TOOL_DEBUG
        std::cerr << "[RESULT]: " << final_res << "\n";
#endif
        return final_res;
    }
    catch (const std::exception& e) {
        return "Loi tinh toan: " + std::string(e.what());
    }
}