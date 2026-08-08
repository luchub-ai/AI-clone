#include "src/client/colab_client.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <utility>

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

ColabClient::ColabClient(std::string url, std::string model, float temp, int tokens)
    : base_url(std::move(url)), model_name(std::move(model)), temperature(temp), max_tokens(tokens) {}

std::string ColabClient::getModelName() const {
    return model_name;
}

void ColabClient::handleError(const std::string& error_type, const std::string& raw_buffer) {
    std::cerr << "[ColabClient Error - " << error_type << "]\n";
    if (!raw_buffer.empty()) {
        std::cerr << "Raw response received:\n" << raw_buffer << "\n";
    }
}

// [THAY ĐỔI THEO UML]: Sửa chữ ký hàm cho khớp prototype mới
LLMResponse ColabClient::chat(const std::vector<Message>& history) {
    LLMResponse response;
    response.model = model_name;

    auto request_start = std::chrono::steady_clock::now();
    auto setElapsedLatency = [&response, request_start]() {
        auto elapsed = std::chrono::steady_clock::now() - request_start;
        response.latency_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        );
    };

    CURL* curl = curl_easy_init();
    if (!curl) {
        handleError("CURL_INIT_FAILED", "");
        return response;
    }

    std::string readBuffer;
    std::string full_url = this->base_url + "/api/chat";

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    
    // [CẬP NHẬT 1 - BẮT BUỘC]: Nới rộng thời gian chờ lên 120 giây (2 phút)
    // Để tránh cURL bóp cổ luồng khi mô hình LLaVA trên Colab đang dịch ảnh Base64
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 200L); 

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // [CẬP NHẬT 2 - BẮT BUỘC]: Tiêm mật mã vượt tường lửa cảnh báo của Ngrok Free
    // Nếu thiếu dòng này, Ngrok sẽ trả về trang HTML cảnh báo làm văng lỗi MALFORMED_JSON
    headers = curl_slist_append(headers, "ngrok-skip-browser-warning: 69420");
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // ===================================================================
    // 1. DỰNG MẢNG "MESSAGES" TỪ HISTORY
    // ===================================================================
    json messages_array = json::array();

    for (const auto& msg : history) {
        json json_msg = {
            {"role", msg.role},
            {"content", msg.content}
        };
        
        if (!msg.images_base64.empty()) {
            json_msg["images"] = msg.images_base64;
        }
        messages_array.push_back(json_msg);
    }

    // ===================================================================
    // 2. NẠP BODY CHUẨN OLLAMA API
    // ===================================================================
    json request_body = {
        {"model", this->model_name},
        {"messages", messages_array},
        {"stream", false},
        
        // [CẬP NHẬT 3 - KHUYÊN DÙNG]: Ép Colab giữ mô hình trên VRAM vĩnh viễn
        // Tránh việc Google tự động dỡ trọng số LLM xuống khi bạn dừng code lâu
        {"keep_alive", -1}, 
        
        {"options", {
            {"temperature", this->temperature},
            {"num_predict", this->max_tokens} 
        }}
    };

    std::string json_str = request_body.dump();
    // std::cout << json_str << "\n\n";
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        std::string curl_err = curl_easy_strerror(res);
        handleError("NETWORK_ERROR (" + curl_err + ")", "");
        setElapsedLatency();
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        handleError("HTTP_STATUS_" + std::to_string(http_code), readBuffer);
        setElapsedLatency();
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // ===================================================================
    // 3. BẪY LỖI "MALFORMED JSON" & BÓC TÁCH KẾT QUẢ
    // ===================================================================
    setElapsedLatency();
    try {
        json response_json = json::parse(readBuffer);

        response.model = response_json.value("model", this->model_name);
        response.tokens_used = response_json.value("prompt_eval_count", 0)
                             + response_json.value("eval_count", 0);

        long long total_duration_ns = response_json.value("total_duration", 0LL);
        if (total_duration_ns > 0) {
            response.latency_ms = static_cast<int>(total_duration_ns / 1000000LL);
        }

        if (response_json.contains("message") && response_json["message"].contains("content")) {
            response.content = response_json["message"]["content"].get<std::string>();
            return response;
        } else {
            handleError("MISSING_MESSAGE_CONTENT_FIELD", readBuffer);
            return response;
        }
    } 
    catch (const json::parse_error& e) {
        handleError("MALFORMED_JSON_PARSING_FAILED", readBuffer);
        return response;
    }
}