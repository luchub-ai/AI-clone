#include "src/common/message_queue.h"

void MessageQueue::send(AgentMessage msg) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(msg));
    }
    // notify() NGOAI vung lock: tranh 1 thread vua duoc danh thuc phai
    // lap tuc block lai chi vi mutex_ luc do van con dang bi send() giu
    // (toi uu nho, khong bat buoc de dung nhung la thuc hanh chuan cho
    // condition_variable).
    cv_.notify_all();
}

bool MessageQueue::hasMessageLocked(const std::string& agent_id) const {
    // std::queue KHONG co iterator cong khai (chi expose front/back/push/
    // pop) nen phai copy tam de duyet ma khong pha vo thu tu that trong
    // queue_. Ham nay chi doc (khong duoc goi tu receive() - receive()
    // dung ky thuat rebuild rieng vi no can XOA phan tu tim thay, xem duoi).
    std::queue<AgentMessage> tmp = queue_;
    while (!tmp.empty()) {
        if (tmp.front().recipient_id == agent_id) return true;
        tmp.pop();
    }
    return false;
}

bool MessageQueue::hasMessage(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasMessageLocked(agent_id);
}

std::optional<AgentMessage> MessageQueue::receive(const std::string& agent_id, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Quet queue_ tim tin DAU TIEN (giu dung thu tu FIFO trong so cac tin
    // cung dia chi) danh cho agent_id; dung lai TOAN BO cac tin KHAC theo
    // DUNG thu tu cu vao lai queue_. Goi ben trong predicate cua
    // wait()/wait_for() ben duoi nen luon chay trong luc mutex_ dang bi
    // giu - an toan doi queue_ truc tiep.
    auto tryExtract = [&]() -> std::optional<AgentMessage> {
        std::queue<AgentMessage> remaining;
        std::optional<AgentMessage> found;
        while (!queue_.empty()) {
            AgentMessage m = std::move(queue_.front());
            queue_.pop();
            if (!found && m.recipient_id == agent_id) {
                found = std::move(m);
            } else {
                remaining.push(std::move(m));
            }
        }
        queue_ = std::move(remaining);
        return found;
    };

    if (timeout_ms <= 0) {
        // wait(lock, pred) tuong duong while(!pred()) wait(lock) - tu goi
        // pred() (tuc tryExtract()) NGAY LAP TUC truoc, nen neu tin da co
        // san thi tra ve luon khong block. Sau moi lan notify_all() (that
        // hoac spurious wakeup), lock duoc tu dong lay lai truoc khi
        // pred() chay tiep - dung dan de doi queue_ ben trong tryExtract.
        std::optional<AgentMessage> result;
        cv_.wait(lock, [&] {
            result = tryExtract();
            return result.has_value();
        });
        return result;
    }

    // wait_for(lock, dur, pred): lap lai pred() moi lan thuc day toi khi
    // pred() true HOAC het rel_time, tra ve gia tri pred() lan cuoi. Ket
    // qua that su (co tin hay khong) da duoc `result` (capture ben ngoai)
    // giu lai tu ben trong predicate, khong can doc gia tri bool tra ve
    // cua wait_for.
    std::optional<AgentMessage> result;
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
        result = tryExtract();
        return result.has_value();
    });
    return result; // nullopt <=> het gio ma predicate van chua tung true
}
