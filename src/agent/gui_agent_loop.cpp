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
    Step step;
    const std::string& thought = last_response.content;
    step.thought     = thought;
    step.tokens_used = last_response.tokens_used;
    step.latency_ms  = last_response.latency_ms;

    static const std::regex tool_regex(
        R"(^[ \t]*Action:[ \t]*([^\n\r]+)\s*[\r\n]+^[ \t]*Action Input:[ \t]*([\s\S]+?)(?=\r?\n[ \t]*(?:Thought:|Action:|Observation:|Final Answer:)|(?![\s\S])))",
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
            time_sleep(4000);
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

std::string GUIAgentLoop::buildSystemPrompt(const Task& task) const {
    std::string base = AgentLoop::buildSystemPrompt(task);

    // Lấy mô tả của các tool đã được đăng ký trong hệ thống (nếu có)
    std::string extra_tools = tools ? tools->getToolDescriptions() : "No additional tools available.";

    base +=
        "\n\n# GUI AGENT - VISUAL REASONING & EXECUTION GUIDELINES\n"
        "You are an autonomous GUI Agent controlling a real desktop. After every step, a screenshot of the CURRENT screen is attached in the observation.\n\n"

        "## GENERAL TOOLS SPECIFICATION\n"
        "In addition to GUI actions, you have access to the following tools:\n"
        + extra_tools + "\n\n"

        "## GUI INPUT TOOL SPECIFICATION\n"
        "To interact with the OS, use Action: gui_input with Action Input as a JSON payload.\n"
        "Khong hien hop thoai xin quyen. Args la JSON, field 'action' bat buoc:\n"
        "  {\"action\":\"move\",\"x\":<int>,\"y\":<int>}\n"
        "  {\"action\":\"click\",\"x\":<int>?,\"y\":<int>?,\"button\":\"left|right|middle\"?}\n"
        "  {\"action\":\"double_click\",\"x\":<int>?,\"y\":<int>?,\"button\":\"left|right|middle\"?}\n"
        "  {\"action\":\"type\",\"text\":<string>}\n"
        "  {\"action\":\"key\",\"keys\":<string>}  (vi du \"enter\", \"ctrl+c\", \"ctrl+alt+t\")\n"
        "  {\"action\":\"scroll\",\"dy\":<int>,\"dx\":<int>?}\n"
        "x,y trong click/double_click la optional: co thi di chuyen chuot toi truoc khi click.\n\n"
        
        "Coordinates (x,y) are PIXELS on the attached screenshot, origin (0,0) "
        "at the TOP-LEFT corner, x increasing rightward, y increasing downward. "
        "The exact image size is stated right after each screenshot in the "
        "observation - use it as the valid coordinate range.\n\n"

        "## CRITICAL RULE 1: MANDATORY VISUAL VERIFICATION\n"
        "- BEFORE generating the next Action, inspect the latest screenshot carefully.\n"
        "- DO NOT assume an action succeeded just because you sent the input. Verify that the screen actually changed as intended.\n"
        "- NEVER output 'Final Answer:' on the step immediately following an action (like typing or clicking) unless you have confirmed the final result visually on the latest screenshot.\n\n"

        "## CRITICAL RULE 2: FALLBACK & ERROR RECOVERY STRATEGIES\n"
        "If the screen does NOT show the expected result (e.g., input field was missed, window didn't open, browser is slow):\n"
        "1. DO NOT repeat the exact same failing action continuously.\n"
        "2. Retry with a Fallback Strategy:\n"
        "   - Target input fields specifically by clicking their coordinates first ({\"action\":\"click\",\"x\":X,\"y\":Y}) before typing.\n"
        "   - If typing failed, send 'enter' or click the 'Search/Submit' button on screen.\n"
        "   - If a GUI app fails to respond, fallback to terminal/CLI execution if applicable.\n\n"

        "## CRITICAL RULE 3: ENVIRONMENT IGNORANCE & TASK FOCUS\n"
        "- IGNORE any internal beliefs about your operating environment (e.g., thinking you are in Google Colab, a headless server, or a text-only interface).\n"
        "- You HAVE FULL CAPABILITY to browse the web and control a real desktop.\n"
        "- If the current interface is not what you need, DO NOT complain or output 'Final Answer'. Instead, proactively use tools (like `exec_tool` to launch a browser), visually analyze the resulting web page screenshot, and use `gui_input` to click exactly where needed.\n\n"

        "## HARD REQUIREMENT FOR FINAL ANSWER:\n"
        "- You MUST NOT call 'Final Answer:' until you explicitly extract and state the visual information from the screenshot.\n"
        "- For example, if the task is searching for weather, your 'Final Answer:' MUST contain the actual temperature and condition numbers visible on the screen.\n"
        "- If you cannot read the specific information from the screenshot yet, you MUST continue acting (e.g., wait, scroll, or search again).\n\n"

        "Each gui_input execution automatically pauses for screen settlement, so you do NOT need manual wait steps.";

    return base;
}