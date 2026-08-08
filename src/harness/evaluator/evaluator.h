#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <expected>
#include "src/harness/trajectory.h"
#include "src/harness/environment/environment.h"
#include "eval_error.h"
#include "src/common/task.h"
#include "src/client/llm_client.h"   // LLMClient, Message — cần cho VLMEvaluator

// ════════════════════════════════════════════════════════════════
//  Abstract Evaluator  (Strategy Pattern)
//  evaluate() trả std::expected<float, EvalError>:
//    - value trong [0.0, 1.0] -> chấm được (0.0f = fail hoàn toàn,
//      1.0f = pass hoàn toàn)
//    - unexpected(EvalError)  -> KHÔNG THỂ chấm điểm, VÀ vì sao (thiếu
//      input, script/model lỗi...) — xem eval_error.h.
//  [SỬA LỖI - bản gốc]: bản đầu tiên trả float trần, lẫn lộn "không
//  chấm được" với "chấm được và fail" -> làm sai average score ở
//  HarnessRunner khi gom nhiều task.
//  [SỬA LỖI - bản std::optional]: bản kế tiếp tách được "không chấm
//  được" ra thành nullopt, nhưng MỌI nguyên nhân khác nhau (thiếu
//  eval_script, script lỗi fork, model trả rỗng, không tìm thấy dòng
//  SCORE...) vẫn bị dồn chung vào đúng 1 nullopt — chỉ phân biệt được
//  qua dòng std::cerr đứng cạnh, không phải qua kiểu. Xem eval_error.h
//  để biết lý do C++23 std::expected giải quyết đúng vấn đề này.
// ════════════════════════════════════════════════════════════════

class Evaluator {
public:
    virtual ~Evaluator() = default;
    virtual std::expected<float, EvalError> evaluate(const Trajectory& traj,
                                                       const Environment& env,
                                                       const Task&        task) = 0;
};

// ── KeywordEvaluator ──────────────────────────────────────────
// eval_script format: "result, success, completed" (CSV keyword)
// Score = số_keyword_tìm_thấy / tổng_số_keyword
// unexpected(EvalError::NoKeywords) nếu eval_script không parse ra
// keyword nào (không phải lỗi của agent, mà là task setup thiếu sót).
class KeywordEvaluator : public Evaluator {
    std::vector<std::string> parseKeywords(const std::string& script) const;
    bool findKeyword(const std::string& text, const std::string& kw) const;
public:
    std::expected<float, EvalError> evaluate(const Trajectory& traj,
                                              const Environment& env,
                                              const Task&        task) override;
};

// ── FunctionalEvaluator ───────────────────────────────────────
// eval_script format: lệnh shell hoặc đường dẫn executable
// Framework gọi: <eval_script> <path_to_trajectory.json>
//   Exit code 0   -> score 1.0
//   Exit code ≠ 0 -> score 0.0
//   system() lỗi fork -> unexpected(ScriptForkFailed)
//   script bị signal kill -> unexpected(ScriptKilledBySignal)
//   (2 nhánh lỗi hạ tầng này TRƯỚC ĐÂY cùng đổ về -1 rồi nullopt, giờ
//   phân biệt được thẳng từ kiểu trả về — xem functional_evaluator.cpp)
class FunctionalEvaluator : public Evaluator {
    // Trả exit code thật, hoặc unexpected(EvalError) nếu system() không
    // chạy được lệnh tới nơi tới chốn.
    std::expected<int, EvalError> runScript(const std::string& cmd,
                                             const std::string& traj_path) const;
public:
    std::expected<float, EvalError> evaluate(const Trajectory& traj,
                                              const Environment& env,
                                              const Task&        task) override;
};

// ── VLMEvaluator ──────────────────────────────────────────────
// "LLM-as-judge": dùng vlm_client (LLMClient bất kỳ, không nhất thiết
// phải multimodal — tên VLM giữ theo class diagram/Bonus 10.1, nhưng
// hiện tại ReActAgentLoop text-only nên Step chưa có ảnh; nếu sau này
// GUIAgentLoop sinh screenshot, Message::images_base64 sẵn có chỗ để
// nhét ảnh vào history dưới đây mà không cần đổi interface).
//
// eval_script format: tiêu chí chấm điểm dạng văn xuôi tự do, VD:
//   "Agent phải tính đúng kết quả phép cộng và trả lời bằng số cụ thể,
//    không được bịa kết quả nếu tool báo lỗi."
// Nếu eval_script rỗng, dùng task.instruction làm tiêu chí mặc định.
//
// Judge được yêu cầu trả lời có dòng "SCORE: <0.0-1.0>" ở cuối, evaluator
// regex ra số đó. unexpected(EvalError) nếu: vlm_client null
// (JudgeClientNull), chat() trả content rỗng (JudgeEmptyResponse), không
// tìm được dòng SCORE hợp lệ (NoScoreLineFound), hoặc parse số thất bại
// (ScoreParseFailed).
class VLMEvaluator : public Evaluator {
    std::unique_ptr<LLMClient> vlm_client;

    std::string buildJudgePrompt(const Trajectory& traj, const Task& task) const;
    std::expected<float, EvalError> parseScore(const std::string& judge_response) const;
public:
    explicit VLMEvaluator(std::unique_ptr<LLMClient> client);

    std::expected<float, EvalError> evaluate(const Trajectory& traj,
                                              const Environment& env,
                                              const Task&        task) override;
};