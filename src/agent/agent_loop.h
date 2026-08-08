#pragma once
#include <memory>
#include <vector>
#include <string>
#include <functional>

#include "client/llm_client.h"      // LLMClient, Message, LLMResponse
#include "tools/tool_registry.h"    // ToolRegistry
#include "agent/skill_loader.h"     // SkillLoader
#include "agent/loop_detector.h"    // LoopDetector
#include "src/harness/trajectory.h"     // Task, Step, Trajectory

// #include <format>       // C++20: std::format
// #include <print>        // C++23: std::print, std::println
// #include <ranges>       // C++20: std::views
// #include <algorithm>    // C++23: std::ranges::fold_left không biết nó đc include ở đâu đó để đc sài :))

// =====================================================================
// AgentLoop (abstract)  —  TEMPLATE METHOD PATTERN
//
// run() định nghĩa "khung" cố định của thuật toán: Observe -> Think -> Act
// (lặp lại đến khi Done / hết max_steps / loop_detector chặn).
//
// observe() / think() / act() là các "primitive operations" — đúng như
// class diagram đã nộp: KHÔNG nhận tham số, chữ ký cố định
//   #observe() void*
//   #think()   std::string*
//   #act()     Step*
// Chúng KHÔNG truyền dữ liệu qua tham số hàm cho nhau, mà đọc/ghi lên
// state dùng chung của object (pending_observation, last_response).
// Đây mới đúng bản chất GoF Template Method: subclass chỉ cắm hành vi,
// KHÔNG được thay đổi trình tự gọi (run() không phải virtual).
// =====================================================================
class AgentLoop {
public:
    AgentLoop(std::unique_ptr<LLMClient> llm_ptr,
              std::shared_ptr<ToolRegistry> tools_ptr,
              std::unique_ptr<SkillLoader> skills_ptr,
              std::unique_ptr<LoopDetector> loop_det_ptr);

    virtual ~AgentLoop() = default;

    // Public, KHÔNG virtual -> đúng tinh thần Template Method: cấu trúc
    // thuật toán bất biến, mọi subclass đều chạy chung 1 khung này.
    Trajectory run(const Task& task);

    void setStepHook(std::function<void(Step)> hook);

protected:
    // ---- 3 bước con: subclass BẮT BUỘC override ----
    virtual void        observe() = 0;  // nạp pending_observation vào conversation_history
    virtual std::string think()   = 0;  // gọi llm->chat(conversation_history) -> last_response
    virtual Step         act()    = 0;  // parse last_response -> Step, tự thực thi tool nếu cần

    // Cho phép subclass tuỳ biến system prompt (VD: ReAct text-only vs
    // GUI multimodal) mà không phải chép lại toàn bộ run().
    virtual std::string buildSystemPrompt(const Task& task) const;

    // ---- Composition đúng theo UML ----
    std::unique_ptr<LLMClient>     llm;
    std::shared_ptr<ToolRegistry>  tools;
    std::unique_ptr<SkillLoader>   skills;
    std::unique_ptr<LoopDetector>  loop_detector;
    std::function<void(Step)>      step_hook;
    std::vector<Message>           conversation_history;

    // ---- "Scratch state" luân chuyển giữa 3 bước con trong CÙNG 1 turn ----
    // Đây là cách hợp lệ để giữ observe()/think()/act() KHÔNG tham số mà
    // vẫn nối được dữ liệu: cùng thao tác trên state riêng của object,
    // thay vì kiểu pipeline think(observation)/act(thought) ở bản cũ.
    std::string  pending_observation;  // input cho observe() ở đầu turn
    LLMResponse  last_response;        // output của think(), input cho act()
};