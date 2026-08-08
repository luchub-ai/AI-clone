#include "src/agent/agent_loop.h"
#include <chrono>
#include <functional>

AgentLoop::AgentLoop(std::unique_ptr<LLMClient> llm_ptr,
                     std::shared_ptr<ToolRegistry> tools_ptr,
                     std::unique_ptr<SkillLoader> skills_ptr,
                     std::unique_ptr<LoopDetector> loop_det_ptr)
    : llm(std::move(llm_ptr)),
      tools(std::move(tools_ptr)),
      skills(std::move(skills_ptr)),
      loop_detector(std::move(loop_det_ptr)) {}

void AgentLoop::setStepHook(std::function<void(Step)> hook) {
    step_hook = std::move(hook);
}

std::string AgentLoop::buildSystemPrompt(const Task& task) const {
    std::string skill_text = skills ? skills->selectSkill(task.instruction) : "";
    std::string tools_text = tools ? tools->getToolDescriptions() : "None";

    return std::format(
        "# ROLE & OBJECTIVE\n"
        "You are an AI Agent that STRICTLY PRIORITIZES TOOL USAGE. "
        "Even for basic math or general knowledge, you MUST use a tool if one is available.\n\n"

        "# EXECUTION MODES\n"
        "## MODE 1: TOOL CALL (Primary)\n"
        "- Use this first for any calculation or data retrieval.\n"
        "- Stop generating immediately after 'Action Input:'.\n\n"

        "## MODE 2: ERROR RECOVERY\n"
        "- If you receive a SYNTAX_VIOLATION or Error, try again with correct formatting or a different tool.\n\n"

        "## MODE 3: DIRECT ANSWER (Fallback)\n"
        "- Use ONLY if no tools are available or all tool attempts failed.\n\n"

        "# EXACT FORMAT EXAMPLES (CRITICAL)\n"
        "You MUST follow these exact templates. DO NOT use markdown (**, *, `) for the prefixes 'Thought:', 'Action:', 'Action Input:', or 'Final Answer:'.\n\n"
        
        "--- Example of MODE 1 (Tool Call) ---\n"
        "Thought: I need to solve a math problem. I will use the calculator tool.\n"
        "Action: calculator\n"
        "Action Input: 353 + 116 \n\n"

        "--- Example of MODE 3 (Direct Answer) ---\n"
        "Thought: No tools are applicable for a simple greeting.\n"
        "Final Answer: Hello! How can I help you today?\n\n"

        "# AVAILABLE TOOLS\n"
        "Tools:\n{}\n\n"

        "# CONTEXT\n{}\n\n"

        "# STARK REMINDERS\n"
        "1. NO MARKDOWN for keywords.\n"
        "2. Output EXACTLY ONE 'Action:' per turn if using a tool.\n"
        "3. If no specific tool is explicitly requested, you are expected to deduce the most suitable tools from your available toolset based on the context of the problem.",
        tools_text, skill_text
    );
}

// =====================================================================
// TEMPLATE METHOD: khung thuật toán cố định, giống nhau cho MỌI subclass.
// AgentLoop không biết gì về cách ReActAgentLoop / GUIAgentLoop parse
// action hay gọi LLM cụ thể ra sao — nó chỉ điều phối trình tự.
// =====================================================================
Trajectory AgentLoop::run(const Task& task) {
    Trajectory traj;
    traj.task_id = task.id;
    traj.model   = llm ? llm->getModelName() : "";
    traj.success = false;

    conversation_history.clear();
    std::vector<Step> step_accumulator;

    auto start_time = std::chrono::high_resolution_clock::now();

    // System prompt luôn là Message đầu tiên -> đúng format chat của Ollama
    conversation_history.push_back(Message{"system", buildSystemPrompt(task)});
    // conversation_history.push_back(Message{"user", task.instruction, {task.images_base64}});

    // "Observation" đầu tiên chính là yêu cầu của user
    // test thử task
    // pending_observation = "User Task: " + task.instruction +" " +  task.images_base64;
    if(task.images_base64.empty())
    {
        conversation_history.push_back(Message{"user", task.instruction});
    }else{
        conversation_history.push_back(Message{"user", task.instruction, {task.images_base64}});
    }

    // C++23: Dùng std::println thay cho std::cout << ... << std::endl
    std::println("\n========================================");
    std::println("KICH HOAT AGENT LOOP | TASK: {}", task.id);
    std::println("========================================");

    int current_step = 0;
    while (current_step < task.max_steps) {
        current_step++;
        std::println("\n[TURN {}/{}]", current_step, task.max_steps);

        if(current_step != 1){ // không goi observe cho step 1  
            observe();                       // (1) nạp pending_observation vào history
        }
        std::string thought = think();   // (2) gọi LLM -> cập nhật last_response
        // std::cout << " ==== debug ====" << std::endl;
        // std::cout << conversation_history[conversation_history.size()-1].role + " " + conversation_history[conversation_history.size()-1].content << std::endl;
        // std::cout << thought << std::endl;
        // std::cout << " ==== debug ====" << std::endl;
        Step step_record = act();        // (3) parse action, tự thực thi tool nếu cần
        step_record.step_id = current_step;

        if (step_hook) step_hook(step_record);  // Observer hook cho HarnessRunner
        step_accumulator.push_back(step_record);

        if (loop_detector && loop_detector->detectLoop(step_accumulator)) {
            // C++23: In ra luồng lỗi stderr
            std::println(stderr, "[LOOP DETECTOR] Phat hien vong lap, dung agent.");
            break;
        }

        if (step_record.action_type == "done") {
            traj.success = true;
            // An toàn hơn với std::optional bằng cách dùng value_or
            std::println("[DONE] {}", step_record.tool_result.value_or("No output"));
            break;
        }

        // Chuẩn bị observation cho turn kế tiếp
        pending_observation = std::format("Observation: {}", step_record.tool_result.value_or(""));
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    traj.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              end_time - start_time).count();
    traj.steps = std::move(step_accumulator);

    // C++23 & C++20 Ranges: Tính tổng token theo phong cách Functional Programming
    traj.total_tokens = std::ranges::fold_left(
        traj.steps | std::views::transform(&Step::tokens_used), 
        0, 
        std::plus<>()
    );

    return traj;
}