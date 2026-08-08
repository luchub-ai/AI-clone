#pragma once
#include <string>
#include <vector>
#include "src/common/step.h" // Tham chiếu đến class Step nằm cùng thư mục common
#include "src/common/task.h" // include vì có những thư mục khác sẽ có thể dùng task.h trong trajectory luôn

class Trajectory {
public:
    std::string task_id;
    std::string model;
    bool success = false;
    int total_tokens = 0;
    int total_time_ms = 0;
    
    // Mối quan hệ "Chứa" (Composes) với Step
    std::vector<Step> steps;

    // Hàm xuất dữ liệu ra file JSON
    [[nodiscard]] std::string exportToJson() const;
};
