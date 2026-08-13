#include "src/harness/harness_runner.h"
#include <iostream>
#include <thread> // std::thread - chi dung trong runParallel() ben duoi

HarnessRunner::HarnessRunner(std::unique_ptr<Environment> env)
    : env_(std::move(env)) {
    if (!env_) {
        throw std::invalid_argument(
            "HarnessRunner: env khong duoc null (composition bat buoc 1 Environment).");
    }
}

void HarnessRunner::registerEvaluator(std::string type_key, std::unique_ptr<Evaluator> evaluator) {
    if (!evaluator) return;
    evaluators_.push_back({std::move(type_key), std::move(evaluator)});
}

void HarnessRunner::onStepRecorded(Step step) {
    std::print("  [HarnessRunner][{}] step {} type={}", 
               current_task_id_, step.step_id, step.action_type);
               
    if (step.action_type == "tool_call") {
        std::print(" tool={}", step.tool_name);
    }
    
    // std::println tự động chèn \n ở cuối
    std::println(" tokens={} latency={}ms", step.tokens_used, step.latency_ms);
}

Trajectory HarnessRunner::runAgent(AgentLoop& loop, const Task& task) {
    // Observer: gắn hook để mỗi Step agent tạo ra chảy qua
    // HarnessRunner::onStepRecorded mà AgentLoop không cần biết
    // HarnessRunner tồn tại -> đúng "Injects Hook (Observer)" trong diagram.
    loop.setStepHook([this](Step step) { onStepRecorded(std::move(step)); });
    return loop.run(task);
}

void HarnessRunner::runBatch(const std::vector<Task>& tasks, AgentLoop& loop) {
    results_.clear();
    results_.reserve(tasks.size());

    for (const auto& task : tasks) {
        current_task_id_ = task.id;
        std::println("\n########################################");
        std::println("# TASK: {}", task.id);
        std::println("########################################");

        BatchResult result;
        result.task_id = task.id;

        // setup() có thể throw (SandboxEnvironment::setup nem runtime_error
        // neu mkdtemp fail). De loi nay thoat thang khoi ham la hop ly,
        // vi khong co workspace thi khong the chay tiep task nao khac.
        env_->setup();

        // RAII guard: dam bao teardown() LUON chay khi roi khoi scope nay,
        // ke ca khi runAgent()/evaluate() nem exception giua chung (vi du
        // json::type_error chua duoc catch het trong OllamaClient::chat).
        // Khong co guard thi 1 exception giua chung se lam ro ri
        // sandbox_dir_ vinh vien vi teardown() khong bao gio chay.
        struct EnvGuard {
            Environment& env;
            ~EnvGuard() { env.teardown(); }
        } guard{*env_};

        try {
            result.trajectory = runAgent(loop, task);
        } catch (const std::exception& e) {
            std::println(stderr, "[HarnessRunner] Agent crash o task {}: {}", task.id, e.what());
            result.agent_crashed = true;
            results_.push_back(std::move(result));
            continue; // guard van chay teardown() khi continue roi scope
        }

        // Chon DUY NHAT 1 evaluator theo Task::eval_type, khong chay het
        // moi evaluator da dang ky cho moi task - vi KeywordEvaluator,
        // FunctionalEvaluator, VLMEvaluator dinh nghia format eval_script
        // hoan toan khac nhau (CSV keyword / shell command / rubric van
        // xuoi), ap nham la sai ngay.
        const EvaluatorEntry* matched = nullptr;
        for (const auto& entry : evaluators_) {
            if (entry.type_key == task.eval_type) { matched = &entry; break; }
        }

        if (!matched) {
            std::println(stderr, "[HarnessRunner] Khong tim thay evaluator cho eval_type='{}' (task {}), bo qua cham diem.",
                         task.eval_type, task.id);
          // result.score giu nguyen gia tri mac dinh: unexpected(NotYetEvaluated)
        } else {
            try {
                result.score = matched->evaluator->evaluate(result.trajectory, *env_, task);
                if (!result.score) {
                    std::println( "[HarnessRunner] Evaluator '{}'\n khong cham diem duoc task '{}' \n ly do: '{}'", matched->type_key, task.id,toString(result.score.error()) );
                }
            } catch (const std::exception& e) {
                std::println("[HarnessRunner] Evaluator crash o task {}: {}",
                             task.id,
                             e.what());
                // C++23: gio co the gan mot EvalError RIENG cho truong hop
                // nay (EvaluatorThrew) thay vi de result.score "vo tinh"
                // giu nguyen unexpected(NotYetEvaluated) mac dinh — 2 ly do
                // khac nhau (chua chay vs. chay nhung crash) truoc day
                // KHONG the phan biet duoc qua kieu tra ve.
                result.score = std::unexpected(EvalError::EvaluatorThrew);
            }
        }

        // [MOI] task_success = agent hoan thanh dung cach (trajectory.success,
        // nghia la co Final Answer, khong bi loop-detector/timeout cat ngang)
        // VA diem so dat nguong do harness dinh nghia. Tach bach voi
        // trajectory.success vi field do CHI phan anh agent co ket thuc
        // dung dinh dang hay khong, khong phan anh chat luong cau tra loi.
        constexpr float kSuccessThreshold = 1.0f; // chinh nguong "pass" tuy nhu cau
        result.task_success = result.trajectory.success
                            && result.score.has_value()
                            && *result.score >= kSuccessThreshold;

        std::println("[HarnessRunner] Task {} -> score={} completed={} success={}",
                     task.id,
                     result.score ? std::format("{}", *result.score) : "N/A",
                     result.trajectory.success,
                     result.task_success);

        // [FIX QUAN TRONG]: dòng này bị THIẾU ở bản trước — mọi task chạy
        // không crash sẽ build xong `result` đầy đủ rồi bị vứt bỏ khi hết
        // scope vòng lặp, không bao giờ vào results_. Hậu quả: getResults()
        // và printBatchSummary() chỉ thấy được các task CRASH, còn task
        // chạy bình thường biến mất hoàn toàn khỏi kết quả.
        results_.push_back(std::move(result));
        // guard destructor chay teardown() khi het vong lap nay
    }

    printBatchSummary();
}

void HarnessRunner::printBatchSummary() const {
    std::println("\n========================================");
    std::println("BATCH SUMMARY ({} tasks)", results_.size());
    std::println("========================================");

    float total_score  = 0.0f;
    int   scored_count = 0;   // chỉ đếm task THỰC SỰ có điểm, không tính nullopt

    for (const auto& r : results_) {
        std::cout << "  " << r.task_id << ": ";
        if (r.agent_crashed) {
            std::println("CRASHED");
            continue;
        }
        if (r.score) {
            std::println("score={}", *r.score);
            total_score += *r.score;
            ++scored_count;
        } else {
            std::println("N/A ({})",toString(r.score.error()));
        }
    }

    if (scored_count > 0) {
        std::println("Average score (tren {} task co diem): {}",
                     scored_count, (total_score / scored_count));
    } else {
        std::println("Khong co task nao cham diem duoc.");
    }
}

// ════════════════════════════════════════════════════════════════
//  Multi-agent coordination (de bai 10.3, +3d) — TOAN BO phan duoi day
//  la THEM MOI, khong dong nao o tren (runAgent/runBatch/onStepRecorded/
//  printBatchSummary) bi sua.
// ════════════════════════════════════════════════════════════════

void HarnessRunner::logStepThreadSafe(const std::string& label, Step step) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    std::print("  [HarnessRunner][{}] step {} type={}",
               label, step.step_id, step.action_type);

    if (step.action_type == "tool_call") {
        std::print(" tool={}", step.tool_name);
    }

    std::println(" tokens={} latency={}ms", step.tokens_used, step.latency_ms);
}

HarnessRunner::BatchResult HarnessRunner::runOneSubAgent(
        const std::string& agent_id, AgentLoop& loop,
        Environment& env, const Task& task) {
    // Nhan log rieng cho sub-agent nay - CHUP (capture) theo GIA TRI vao
    // lambda ngay tai day (tren thread goi runOneSubAgent(), tuc thread
    // rieng cua job nay trong runParallel() ben duoi), KHONG doc lai
    // current_task_id_ (member dung chung voi duong runBatch() 1-thread)
    // luc hook chay - xem ly do trong .h.
    const std::string label = agent_id + "/" + task.id;
    loop.setStepHook([this, label](Step step) {
        logStepThreadSafe(label, std::move(step));
    });

    BatchResult result;
    result.task_id = task.id;

    // Giong het RAII guard trong runBatch() o tren, nhung boc quanh `env`
    // (tham so, Environment cua RIENG job nay) thay vi *env_ (member dung
    // chung cho duong 1-agent).
    env.setup();
    struct EnvGuard {
        Environment& env;
        ~EnvGuard() { env.teardown(); }
    } guard{env};

    try {
        result.trajectory = loop.run(task);
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            std::println(stderr, "[HarnessRunner][{}] Agent crash o task {}: {}",
                         agent_id, task.id, e.what());
        }
        result.agent_crashed = true;
        return result; // guard van goi env.teardown() khi return roi scope
    }

    // Chon evaluator y het logic trong runBatch(): dung DUY NHAT 1
    // evaluator khop task.eval_type. evaluators_ o day CHI DUOC DOC (xem
    // tien dieu kien trong .h) nen an toan khi nhieu thread cung chay
    // ham nay dong thoi.
    const EvaluatorEntry* matched = nullptr;
    for (const auto& entry : evaluators_) {
        if (entry.type_key == task.eval_type) { matched = &entry; break; }
    }

    if (!matched) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::println(stderr, "[HarnessRunner][{}] Khong tim thay evaluator cho eval_type='{}' (task {}), bo qua cham diem.",
                     agent_id, task.eval_type, task.id);
        // result.score giu nguyen gia tri mac dinh: unexpected(NotYetEvaluated)
    } else {
        try {
            result.score = matched->evaluator->evaluate(result.trajectory, env, task);
            if (!result.score) {
                std::lock_guard<std::mutex> lock(log_mutex_);
                std::println("[HarnessRunner][{}] Evaluator '{}'\n khong cham diem duoc task '{}' \n ly do: '{}'",
                             agent_id, matched->type_key, task.id, toString(result.score.error()));
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(log_mutex_);
            std::println("[HarnessRunner][{}] Evaluator crash o task {}: {}",
                         agent_id, task.id, e.what());
            result.score = std::unexpected(EvalError::EvaluatorThrew);
        }
    }

    constexpr float kSuccessThreshold = 1.0f; // dung chinh nguong voi runBatch()
    result.task_success = result.trajectory.success
                        && result.score.has_value()
                        && *result.score >= kSuccessThreshold;

    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::println("[HarnessRunner][{}] Task {} -> score={} completed={} success={}",
                     agent_id, task.id,
                     result.score ? std::format("{}", *result.score) : "N/A",
                     result.trajectory.success,
                     result.task_success);
    }

    return result;
    // guard destructor goi env.teardown() ngay tai day
}

std::vector<HarnessRunner::SubAgentResult> HarnessRunner::runParallel(std::vector<SubAgentJob> jobs) {
    std::vector<SubAgentResult> results(jobs.size()); // pre-size - moi thread ghi dung 1 phan tu, xem .h
    std::vector<std::thread> threads;
    threads.reserve(jobs.size());

    for (std::size_t i = 0; i < jobs.size(); ++i) {
        // Capture theo REFERENCE toi jobs/results (song het ham nay nho
        // threads.join() ben duoi truoc khi return) nhung CHI THAO TAC
        // qua chi so `i` rieng cua tung thread - moi thread dung dung 1
        // SubAgentJob (jobs[i]) va ghi dung 1 SubAgentResult (results[i]),
        // khong thread nao cham vao phan tu cua thread khac.
        threads.emplace_back([this, &jobs, &results, i]() {
            SubAgentJob& job = jobs[i];
            results[i].agent_id = job.agent_id;
            results[i].result.task_id = job.task.id;

            if (!job.env) {
                std::lock_guard<std::mutex> lock(log_mutex_);
                std::println(stderr, "[HarnessRunner][{}] Job thieu Environment - bo qua.", job.agent_id);
                results[i].result.agent_crashed = true;
                return;
            }
            if (!job.loop) {
                std::lock_guard<std::mutex> lock(log_mutex_);
                std::println(stderr, "[HarnessRunner][{}] Job thieu AgentLoop - bo qua.", job.agent_id);
                results[i].result.agent_crashed = true;
                return;
            }

            results[i].result = runOneSubAgent(job.agent_id, *job.loop, *job.env, job.task);
        });
    }

    for (auto& t : threads) t.join();

    return results;
}