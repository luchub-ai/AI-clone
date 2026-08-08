#pragma once
#include <vector>
#include "src/common/step.h"

#include <format>       // C++20: Định dạng chuỗi
#include <print>        // C++23: std::println
#include <ranges>       // C++20: std::views
#include <algorithm>    // C++20: std::ranges::distance

class LoopDetector {
private:
    int warning_threshold;
    int critical_threshold;

public:
    explicit LoopDetector(int warn_thresh = 2, int crit_thresh = 3);

    [[nodiscard]] bool detectLoop(const std::vector<Step>& history) const;
    [[nodiscard]] bool isPingPongLoop(const std::vector<Step>& history) const;
};