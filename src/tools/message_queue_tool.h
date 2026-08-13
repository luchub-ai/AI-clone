#pragma once

#include "src/common/message_queue.h"
#include "src/tools/tool.h"

#include <memory>
#include <optional>
#include <string>

// ════════════════════════════════════════════════════════════════
//  MessageQueueTool (de bai 10.3, +3d) — cong cu DUY NHAT trong toan
//  bo multi-agent coordination duoc phep cam vao 1 vat chia se THAT SU
//  (shared_ptr<MessageQueue>). Moi Tool khac cua tung sub-agent
//  (Exec/File/Memory/WebBrowser/...) PHAI la 1 ban RIENG cho tung
//  agent - xem giai thich + wiring cu the trong
//  benchmark/multi_agent_demo.cpp.
//
//  Nho dua giao tiep vao dung 1 Tool binh thuong (thay vi sua
//  AgentLoop/ReActAgentLoop de "biet" ve agent khac), HarnessRunner /
//  AgentLoop hoan toan KHONG can biet multi-agent coordination ton tai
//  - dung nguyen tac 4.4 cua de bai "Tool implementations KHONG phu
//  thuoc AgentLoop". LLM dieu khien viec gui/nhan qua CHINH co che
//  tool-call binh thuong no da dung cho calculator/file/memory/...,
//  khong can them khai niem moi nao trong ReAct loop.
//
//  Args dau vao la JSON theo 1 trong 2 dang (cung tinh than
//  action-dispatch nhu WebBrowserTool):
//    {"action": "send", "to": "<agent_id nguoi nhan>", "content": "..."}
//      -> gui 1 message, tra loi xac nhan NGAY (khong block).
//    {"action": "receive", "timeout_ms": 5000}
//      -> BLOCK toi da timeout_ms doi tin gui DEN dia chi cua CHINH
//         instance nay (agent_id_ duoc gan luc construct, KHONG phai do
//         LLM tu khai trong args - tranh agent "mao danh" doc tin cua
//         agent khac). Bo qua "timeout_ms" (hoac <= 0) nghia la CHO VO
//         HAN.
//
//  CANH BAO ve max_steps: goi "receive" voi cho vo han (hoac timeout qua
//  lon) se chiem het ngan sach so buoc (Task::max_steps) cua ReAct loop
//  neu ben gui khong bao gio gui toi - CHI dung timeout vo han khi that
//  su chac chan (vd theo dung kich ban demo, agent B luon cho sau khi
//  agent A chac chan se gui), con lai nen dung timeout_ms huu han de
//  agent con co the chuyen sang lam viec khac / bao cao "chua nhan duoc
//  gi" thay vi treo cung.
// ════════════════════════════════════════════════════════════════
class MessageQueueTool : public Tool {
public:
    // mq: SHARED giua nhieu sub-agent - TOAN BO instance MessageQueueTool
    //     cua cac sub-agent trong 1 lan chay multi-agent phai tro CHUNG 1
    //     shared_ptr nay (vi vay tham so la shared_ptr, KHONG phai
    //     unique_ptr nhu cac Tool khac).
    // agent_id: dinh danh CUA CHINH sub-agent so huu instance nay - vua
    //     la "dia chi nha" (recipient_id filter trong receive()) khi
    //     nguoi khac gui toi minh, vua la sender_id khi minh gui di. Moi
    //     sub-agent PHAI duoc gan 1 agent_id KHAC NHAU (khong tu kiem tra
    //     trung o day - trach nhiem cua noi wiring, xem demo).
    MessageQueueTool(std::shared_ptr<MessageQueue> mq, std::string agent_id);

    [[nodiscard]] std::string getName() const override;
    [[nodiscard]] std::string getDescription() const override;
    std::optional<std::string> execute(const std::string& args) override;

private:
    std::shared_ptr<MessageQueue> mq_;
    std::string agent_id_;

    // Da tach san "to"/"content"/"timeout_ms" thanh tham so kieu tho (khong
    // truyen thang nlohmann::json giua cac ham) - giu header nay khong
    // phai include <nlohmann/json.hpp>, dung tinh than web_browser_tool.h
    // (viec parse JSON gom het trong execute() cua .cpp).
    std::string handleSend(const std::string& to, const std::string& content);
    std::string handleReceive(int timeout_ms);
};
