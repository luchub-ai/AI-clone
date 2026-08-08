#pragma once
#include <string>
#include <vector>

struct LLMResponse {
    std::string content;
    int tokens_used = 0;
    int latency_ms = 0;
    std::string model;
};

// [THAY ĐỔI THEO UML]: Định nghĩa các thuộc tính cho Message
struct Message {
    std::string role;
    std::string content;
    std::vector<std::string> images_base64;
};

class LLMClient {
public:
    virtual ~LLMClient() = default;

    // [THAY ĐỔI THEO UML]: Sửa prototype chỉ nhận duy nhất 1 tham số history
    virtual LLMResponse chat(const std::vector<Message>& history) = 0;

    virtual std::string getModelName() const = 0;
};