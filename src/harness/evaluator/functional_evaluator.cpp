#include "evaluator.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <sys/wait.h>

std::expected<int, EvalError> FunctionalEvaluator::runScript(const std::string& cmd,
                                                               const std::string& traj_path) const {
    std::string full_cmd = cmd + " " + traj_path;
    int ret = std::system(full_cmd.c_str());

    if (ret == -1) {
        std::cerr << "[FunctionalEval] system() failed to fork.\n";
        return std::unexpected(EvalError::ScriptForkFailed);
    }
    if (!WIFEXITED(ret)) {
        std::cerr << "[FunctionalEval] Script terminated by signal.\n";
        return std::unexpected(EvalError::ScriptKilledBySignal);
    }
    return WEXITSTATUS(ret);
}

std::expected<float, EvalError> FunctionalEvaluator::evaluate(const Trajectory& traj,
                                                                const Environment& /*env*/,
                                                                const Task&        task) {
    if (task.eval_script.empty()) {
        std::cerr << "[FunctionalEval] eval_script bi trong -> khong the cham diem.\n";
        return std::unexpected(EvalError::EvalScriptEmpty);
    }

    std::string tmp_path = "/tmp/eval_" + traj.task_id + ".json";
    {
        std::ofstream ofs(tmp_path);
        if (!ofs) {
            std::cerr << "[FunctionalEval] Khong the tao file tam: " << tmp_path << "\n";
            return std::unexpected(EvalError::TempFileWriteFailed);   // loi ghi file khong phai agent fail
        }
        ofs << traj.exportToJson();
    }

    // C++23: runScript() gio tra std::expected<int, EvalError> — nhanh
    // "loi fork" va "bi signal kill" (truoc day cung don ve -1 roi ca 2
    // deu thanh nullopt) gio la 2 gia tri EvalError PHAN BIET duoc ngay,
    // khong can doc lai dong std::cerr o tren de biet ly do nao.
    auto exit_result = runScript(task.eval_script, tmp_path);
    std::remove(tmp_path.c_str());

    if (!exit_result) {
        return std::unexpected(exit_result.error());
    }

    float score = (*exit_result == 0) ? 1.0f : 0.0f;
    std::cout << "[FunctionalEval] exit code: " << *exit_result
              << "  \u2192  score: " << score << "\n";
    return score;
}