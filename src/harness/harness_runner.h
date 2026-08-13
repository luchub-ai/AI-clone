#pragma once
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <expected>
#include <stdexcept>
#include <format> // C++20
#include <print>  // C++23
#include <mutex>  // log_mutex_ - xem khoi "Multi-agent coordination" ben duoi

#include "src/harness/environment/environment.h"
#include "src/harness/evaluator/evaluator.h"
#include "src/harness/trajectory.h"
#include "src/agent/agent_loop.h"
#include "src/common/task.h"

// ════════════════════════════════════════════════════════════════
//  HarnessRunner — orchestrator tầng trên cùng.
//  KHÔNG tự chạy ReAct loop (việc của AgentLoop), KHÔNG tự chấm điểm
//  (việc của Evaluator), KHÔNG tự tạo workspace (việc của Environment).
//  HarnessRunner chỉ ĐIỀU PHỐI trình tự:
//    setup env -> runAgent -> chọn evaluator theo Task::eval_type
//    -> evaluate -> teardown env -> lặp cho task kế tiếp.
// ════════════════════════════════════════════════════════════════
class HarnessRunner {
public:
    // Kết quả 1 task sau khi chạy xong pipeline — bổ sung so với UML gốc
    // (UML chỉ ghi runBatch() trả void, nhưng void không lưu gì thì
    // không tổng kết batch được).
    struct BatchResult {
        std::string task_id;
        bool        agent_crashed = false;
        // C++23: std::expected<float, EvalError> thay optional<float> cũ.
        // unexpected(EvalError) = "không chấm được" VÀ VÌ SAO (thiếu
        // evaluator khớp eval_type -> NotYetEvaluated là giá trị mặc định
        // dưới đây, evaluator ném exception -> EvaluatorThrew, hoặc
        // evaluator chạy nhưng không ra điểm được với lý do cụ thể của
        // riêng nó - xem eval_error.h). 0.0f = chấm được, agent fail thật.
        // Không cần cờ riêng "evaluator_found" vì expected đã tự mang đủ
        // thông tin đó (và còn mang thêm LÝ DO, thứ optional không có).
        std::expected<float, EvalError> score = std::unexpected(EvalError::NotYetEvaluated);
        Trajectory  trajectory;
        // [MỚI] "task_success" khác hẳn "trajectory.success":
        //   - trajectory.success (set trong AgentLoop::run()) chỉ nghĩa là
        //     agent thoát loop bằng "Final Answer" đúng cú pháp, KHÔNG phản
        //     ánh câu trả lời đúng hay sai.
        //   - task_success ở đây mới là "task có đạt yêu cầu hay không",
        //     tính từ score sau khi evaluator chấm xong. Tách riêng 2 khái
        //     niệm để tránh nhầm lẫn kiểu score=0.33 nhưng in ra success=true.
        bool task_success = false;
    };

    explicit HarnessRunner(std::unique_ptr<Environment> env);

    // Đăng ký 1 evaluator gắn với 1 type_key khớp Task::eval_type
    // (VD: "keyword", "functional", "vlm").
    void registerEvaluator(std::string type_key, std::unique_ptr<Evaluator> evaluator);

    // Chạy 1 agent trên 1 task, trả Trajectory thô — KHÔNG setup/teardown
    // env, KHÔNG chấm điểm. Public để có thể test/debug 1 task đơn lẻ
    // mà không cần chạy nguyên batch.
    Trajectory runAgent(AgentLoop& loop, const Task& task);

    // Chạy pipeline đầy đủ cho nhiều task: setup -> runAgent -> evaluate
    // -> teardown, lặp cho từng task, in báo cáo tổng kết cuối cùng.
    void runBatch(const std::vector<Task>& tasks, AgentLoop& loop);

    [[nodiscard]] const std::vector<BatchResult>& getResults() const { return results_; }

    // ════════════════════════════════════════════════════════════
    //  Multi-agent coordination (de bai 10.3, +3d) — THEM MOI, KHONG
    //  dung/doi runBatch()/runAgent()/onStepRecorded() o tren (van chay
    //  dung nhu cu cho duong 1-agent hien tai).
    //
    //  1 "cong viec" cho 1 sub-agent. `env` va `loop` PHAI la instance
    //  RIENG cho TUNG sub-agent (khong duoc 2 SubAgentJob tro chung 1
    //  Environment/AgentLoop/ToolRegistry) - HarnessRunner khong tu
    //  dung ToolRegistry/Tool nao ca nen KHONG the tu kiem tra dieu
    //  nay, day la trach nhiem cua noi goi (caller) khi wiring, xem
    //  benchmark/multi_agent_demo.cpp de biet vi sao va cach wiring
    //  dung.
    // ════════════════════════════════════════════════════════════
    struct SubAgentJob {
        std::string agent_id;             // nhan dang - dung lam nhan log + phai khop agent_id da gan cho MessageQueueTool cua job nay
        std::unique_ptr<Environment> env; // setup()/teardown() se chay TRONG thread rieng cua job nay
        AgentLoop*  loop = nullptr;       // KHONG so huu - nguoi goi tu tao va giu AgentLoop nay song het runParallel()
        Task        task;
    };

    // Ket qua 1 sub-agent - boc lai BatchResult (da co san o tren) kem
    // agent_id de nguoi goi biet ket qua nay cua ai khi gop bao cao.
    struct SubAgentResult {
        std::string agent_id;
        BatchResult result;
    };

    // Spawn 1 std::thread cho MOI job trong `jobs` (chay dong thoi tat
    // ca, khong gioi han so luong o day), moi thread tu chay pipeline
    // setup -> run -> evaluate -> teardown TREN CHINH Environment/
    // AgentLoop cua job do roi ghi ket qua vao DUNG 1 phan tu cua vector
    // ket qua (khong co 2 thread nao cham vao cung 1 phan tu nen khong
    // can mutex quanh vector ket qua - ghi vao CAC PHAN TU KHAC NHAU cua
    // 1 std::vector da pre-size la an toan). join() tat ca truoc khi tra
    // ve - ket qua tra ve DUNG THEO THU TU `jobs` dau vao, KHONG theo
    // thu tu hoan thanh thuc te.
    //
    // TIEN DIEU KIEN: khong goi registerEvaluator() dong thoi voi loi
    // goi nay dang chay - evaluators_ chi duoc DOC (khong ghi) trong
    // luc runParallel() thuc thi nen nhieu thread doc dong thoi la an
    // toan, nhung se vi pham (data race) neu co 1 thread khac ghi
    // (push_back) vao evaluators_ cung luc.
    std::vector<SubAgentResult> runParallel(std::vector<SubAgentJob> jobs);

private:
    struct EvaluatorEntry {
        std::string type_key;
        std::unique_ptr<Evaluator> evaluator;
    };

    void onStepRecorded(Step step);
    void printBatchSummary() const;

    // === Multi-agent coordination — THEM MOI ===
    // Phien ban "khong dung state dung chung cua duong 1-agent" cua
    // runAgent() + phan than vong lap runBatch() o tren: nhan agent_id
    // + AgentLoop&/Environment&/Task RIENG qua tham so thay vi doc
    // current_task_id_/env_ (member) - vi day co the chay tren 1 thread
    // KHAC voi thread goi runParallel(), doc/ghi 1 member dung chung tu
    // nhieu thread se la data race (dung loai loi UB ma chinh de bai
    // 10.3 dang tranh voi MemoryTool::db_/sqlite3*).
    BatchResult runOneSubAgent(const std::string& agent_id, AgentLoop& loop,
                                 Environment& env, const Task& task);

    // Ghi 1 dong log cho 1 Step, boc trong log_mutex_ - dung rieng cho
    // runParallel() (nhieu thread cung println() cung luc se bi XEN chu
    // giua cac dong voi nhau, khong doc duoc, du std::cout/std::print tu
    // than khong UB khi nhieu thread ghi dong thoi). onStepRecorded() o
    // tren KHONG doi vi runBatch() van chi chay 1 thread, khong can khoa.
    void logStepThreadSafe(const std::string& label, Step step);

    std::unique_ptr<Environment> env_;
    std::vector<EvaluatorEntry>  evaluators_;
    std::vector<BatchResult>     results_;
    std::string                  current_task_id_;  // context cho onStepRecorded (duong 1-agent, KHONG dung cho runParallel)

    // Bao ve console output khi nhieu sub-agent (nhieu thread) cung log
    // trong runParallel(). Lam HarnessRunner het movable (std::mutex
    // khong copy/move duoc) - da kiem tra: HarnessRunner hien chi duoc
    // construct-tai-cho (vd `HarnessRunner harness(std::move(env));`
    // trong run_eval.cpp), khong noi nao move/copy 1 instance da co san,
    // nen doi nay an toan.
    std::mutex log_mutex_;
};