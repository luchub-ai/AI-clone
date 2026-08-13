#pragma once
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

// ════════════════════════════════════════════════════════════════
//  AgentMessage — 1 don vi giao tiep giua cac sub-agent qua MessageQueue.
//  Dat rieng o day (khong nhet vao common/task.h hay step.h) vi day la
//  khai niem cua TANG DIEU PHOI MULTI-AGENT (10.3), khong lien quan gi
//  toi Task/Step cua 1 agent don le.
// ════════════════════════════════════════════════════════════════
struct AgentMessage {
    std::string sender_id;
    std::string recipient_id;
    std::string content;
    // Mili giay tu 1 moc steady_clock noi bo - CHI de sap thu tu/log luc
    // debug, KHONG dung de dong bo logic (co che dong bo THAT SU o day la
    // mutex_ + cv_ trong MessageQueue, khong phai timestamp).
    long long timestamp_ms = 0;
};

// ════════════════════════════════════════════════════════════════
//  MessageQueue (de bai 10.3) — dung 1 thu THAT SU can va AN TOAN de
//  share giua cac sub-agent chay tren nhieu thread khac nhau.
//
//  Vi sao CHI mot minh cai nay duoc share, con ToolRegistry/Environment
//  (va cac Tool statful nhu MemoryTool/WebBrowserTool ben trong)
//  KHONG duoc share: xem giai thich chi tiet dau file
//  benchmark/multi_agent_demo.cpp. Tom tat: moi truy cap vao state dung
//  chung o day (queue_) DEU di qua dung 2 ham public send()/receive(),
//  va CA HAI deu lock mutex_ truoc khi dung vao queue_ - khong co
//  duong nao khac cham vao state do ma bo qua mutex. MemoryTool
//  (sqlite3* goi thang C API) va Environment (thao tac filesystem/
//  process truc tiep qua nhieu ham public khac nhau) khong co tinh
//  chat "moi duong deu qua 1 khoa" nay.
//
//  THIET KE: dung DUNG 1 std::queue<AgentMessage> + 1 std::mutex nhu de
//  bai goi y, KHONG tach thanh map<agent_id, queue<...>> (moi agent 1
//  hang doi rieng) de giu dung tinh than "1 hang doi dung chung". He qua:
//  receive() khong the chi pop() phan tu dau (co the khong phai tin nhan
//  cua MINH) - phai quet tim tin DAU TIEN dung recipient_id, roi dung lai
//  cac tin con lai theo DUNG thu tu FIFO cu (xem receive() trong .cpp).
//  Voi so luong message o quy mo 1 lan demo/exam (vai - vai chuc tin,
//  khong phai hang trieu), chi phi O(n) moi lan receive() khong dang ke.
// ════════════════════════════════════════════════════════════════
class MessageQueue {
public:
    // Day 1 message vao cuoi queue_ roi danh thuc TAT CA thread dang cho
    // trong receive() - PHAI notify_all() (khong the notify_one()) vi
    // khong biet truoc thread nao (agent nao) dang cho tin nay.
    void send(AgentMessage msg);

    // Cho toi khi co message danh cho agent_id hoac het timeout_ms.
    //   timeout_ms >  0 : cho toi da timeout_ms mili giay roi tra
    //                     std::nullopt neu van chua co tin.
    //   timeout_ms <= 0 : cho VO HAN - CHI nen dung khi agent biet chac
    //                     se co tin toi (vd da dong bo qua 1 buoc khac),
    //                     vi 1 lan receive() block mai se an het ngan
    //                     sach max_steps cua ReAct loop truoc khi kip
    //                     lam viec khac (xem canh bao trong
    //                     MessageQueueTool::getDescription()).
    // Dung condition_variable::wait_for voi predicate ben trong - KHONG
    // busy-wait/poll CPU trong luc cho.
    std::optional<AgentMessage> receive(const std::string& agent_id, int timeout_ms = 0);

    // Kiem tra khong block - agent co the goi truoc de quyet dinh nen
    // receive() (co the block) hay lam viec khac trong luc chua co tin.
    [[nodiscard]] bool hasMessage(const std::string& agent_id) const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<AgentMessage> queue_;

    // Quet queue_ (PHAI duoc goi trong luc da giu mutex_ - ham nay tu no
    // KHONG tu lock) tim tin dau tien danh cho agent_id. Dung chung boi
    // hasMessage() (chi doc, khong doi queue_) va receive() (co doi
    // queue_, xem .cpp).
    [[nodiscard]] bool hasMessageLocked(const std::string& agent_id) const;
};
