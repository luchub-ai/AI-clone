#pragma once
#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

// ════════════════════════════════════════════════════════════════
//  EvalError — thay the cho "std::nullopt vo danh" cua Evaluator ban
//  truoc.
//
//  TRUOC: Evaluator::evaluate() tra std::optional<float>, va MOI ly do
//  "khong cham diem duoc" (KeywordEvaluator het keyword, FunctionalEvaluator
//  eval_script rong / khong tao duoc file tam / system() loi fork / script
//  bi signal kill, VLMEvaluator client null / model tra rong / khong tim
//  thay dong SCORE / parse SCORE that bai...) deu bi don ve chung 1
//  std::nullopt. Nguoi doc CHI phan biet duoc qua dong std::cerr ben canh
//  (xem cac ghi chu [SUA] trong keyword/functional/vlm_evaluator.cpp ban
//  truoc) — ban than KIEU TRA VE khong mang thong tin gi, HarnessRunner
//  muon xu ly khac nhau tuy nguyen nhan la KHONG THE lam duoc tu kieu.
//
//  GIO: moi nguyen nhan la 1 gia tri enum rieng, Evaluator::evaluate()
//  tra std::expected<float, EvalError> (xem evaluator.h) — nguoi goi doc
//  duoc NGAY tu kieu tra ve, khong can doi/parse log.
// ════════════════════════════════════════════════════════════════
enum class EvalError {
    NotYetEvaluated = 0,   // Chua co evaluator nao chay (khong khop Task::eval_type)
    EvaluatorThrew,        // Evaluator::evaluate() nem exception giua chung
    NoKeywords,             // KeywordEvaluator: eval_script khong parse ra keyword nao
    EvalScriptEmpty,        // FunctionalEvaluator: eval_script rong
    TempFileWriteFailed,    // FunctionalEvaluator: khong tao duoc file trajectory tam
    ScriptForkFailed,       // FunctionalEvaluator: system() loi fork
    ScriptKilledBySignal,   // FunctionalEvaluator: eval script bi signal kill
    JudgeClientNull,        // VLMEvaluator: vlm_client la null
    JudgeEmptyResponse,     // VLMEvaluator: LLM giam khao tra ve content rong
    NoScoreLineFound,       // VLMEvaluator: khong tim thay dong "SCORE:" hop le
    ScoreParseFailed,       // VLMEvaluator: parse so tu dong SCORE that bai
};

namespace detail {
inline constexpr std::array<std::string_view, 11> kEvalErrorMessages{
    "chua co evaluator nao chay (khong khop eval_type cua task)",
    "evaluator nem exception giua chung",
    "eval_script khong parse ra keyword nao",
    "eval_script bi rong",
    "khong tao duoc file trajectory tam",
    "system() loi fork khi chay eval script",
    "eval script bi signal kill",
    "vlm_client la null",
    "vlm_client (giam khao) tra ve content rong",
    "khong tim thay dong 'SCORE:' hop le trong judge response",
    "loi parse so thap phan tu dong SCORE",
};
} // namespace detail

// C++23: std::to_underlying thay static_cast<int>(e) tuong minh — khong
// can biet/doan underlying_type thuc su cua EvalError la gi (mac dinh la
// int, nhung neu sau nay doi thanh "enum class EvalError : uint8_t" de
// tiet kiem bo nho thi dong nay van dung nguyen, khong sua gi ca).
[[nodiscard]] inline std::string_view toString(EvalError e) {
    auto idx = static_cast<std::size_t>(std::to_underlying(e));
    return idx < detail::kEvalErrorMessages.size()
               ? detail::kEvalErrorMessages[idx]
               : "loi khong xac dinh";
}
