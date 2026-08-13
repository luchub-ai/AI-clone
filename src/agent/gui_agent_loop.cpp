#include "src/agent/gui_agent_loop.h"

#include <cctype>
#include <iostream>
#include <print>
#include <regex>
#include "utils/timesleep.h"
#include "utils/image_dimensions.h"

namespace {
// Giong het rtrimWhitespace trong react_agent_loop.cpp (ban local rieng,
// vi khong con ke thua ReActAgentLoop de dung chung nua).
std::string rtrimWhitespace(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}
}  // namespace

GUIAgentLoop::GUIAgentLoop(std::unique_ptr<LLMClient> llm_ptr,
                            std::shared_ptr<ToolRegistry> tools_ptr,
                            std::unique_ptr<SkillLoader> skills_ptr,
                            std::unique_ptr<LoopDetector> loop_det_ptr,
                            std::string screenshot_tool_name)
    : AgentLoop(std::move(llm_ptr), std::move(tools_ptr),
                std::move(skills_ptr), std::move(loop_det_ptr)),
      screenshot_tool_name_(std::move(screenshot_tool_name)) {
}

void GUIAgentLoop::observe() {
    // 1) Chup man hinh qua ToolRegistry (khong tu giu ScreenshotTool rieng).
    time_sleep(4000);
    std::optional<std::string> screenshot_path;
    if (tools) {
        screenshot_path = tools->executeTool(screenshot_tool_name_, "{}");
    }

    // Xoa anh trong CAC MESSAGE CU (giu lai text), chi message vua them o
    // duoi day mang anh MOI NHAT.
    for (auto& msg : conversation_history) {
        if (!msg.images_base64.empty()) {
            msg.images_base64.clear();
        }
    }

    std::string text = pending_observation;
    std::vector<std::string> images;

    if (screenshot_path) {
        auto b64 = ImageUtils::imageToBase64String(*screenshot_path);
        if (b64) {
            images.push_back(std::move(*b64));

            // [Dịch tiếng Anh] Bao ro kich thuoc anh + he quy chieu toa do cho model
            if (auto dims = ImageUtils::readPngDimensions(*screenshot_path)) {
                text += "\n[Attached Screenshot] Dimensions: " + std::to_string(dims->first) +
                        "x" + std::to_string(dims->second) +
                        " px. Origin (0,0) is at the TOP-LEFT corner, x increases rightward "
                        "(max " + std::to_string(dims->first - 1) +
                        "), y increases downward (max " +
                        std::to_string(dims->second - 1) + ").";
            }
        } else {
            text += "\n[Canh bao: chup man hinh thanh cong nhung khong doc "
                    "duoc file anh tai " + *screenshot_path + "]";
        }
    } else {
        text += "\n[Canh bao: khong chup duoc man hinh o buoc nay - kiem tra "
                "quyen xdg-desktop-portal hoac Environment::supportsGui()]";
    }

    conversation_history.push_back(Message{"user", text, images});
}

std::string GUIAgentLoop::think() {
    // Fix loi turn 1 bi mu (khong co anh)
    if (conversation_history.size() == 2 && tools) {
        if (auto shot = tools->executeTool(screenshot_tool_name_, "{}")) {
            if (auto b64 = ImageUtils::imageToBase64String(*shot)) {
                Message& task_msg = conversation_history.back(); 
                task_msg.images_base64.push_back(*b64);

                // [Dịch tiếng Anh] Dong bo he toa do o turn 1
                if (auto dims = ImageUtils::readPngDimensions(*shot)) {
                    task_msg.content += "\n[Attached Screenshot] Dimensions: " +
                        std::to_string(dims->first) + "x" + std::to_string(dims->second) +
                        " px. Origin (0,0) is at the TOP-LEFT corner, x increases rightward "
                        "(max " + std::to_string(dims->first - 1) +
                        "), y increases downward (max " +
                        std::to_string(dims->second - 1) + ").";
                }
            } else {
                std::println(stderr, "[GUIAgentLoop] Canh bao: khong doc duoc "
                                     "anh chup man hinh ban dau tai {}", *shot);
            }
        } else {
            std::println(stderr, "[GUIAgentLoop] Canh bao: khong chup duoc "
                                 "man hinh ban dau (turn 1 se chay 'mu').");
        }
    }

    last_response = llm->chat(conversation_history);
    conversation_history.push_back(Message{"assistant", last_response.content, {}});
    return last_response.content;
}

Step GUIAgentLoop::act() {
    // Y het ReActAgentLoop::act() (cung 2 regex, cung logic uu tien
    // tool_regex truoc done_regex) - xem comment goc trong
    // react_agent_loop.cpp de biet ly do thiet ke tung dong regex.
    Step step;
    const std::string& thought = last_response.content;
    step.thought     = thought;
    step.tokens_used = last_response.tokens_used;
    step.latency_ms  = last_response.latency_ms;
 
    // Cho phép bắt đầu bằng Thought hoặc Screen Analysis trước khi tới Action
    static const std::regex tool_regex(
        R"(^[ \t]*Action:[ \t]*([^\n\r]+)\s*[\r\n]+^[ \t]*Action Input:[ \t]*([\s\S]+?)(?=\r?\n[ \t]*(?:Screen Analysis:|Thought:|Action:|Observation:|Final Answer:)|(?![\s\S])))",
        std::regex::ECMAScript | std::regex::multiline);
    static const std::regex done_regex(
        R"(^[ \t]*Final Answer:[ \t]*([\s\S]+))",
        std::regex::ECMAScript | std::regex::multiline);
    std::smatch match;
 
    if (std::regex_search(thought, match, tool_regex)) {
        step.action_type = "tool_call";
        step.tool_name    = rtrimWhitespace(match[1].str());
        step.tool_args    = rtrimWhitespace(match[2].str());
 
        std::println("  [Tool Executor] -> tool name: {} tool_args: {}", step.tool_name, step.tool_args);
 
        if (tools) {
            time_sleep(2000);
            auto opt_res = tools->executeTool(step.tool_name, step.tool_args);
            std::string result = opt_res.value_or("ERROR: Tool failed or returned empty.");
 
            // Ep nhac lai NGAY TRONG DU LIEU (khong chi 1 lan trong system
            // prompt luc dau - se bi "loang" dan khi context dai ra) rang
            // ket qua bat ky tool nao (tru capture_screenshot) KHONG xac
            // nhan trang thai man hinh that su - vi model co xu huong tin
            // vao lich su text no VUA NOI hon la anh no dang thay (vd exec
            // xdg-open bao "Opening in existing browser session." roi model
            // tu suy dien tiep la da thanh cong, du GUI thuc te van dang o
            // workspace/cua so khac chua duoc focus).
            if (step.tool_name != screenshot_tool_name_) {
                result += "\n[GHI CHU HE THONG] Ket qua tren KHONG xac nhan "
                          "man hinh da thay doi dung y - PHAI doi chieu voi "
                          "anh chup man hinh o buoc quan sat tiep theo truoc "
                          "khi coi hanh dong nay la thanh cong hoac dua ra "
                          "Final Answer.";
            }
 
            step.tool_result = result;
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

std::string GUIAgentLoop::buildSystemPrompt(const Task& task) const {
    // 1. Lấy Base Prompt nhưng bỏ đi phần Ví dụ cũ nếu có thể (Hoặc tự viết lại toàn bộ khung)
    // Để an toàn và đồng nhất, tôi khuyên bạn nên viết lại toàn bộ khung cho GUIAgentLoop thay vì cộng chuỗi, 
    // vì GUI Agent có cách format tư duy (Screen Analysis) khác hẳn Text Agent thông thường.
    
    std::string tools_text = tools ? tools->getToolDescriptions() : "None";
    std::string skill_text = skills ? skills->selectSkill(task.instruction) : "";

    std::string prompt = std::format(
        "# ROLE & OBJECTIVE\n"
        "You are an autonomous GUI Agent controlling a real desktop. After every step, a screenshot of the CURRENT screen is attached in the observation.\n"
        "You STRICTLY PRIORITIZE TOOL USAGE. Even for basic math or general knowledge, you MUST use a tool if one is available.\n\n"

        "# AVAILABLE TOOLS\n"
        "Tools:\n{}\n\n"

        "# EXACT FORMAT EXAMPLES (CRITICAL - ANTI-HALLUCINATION)\n"
        "You MUST strictly follow this exact format for EVERY step. DO NOT output 'Thought' until you have explicitly described the image in 'Screen Analysis'.\n\n"
        "--- Example of Tool Call ---\n"
        "Screen Analysis: I see a terminal window. The browser is not visible.\n"
        "Thought: 1. I need to open the browser. 2. I will use the exec tool to launch chrome.\n"
        "Action: exec\n"
        "Action Input: {{\"command\": \"google-chrome --profile-directory=\\\"Default\\\" --new-window https://google.com\"}}\n\n"
        
        "--- Example of Final Answer ---\n"
        "Screen Analysis: I see the email sent confirmation pop-up on the screen.\n"
        "Thought: The task is completed successfully. I will output the Final Answer.\n"
        "Final Answer: I have successfully sent the email.\n\n"

        "# GUI INPUT TOOL SPECIFICATION\n"
        "Coordinates (x,y) are PIXELS on the attached screenshot, origin (0,0) at the TOP-LEFT corner.\n"
        "Khong hien hop thoai xin quyen. Args la JSON, field 'action' bat buoc:\n"
        "  {{\"action\":\"move\",\"x\":<int>,\"y\":<int>}}\n"
        "  {{\"action\":\"click\",\"x\":<int>?,\"y\":<int>?,\"button\":\"left|right|middle\"?}}\n"
        "  {{\"action\":\"type\",\"text\":<string>}}\n"
        "  {{\"action\":\"key\",\"keys\":<string>}}\n"
        "  {{\"action\":\"scroll\",\"dy\":<int>,\"dx\":<int>?}}\n\n"

        "# CRITICAL RULES\n"
        "1. FALLBACK & ERROR RECOVERY: If you see a Terminal when you expect a Browser, it means the application opened in the background. Use Action Input: {{\"action\":\"key\",\"keys\":\"alt+tab\"}} to switch windows.\n"
        "2. ENVIRONMENT IGNORANCE: IGNORE any internal beliefs about your operating environment (e.g., thinking you are in Google Colab). You HAVE FULL CAPABILITY to browse the web and control a real desktop.\n"
        "3. PROACTIVE PLANNING (STEP 0): Before taking your very first action for a new task, your first 'Thought' MUST contain a clear, numbered, step-by-step plan.\n"
        "4. DYNAMIC ADJUSTMENT: If 'Screen Analysis' reveals your previous action failed, DO NOT blindly repeat it. Acknowledge the failure in 'Thought', formulate an alternative workaround, and execute the new plan.\n"
        "5. HARD REQUIREMENT FOR FINAL ANSWER: You MUST NOT call 'Final Answer:' until you visually extract and state the final success state from the screenshot.\n\n",
        tools_text
    );

    if (!skill_text.empty()) {
        prompt += "# TASK SPECIFIC SKILL / STANDARD OPERATING PROCEDURE\n"
                  "You MUST strictly follow the procedure outlined below to solve the current task:\n"
                  + skill_text + "\n\n";
    }

    return prompt;
}