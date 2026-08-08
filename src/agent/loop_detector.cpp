#include "loop_detector.h"
#include <format>       // C++20: Định dạng chuỗi
#include <print>        // C++23: std::println
#include <ranges>       // C++20: std::views
// #include <algorithm>    // C++20: std::ranges::distance (chả hiểu sao cái này lúc cần lúc không)

LoopDetector::LoopDetector(int warn_thresh, int crit_thresh)
    : warning_threshold(warn_thresh), critical_threshold(crit_thresh) {}

bool LoopDetector::detectLoop(const std::vector<Step>& history) const {
    if (history.size() < static_cast<size_t>(warning_threshold)) return false;

    const auto& last = history.back();

    // C++20 Views: Tạo một luồng ảo duyệt ngược history và bỏ qua phần tử cuối cùng (chính là 'last')
    // Bước này giúp tránh copy dữ liệu và tái sử dụng cho cả 3 phép đếm bên dưới.
    auto reversed_prev_steps = history | std::views::reverse | std::views::drop(1);

    // --- LOẠI 1: GENERIC REPEAT (Lặp hành động y hệt liên tiếp) ---
    if (last.action_type == "tool_call") {
        // C++20 Ranges: Lấy các phần tử thỏa mãn điều kiện (take_while) và đếm số lượng (distance)
        int consecutive_repeats = 1 + std::ranges::distance(
            reversed_prev_steps | std::views::take_while([&](const Step& s) {
                return s.action_type == "tool_call" && 
                       s.tool_name == last.tool_name && 
                       s.tool_args == last.tool_args;
            })
        );
        
        for (auto it = history.rbegin() + 1; it != history.rend(); ++it) {
            if (it->action_type == "tool_call" && 
                it->tool_name == last.tool_name && 
                it->tool_args == last.tool_args) {
                consecutive_repeats++;
            } else {
                break;
            }
        }

        if (consecutive_repeats >= critical_threshold) {
            // C++23: std::println thay cho toán tử << (truyền stderr để in ra luồng lỗi)
            std::println(stderr, "[CRITICAL LOOP] Agent kẹt lặp lệnh: {}({}) {} lần!", 
                         last.tool_name, last.tool_args, consecutive_repeats);
            return true;
        } else if (consecutive_repeats >= warning_threshold) {
            std::println(stderr, "[Warning Loop] Agent đang có xu hướng lặp lệnh: {}", last.tool_name);
        }
    }

    // --- LOẠI 2: ERROR/INVALID LOOP (Lặp lỗi parse, không sinh ra tool call) ---
    // Giả sử hàm act() của bạn gán action_type là "error" hoặc "invalid" khi LLM trả sai format
    if (last.action_type == "error") {
        int consecutive_errors = 1 + std::ranges::distance(
            reversed_prev_steps | std::views::take_while([](const Step& s) {
                return s.action_type == "error" || s.action_type == "invalid" || s.action_type == "unknown";
            })
        );

        if (consecutive_errors >= critical_threshold) {
            std::println(stderr, "[CRITICAL LOOP] Agent kẹt lặp lỗi định dạng {} lần!", consecutive_errors);
            return true;
        } else if (consecutive_errors >= warning_threshold) {
            std::println(stderr, "[Warning Loop] Agent đang liên tục trả về sai định dạng (Error State)...");
        }
    }

    // --- LOẠI 3: THOUGHT LOOP (LLM nói nhảm y hệt nhau dù action_type là gì) ---
    // Phòng trường hợp LLM cứ lặp đi lặp lại 1 câu xin lỗi (vì thought thì các AI thường chỉ làm đúng 1 lần và sử dụng tool call, nên ko có chuyện suy nghĩ nhiều bước)
    if (!last.thought.empty()) {
        int thought_repeats = 1 + std::ranges::distance(
            reversed_prev_steps | std::views::take_while([&](const Step& s) {
                return s.thought == last.thought;
            })
        );
        
        if (thought_repeats >= critical_threshold) {
            std::println(stderr, "[CRITICAL LOOP] Agent kẹt nói nhảm (lặp Thought) {} lần!", thought_repeats);
            return true;
        } else if (thought_repeats >= warning_threshold) {
            std::println(stderr, "[Warning Loop] Agent có dấu hiệu lặp lại luồng suy nghĩ (Thought)...");
        }
    }
    // --- LOẠI 4: PING-PONG LOOP (Đánh bóng bàn A -> B -> A -> B) ---
    return isPingPongLoop(history);
    
}

bool LoopDetector::isPingPongLoop(const std::vector<Step>& history) const {
    if (history.size() < 4) return false;

    size_t n = history.size();
    const Step& s0 = history[n - 1]; // A
    const Step& s1 = history[n - 2]; // B
    const Step& s2 = history[n - 3]; // A'
    const Step& s3 = history[n - 4]; // B'

    bool a_match = (s0.tool_name == s2.tool_name && s0.tool_args == s2.tool_args);
    bool b_match = (s1.tool_name == s3.tool_name && s1.tool_args == s3.tool_args);
    bool distinct = (s0.tool_name != s1.tool_name || s0.tool_args != s1.tool_args);

    if (a_match && b_match && distinct) {
        // C++23: Tối ưu in log
        std::println(stderr, "[CRITICAL PING-PONG] kẹt đánh bóng bàn giữa [{}] và [{}]", s0.tool_name, s1.tool_name);
        return true;
    }
    return false;
}