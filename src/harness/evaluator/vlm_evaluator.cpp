#include "evaluator.h"
#include <iostream>
#include <regex>
#include <algorithm>
#include <sstream>

VLMEvaluator::VLMEvaluator(std::unique_ptr<LLMClient> client)
    : vlm_client(std::move(client)) {}

std::string VLMEvaluator::buildJudgePrompt(const Trajectory& traj, const Task& task) const {
    std::string criteria = task.eval_script.empty() ? task.instruction : task.eval_script;

    std::ostringstream oss;
    oss << "Ban la giam khao cham diem cho mot AI agent. Doc lai qua trinh agent "
        << "thuc hien task ben duoi va cham diem tu 0.0 (hoan toan fail) den 1.0 "
        << "(hoan thanh xuat sac), dua tren tieu chi sau:\n\n"
        << "TIEU CHI: " << criteria << "\n\n"
        << "TASK GOC: " << task.instruction << "\n\n"
        << "QUA TRINH THUC HIEN CUA AGENT:\n";

    for (const auto& step : traj.steps) {
        oss << "--- Step " << step.step_id << " ---\n";
        if (!step.thought.empty())     oss << "Thought: " << step.thought << "\n";
        if (step.action_type == "tool_call") {
            oss << "Tool: " << step.tool_name << "(" << step.tool_args << ")\n"
                << "Result: " << step.tool_result.value_or("no result") << "\n";
        } else if (step.action_type == "done") {
            oss << "Final Answer: " << step.tool_result.value_or("no result") << "\n";
        } else {
            oss << "Error: " << step.tool_result.value_or("no result") << "\n";
        }
    }

    oss << "\nAgent tuyen bo thanh cong: " << (traj.success ? "co" : "khong") << "\n\n"
        << "Hay phan tich ngan gon, sau do BAT BUOC ket thuc bang chinh xac 1 dong "
        << "dang: SCORE: <so thap phan tu 0.0 den 1.0>";

    return oss.str();
}

std::expected<float, EvalError> VLMEvaluator::parseScore(const std::string& judge_response) const {
    // Bắt số dạng "SCORE: 0.8" hoặc "SCORE:1" — case-insensitive
    static const std::regex score_regex(
        R"(SCORE:\s*([01](?:\.\d+)?))", std::regex::icase);

    std::smatch match;
    if (!std::regex_search(judge_response, match, score_regex)) {
        std::cerr << "[VLMEval] Khong tim thay dong 'SCORE:' hop le trong judge response.\n";
        return std::unexpected(EvalError::NoScoreLineFound);
    }

    try {
        float score = std::stof(match[1].str());
        // Kẹp trong [0.0, 1.0] phòng model trả số ngoài khoảng dù đã ràng buộc prompt
        score = std::clamp(score, 0.0f, 1.0f);
        return score;
    } catch (const std::exception&) {
        std::cerr << "[VLMEval] Loi parse so tu SCORE line: " << match[1].str() << "\n";
        return std::unexpected(EvalError::ScoreParseFailed);
    }
}

std::expected<float, EvalError> VLMEvaluator::evaluate(const Trajectory& traj,
                                                         const Environment& /*env*/,
                                                         const Task&        task) {
    if (!vlm_client) {
        std::cerr << "[VLMEval] vlm_client la null -> khong the cham diem.\n";
        return std::unexpected(EvalError::JudgeClientNull);
    }

    std::vector<Message> judge_history;
    judge_history.push_back(Message{"user", buildJudgePrompt(traj, task), {}});

    LLMResponse resp = vlm_client->chat(judge_history);

    if (resp.content.empty()) {
        // Đây chính là điểm mù đã nói ở review OllamaClient trước: chat()
        // trả content rỗng cho CẢ hai trường hợp "model thật sự trả rỗng"
        // lẫn "network/HTTP/JSON lỗi". VLMEvaluator không phân biệt được,
        // nên coi cả hai là JudgeEmptyResponse hiện tại. Nếu LLMResponse
        // sau này có thêm field success/error thì chỗ này nên check field
        // đó (và tách thành EvalError riêng) thay vì suy luận qua
        // content.empty().
        std::cerr << "[VLMEval] vlm_client tra ve content rong -> khong the cham diem.\n";
        return std::unexpected(EvalError::JudgeEmptyResponse);
    }

    std::cout << "[VLMEval] Judge raw response:\n" << resp.content << "\n";
    return parseScore(resp.content);
}