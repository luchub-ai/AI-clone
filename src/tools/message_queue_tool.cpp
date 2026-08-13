#include "src/tools/message_queue_tool.h"

#include <chrono>
#include <sstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

MessageQueueTool::MessageQueueTool(std::shared_ptr<MessageQueue> mq, std::string agent_id)
    : mq_(std::move(mq)), agent_id_(std::move(agent_id)) {}

std::string MessageQueueTool::getName() const {
    return "message_queue";
}

std::string MessageQueueTool::getDescription() const {
    std::ostringstream oss;
    oss << "Cong cu giao tiep voi (cac) sub-agent khac dang chay song song. "
           "Dia chi cua CHINH ban trong he thong nay la \"" << agent_id_ << "\".\n"
           "Tham so dau vao (args) phai la JSON theo 1 trong 2 dang:\n"
           "{\"action\": \"send\", \"to\": \"<agent_id nguoi nhan>\", \"content\": \"noi dung can gui\"}\n"
           "{\"action\": \"receive\", \"timeout_ms\": 5000} "
           "(bo qua timeout_ms hoac dat <=0 nghia la cho VO HAN - CHI dung khi "
           "chac chan se co tin gui toi, neu khong se treo het buoc chay con lai).";
    return oss.str();
}

std::optional<std::string> MessageQueueTool::execute(const std::string& args) {
    // Khac MemoryTool/WebBrowserTool (luon co san 1 db_/session hop le sau
    // constructor), o day PHAI kiem tra mq_ vi shared_ptr rong la 1 loi
    // wiring THAT SU co the xay ra (vd quen truyen MessageQueue chung khi
    // dung tool nay ngoai ngu canh multi-agent) - tra ve chuoi loi ro rang
    // cho LLM thay vi de nullptr->send() crash ca task.
    if (!mq_) {
        return "Loi noi bo: MessageQueueTool chua duoc gan MessageQueue (wiring thieu).";
    }

    try {
        json j = json::parse(args);

        if (!j.contains("action") || !j["action"].is_string()) {
            return "Loi: thieu truong 'action' kieu string (\"send\" hoac \"receive\").";
        }
        std::string action = j["action"];

        if (action == "send") {
            if (!j.contains("to") || !j["to"].is_string() ||
                !j.contains("content") || !j["content"].is_string()) {
                return "Loi: action 'send' can 'to' (string) va 'content' (string).";
            }
            return handleSend(j["to"], j["content"]);
        }

        if (action == "receive") {
            int timeout_ms = 0; // mac dinh: cho vo han (xem canh bao trong getDescription())
            if (j.contains("timeout_ms")) {
                if (!j["timeout_ms"].is_number_integer()) {
                    return "Loi: 'timeout_ms' phai la so nguyen (don vi mili giay).";
                }
                timeout_ms = j["timeout_ms"];
            }
            return handleReceive(timeout_ms);
        }

        return "Loi: 'action' chi duoc la 'send' hoac 'receive'.";

    } catch (const json::exception& e) {
        // Bat rong json::exception (parse_error lan type_error) giong
        // dung tinh than MemoryTool::execute() - tranh 1 JSON sai dinh
        // dang lam throw thang len HarnessRunner, bien loi tam thoi (LLM
        // co the tu sua o buoc sau) thanh "agent_crashed".
        return std::string("Loi xu ly JSON: ") + e.what();
    }
}

std::string MessageQueueTool::handleSend(const std::string& to, const std::string& content) {
    AgentMessage msg;
    msg.sender_id = agent_id_;
    msg.recipient_id = to;
    msg.content = content;
    msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();

    mq_->send(std::move(msg));

    std::ostringstream oss;
    oss << "Da gui message toi '" << to << "'.";
    return oss.str();
}

std::string MessageQueueTool::handleReceive(int timeout_ms) {
    auto result = mq_->receive(agent_id_, timeout_ms);
    if (!result) {
        std::ostringstream oss;
        oss << "Khong co message nao sau " << timeout_ms << "ms cho.";
        return oss.str();
    }

    std::ostringstream oss;
    oss << "Nhan duoc message tu '" << result->sender_id << "': " << result->content;
    return oss.str();
}
