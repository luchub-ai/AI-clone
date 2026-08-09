#pragma once
#include <string>
#include <vector>
#include <optional>

// Interface truu tuong cho embedding provider - cung tinh than Strategy
// Pattern voi LLMClient (xem llm_client.h): muon doi provider (Ollama,
// OpenAI, Cohere...) sau nay chi can them 1 class con moi ke thua tu day,
// MemoryTool KHONG can sua gi vi no chi phu thuoc vao interface nay.
class EmbeddingClient {
public:
    virtual ~EmbeddingClient() = default;

    // Tra ve std::nullopt khi request that bai (loi mang, HTTP khac 200,
    // JSON response sai dinh dang mong doi...) THAY VI throw, de MemoryTool
    // co the fallback em ai (LIKE keyword / luu text khong embedding) thay
    // vi lam crash ca agent giua demo.
    virtual std::optional<std::vector<float>> embed(const std::string& text) = 0;
};
