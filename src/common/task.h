#pragma once
#include <string>

struct Task {
    std::string id;
    std::string instruction = "";
    std::string images_base64 = "";
    std::string eval_type = "";
    std::string eval_script = "";
    int max_steps;
};