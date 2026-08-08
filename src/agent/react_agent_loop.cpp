#include "src/agent/react_agent_loop.h"
#include <regex>
#include <iostream>
#include <cctype>

namespace {
// static/anonymous-namespace: tránh đụng độ với hàm trim() global (external
// linkage, không có trong header nào) đã tồn tại sẵn ở src/tools/memory_tool.cpp.
// Chỉ cắt whitespace ở CUỐI chuỗi - phần đầu chuỗi tool_args/tool_name vốn
// đã sạch nhờ [ \t]* ngay sau "Action:"/"Action Input:" trong regex.
std::string rtrimWhitespace(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}
}  // namespace

void ReActAgentLoop::observe() {
    // Nạp pending_observation (do run() set trước mỗi turn, hoặc do act()
    // để lại từ turn trước) vào lịch sử hội thoại dưới dạng 1 Message thật.
    conversation_history.push_back(Message{"user", pending_observation, {}});
    // std::cout << "\n\n\n"<< pending_observation  << "\n\n\n";
}

std::string ReActAgentLoop::think() {
    last_response = llm->chat(conversation_history);
    // Ghi lại lời AI vào history để turn sau LLM "nhớ" mình vừa nói gì
    conversation_history.push_back(Message{"assistant", last_response.content, {}});
    return last_response.content;
}

Step ReActAgentLoop::act() {
    Step step;
    const std::string& thought = last_response.content;
    step.thought     = thought;
    step.tokens_used = last_response.tokens_used;
    step.latency_ms  = last_response.latency_ms;

    // Anchor bằng ^ (kèm cờ multiline) để 2 marker chỉ được công nhận khi
    // đứng ở ĐẦU DÒNG - đúng với format mẫu trong buildSystemPrompt(), nơi
    // mỗi keyword ("Action:", "Action Input:", "Final Answer:") luôn mở đầu
    // 1 dòng riêng. Không anchor thì chỉ cần LLM lỡ nhắc tới cụm "Final
    // Answer:" giữa 1 câu Thought bình thường (vd: "Tôi sẽ không đưa ra
    // Final Answer: vội...") là done_regex đã match nhầm.
    // Capture tool_args KHÔNG còn greedy-đến-hết-chuỗi ([\s\S]+) mà dừng
    // trước dòng bắt đầu bằng 1 marker cấu trúc khác (Thought:/Action:/
    // Observation:/Final Answer:), hoặc ở cuối chuỗi nếu model dừng đúng
    // như prompt yêu cầu ("Stop generating immediately after 'Action
    // Input:'"). Lý do: nếu model KHÔNG dừng đúng lúc mà tiếp tục
    // hallucinate sang 1 turn khác trong CÙNG response (vd tự bịa thêm
    // "Thought:"/"Final Answer:" phía sau), bản cũ sẽ nuốt luôn phần đó
    // vào tool_args -> tool nhận input rác. (?![\s\S]) là cách viết "cuối
    // chuỗi tuyệt đối" không phụ thuộc cờ multiline (khác với $, vốn sẽ
    // khớp ở cuối MỖI dòng khi bật multiline, làm gãy args nhiều dòng
    // hợp lệ như nội dung file cần ghi).
    static const std::regex tool_regex(
        R"(^[ \t]*Action:[ \t]*([^\n\r]+)\s*[\r\n]+^[ \t]*Action Input:[ \t]*([\s\S]+?)(?=\r?\n[ \t]*(?:Thought:|Action:|Observation:|Final Answer:)|(?![\s\S])))",
        std::regex::ECMAScript | std::regex::multiline);
    static const std::regex done_regex(
        R"(^[ \t]*Final Answer:[ \t]*([\s\S]+))",
        std::regex::ECMAScript | std::regex::multiline);
    std::smatch match;

    // QUAN TRỌNG: check tool_regex TRƯỚC done_regex (đảo ngược thứ tự cũ).
    // Lý do: pattern của tool_regex chặt hơn hẳn - đòi hỏi ĐỦ cặp
    // "Action:" (đầu dòng) + xuống dòng + "Action Input:" (đầu dòng), nên
    // xác suất bị match nhầm bởi 1 câu văn thuật lại thấp hơn nhiều so với
    // done_regex vốn chỉ cần đúng 1 keyword "Final Answer:" ở đầu dòng.
    // Nhờ vậy nếu LLM lỡ nhắc "Final Answer:" trong lúc suy luận rồi MỚI
    // thật sự gọi Action:, agent vẫn ưu tiên thực thi tool đúng như ý định
    // thật của model, thay vì dừng sớm và nhét nguyên phần Action:/Action
    // Input: chưa parse vào làm câu trả lời cuối.
    if (std::regex_search(thought, match, tool_regex)) {
        step.action_type = "tool_call";
        step.tool_name    = rtrimWhitespace(match[1].str());
        step.tool_args    = rtrimWhitespace(match[2].str());

        // Sử dụng std::println của C++23. 
        // Đã tự động chèn thêm một dấu cách nhỏ giữa tool_name và tool_args để log in ra đẹp hơn.
        std::println("  [Tool Executor] -> tool name: {} tool_args: {}", step.tool_name, step.tool_args);

        if (tools) {
            auto opt_res = tools->executeTool(step.tool_name, step.tool_args);
            step.tool_result = opt_res.value_or("ERROR: Tool failed or returned empty.");
        } else {
            step.tool_result = "ERROR: ToolRegistry is null.";
        }
    }
    else if (std::regex_search(thought, match, done_regex)) {
        step.action_type = "done";
        step.tool_result  = match[1].str();
    }
    else {
        step.action_type = "error";
        step.tool_result  = "SYNTAX_VIOLATION: expected 'Action:' or 'Final Answer:'";
    }

    return step;
}