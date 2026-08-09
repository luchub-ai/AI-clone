#include "ollama_client.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <utility>
#include "utils/resolve_imagebase64.h"

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

OllamaClient::OllamaClient(std::string url, std::string model, float temp, int tokens)
    : base_url(std::move(url)), model_name(std::move(model)), temperature(temp), max_tokens(tokens) {}

std::string OllamaClient::getModelName() const {
    return model_name;
}

void OllamaClient::handleError(const std::string& error_type, const std::string& raw_buffer) {
    std::cerr << "[OllamaClient Error - " << error_type << "]\n";
    if (!raw_buffer.empty()) {
        std::cerr << "Raw response received:\n" << raw_buffer << "\n";
    }
}

// [THAY ĐỔI THEO UML]: Sửa chữ ký hàm cho khớp prototype mới
LLMResponse OllamaClient::chat(const std::vector<Message>& history) {
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

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 200L); 


    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // ===================================================================
    // 1. DỰNG MẢNG "MESSAGES" TỪ HISTORY (THAY ĐỔI ĐỂ KHỚP VỚI PROTOTYPE)
    // ===================================================================
    json messages_array = json::array();

    for (const auto& msg : history) {
        json json_msg = {
            {"role", msg.role},
            {"content", msg.content}
        };
        
        // Cải tiến: Lọc và chuyển đổi từng phần tử trong mảng ảnh
        if (!msg.images_base64.empty()) {   
            json valid_images = json::array();

            for (const auto& img_item : msg.images_base64) {
                // Cho chuỗi đi qua máy quét
                auto resolved_b64 = resolveImageToBase64(img_item);
                
                // Nếu lấy được Base64 hợp lệ thì mới nhét vào JSON
                if (resolved_b64.has_value() && !resolved_b64->empty()) {
                    valid_images.push_back(*resolved_b64);
                }
            }

            // CHỈ KHI có ít nhất 1 ảnh hợp lệ thì mới thêm trường "images" vào payload
            // Nếu toàn bộ ảnh bị lỗi/file không tồn tại -> Bỏ trường này để LLM không bị crash
            if (!valid_images.empty()) {
                json_msg["images"] = valid_images;
            } else {
                std::cout << "[Sanitizer] Toàn bộ ảnh của message bị lỗi. Đã hủy trường 'images'.\n";
            }
        }

        messages_array.push_back(json_msg);
    }

    // ===================================================================
    // 2. NẠP BODY CHUẨN OLLAMA API (GIỮ NGUYÊN HOÀN TOÀN LOGIC CŨ)
    // ===================================================================
    json request_body = {
        {"model", this->model_name},
        {"messages", messages_array},
        {"stream", false},
        {"options", {
            {"temperature", this->temperature},
            {"num_predict", this->max_tokens}, // 2048
            {"num_ctx",this->max_tokens * 4} // default 8192
        }}
    };

    std::string json_str = request_body.dump();
    // // === debug == 
    std::string json_str_test = request_body.dump(4);
    std::cout << "===== debug llmclient =====\n " << json_str_test << std::endl;
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
    // 3. BẪY LỖI "MALFORMED JSON" & BÓC TÁCH KẾT QUẢ (GIỮ NGUYÊN HOÀN TOÀN)
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
