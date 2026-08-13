// ════════════════════════════════════════════════════════════════
//  tests/message_queue.cpp
//
//  Test THAT (khong mock) cho src/common/message_queue.h - phan ha
//  tang duy nhat duoc CHIA SE giua cac sub-agent trong de bai 10.3
//  Multi-agent Coordination. Dung std::thread THAT (khong chi goi
//  send()/receive() tuan tu tren 1 thread) de kiem tra dung dan tinh
//  chat "khoa dung, khong busy-wait, dung nguoi nhan" - day la loai
//  loi CHI xuat hien khi thuc su co >= 2 thread chay dong thoi, khong
//  the phat hien bang cach doc code hay chay tren 1 thread.
//
//  KHONG dung framework test nao (Catch2/GoogleTest) - main() tran +
//  in ket qua tung dong, dung quy uoc cac file da co san trong
//  tests/ (vd tests/memory_tool.cpp).
//
//  BIEN DICH THU CONG (repo nay hien KHONG co CMake target rieng cho
//  thu muc tests/, giong quy uoc cac file test khac). Chay tu THU MUC
//  GOC cua repo (can -I. tro dung ve goc de cac #include "src/..." o
//  tren giai quyet duoc):
//
//    g++ -std=c++23 -pthread tests/message_queue.cpp src/common/message_queue.cpp -I. -o /tmp/test_message_queue
//    /tmp/test_message_queue
//
//  NEN chay THEM 1 lan voi ThreadSanitizer de bat cac data race co the
//  lot qua kiem tra thu cong (ket qua cuoi cung dung khong co nghia la
//  KHONG co race - assert chi thay TRIEU CHUNG, TSan quan sat truc
//  tiep chinh hanh vi truy cap bo nho cua tung thread):
//
//    g++ -std=c++23 -pthread -fsanitize=thread -g tests/message_queue.cpp src/common/message_queue.cpp -I. -o /tmp/test_message_queue_tsan
//    /tmp/test_message_queue_tsan
// ════════════════════════════════════════════════════════════════

#include "src/common/message_queue.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const std::string& label) {
    if (cond) {
        std::cout << "[OK]   " << label << "\n";
    } else {
        std::cout << "[FAIL] " << label << "\n";
        ++g_failures;
    }
}

// ── Test 1: send/receive co ban tren CUNG 1 thread ─────────────────
// Sanity check truoc khi dung toi threading that - neu cai nay da sai
// thi loi nam o logic co ban (vd nham recipient_id), khong lien quan
// gi den dong bo thread ca.
void testBasicSendReceiveSameThread() {
    MessageQueue mq;

    check(!mq.hasMessage("agent_b"), "hang doi rong luc dau, hasMessage tra false");

    AgentMessage msg;
    msg.sender_id = "agent_a";
    msg.recipient_id = "agent_b";
    msg.content = "xin chao";
    mq.send(msg);

    check(mq.hasMessage("agent_b"), "hasMessage tra true sau khi send dung recipient");
    check(!mq.hasMessage("agent_c"), "hasMessage tra false cho recipient KHONG duoc gui toi");

    auto received = mq.receive("agent_b", /*timeout_ms=*/100);
    check(received.has_value(), "receive() lay duoc message vua gui");
    if (received) {
        check(received->sender_id == "agent_a", "sender_id giu nguyen");
        check(received->content == "xin chao", "content giu nguyen");
    }

    check(!mq.hasMessage("agent_b"), "message da bi lay ra khoi hang doi, khong con nua");
}

// ── Test 2: receive() dung TIMEOUT khi khong co message ─────────────
void testReceiveTimesOut() {
    MessageQueue mq;

    auto start = std::chrono::steady_clock::now();
    auto received = mq.receive("khong_ai_gui_ca", /*timeout_ms=*/200);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();

    check(!received.has_value(), "receive() tra nullopt khi het gio, khong co message");
    // Cho phep sai so nho (may cham/lich CPU) nhung PHAI it nhat ~180ms
    // (khong duoc tra ve gan nhu ngay lap tuc - neu vay nghia la
    // wait_for dang khong thuc su cho, co the loi logic predicate).
    check(elapsed_ms >= 180,
          "thuc su CHO gan du 200ms (khong tra ve ngay), do duoc " + std::to_string(elapsed_ms) + "ms");
    // Va cung KHONG duoc cho qua lau so voi timeout yeu cau (vd bi
    // treo vinh vien do predicate khong bao gio dung).
    check(elapsed_ms < 2000, "khong cho qua lau so voi timeout_ms yeu cau");
}

// ── Test 3: 2 thread THAT - 1 thread receive() BLOCK truoc, thread kia
//    send() SAU - phai thuc su DANH THUC duoc thread dang cho, khong
//    phai polling may man trung thoi diem ────────────────────────────
void testBlockingReceiveWakesUpOnSend() {
    MessageQueue mq;
    std::atomic<bool> receiver_started{false};
    std::optional<AgentMessage> received;

    std::thread receiver([&] {
        receiver_started = true;
        // Cho VO HAN (timeout_ms=0) - CHI hop ly trong test nay vi ta
        // BIET chac se co send() toi tu thread kia; ngoai doi that
        // Tool nen dung timeout huu han (xem message_queue_tool.h).
        received = mq.receive("agent_x", /*timeout_ms=*/0);
    });

    // Doi receiver THAT SU vao trong receive()/wait() truoc khi
    // send() - test nay co chu dich kiem tra dung duong "dang block
    // roi moi duoc danh thuc", khac voi duong "tin da co san khi goi
    // receive()" ma testBasicSendReceiveSameThread da kiem tra roi.
    while (!receiver_started.load()) { std::this_thread::yield(); }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    AgentMessage msg;
    msg.sender_id = "agent_y";
    msg.recipient_id = "agent_x";
    msg.content = "danh thuc nao";
    mq.send(msg);

    receiver.join();

    check(received.has_value(), "thread dang block trong receive() duoc danh thuc dung luc co send()");
    if (received) {
        check(received->content == "danh thuc nao", "noi dung nhan duoc dung nhu da gui");
    }
}

// ── Test 4: dung recipient - 2 "hop thu" khac nhau khong lam lan noi
//    dung cua nhau, va THU TU FIFO trong 1 hop thu duoc giu nguyen ───
void testRecipientFilteringAndFifoOrder() {
    MessageQueue mq;

    auto make = [](std::string from, std::string to, std::string content) {
        AgentMessage m;
        m.sender_id = std::move(from);
        m.recipient_id = std::move(to);
        m.content = std::move(content);
        return m;
    };

    // Gui XEN KE cho agent_x va agent_y - neu receive() vo tinh lam
    // xao tron thu tu hoac lay nham tin cua nguoi khac se lo ngay o day.
    mq.send(make("s", "agent_x", "x1"));
    mq.send(make("s", "agent_y", "y1"));
    mq.send(make("s", "agent_x", "x2"));
    mq.send(make("s", "agent_y", "y2"));
    mq.send(make("s", "agent_x", "x3"));

    auto x1 = mq.receive("agent_x", 100);
    auto x2 = mq.receive("agent_x", 100);
    auto x3 = mq.receive("agent_x", 100);
    check(x1 && x1->content == "x1", "agent_x nhan dung tin dau tien cua minh (x1)");
    check(x2 && x2->content == "x2",
          "agent_x nhan dung tin thu 2 cua minh (x2), khong bi xao tron boi tin cua agent_y xen giua");
    check(x3 && x3->content == "x3", "agent_x nhan dung tin thu 3 cua minh (x3)");

    auto y1 = mq.receive("agent_y", 100);
    auto y2 = mq.receive("agent_y", 100);
    check(y1 && y1->content == "y1",
          "agent_y van nhan dung tin dau tien cua minh (y1) - khong mat du agent_x da lay het tin cua no truoc");
    check(y2 && y2->content == "y2", "agent_y nhan dung tin thu 2 cua minh (y2)");

    check(!mq.receive("agent_x", 50).has_value(), "agent_x het tin, receive() tra nullopt sau timeout ngan");
    check(!mq.receive("agent_y", 50).has_value(), "agent_y het tin, receive() tra nullopt sau timeout ngan");
}

// ── Test 5: NHIEU thread cung send() dong thoi vao CUNG 1
//    MessageQueue - kiem tra khong crash, khong mat message, khong
//    nhan trung lap. Day la kiem tra gan nhat voi tinh huong THAT
//    trong benchmark/multi_agent_demo.cpp (nhieu agent/thread cung
//    dung chung 1 MessageQueue) ma test nay co the mo phong don gian. ──
void testConcurrentSendersNoLostOrDuplicateMessages() {
    MessageQueue mq;
    constexpr int kSenders = 8;
    constexpr int kMessagesPerSender = 50;

    std::vector<std::thread> senders;
    for (int s = 0; s < kSenders; ++s) {
        senders.emplace_back([&mq, s] {
            for (int i = 0; i < kMessagesPerSender; ++i) {
                AgentMessage m;
                m.sender_id = "sender_" + std::to_string(s);
                m.recipient_id = "collector";
                m.content = std::to_string(s) + "_" + std::to_string(i);
                mq.send(std::move(m));
            }
        });
    }
    for (auto& t : senders) t.join();

    // Tat ca sender da join() (tuc da send() xong het) truoc khi bat
    // dau receive() o day nen vong lap duoi khong can chay song song -
    // van kiem tra dung dieu can kiem tra: TONG SO message nhan duoc
    // phai KHOP CHINH XAC voi tong so da gui, khong thieu khong du
    // (thieu = mat message do loi dong bo trong send(); du/trung = loi
    // logic doc trung 1 message 2 lan trong receive()).
    int received_count = 0;
    while (mq.receive("collector", /*timeout_ms=*/50).has_value()) {
        ++received_count;
    }

    check(received_count == kSenders * kMessagesPerSender,
          "nhan du " + std::to_string(kSenders * kMessagesPerSender) + " message tu " +
              std::to_string(kSenders) + " thread gui dong thoi, khong mat khong trung (nhan duoc " +
              std::to_string(received_count) + ")");
}

} // namespace

int main() {
    std::cout << "==== Test MessageQueue (src/common/message_queue.h) ====\n\n";

    testBasicSendReceiveSameThread();
    std::cout << "\n";
    testReceiveTimesOut();
    std::cout << "\n";
    testBlockingReceiveWakesUpOnSend();
    std::cout << "\n";
    testRecipientFilteringAndFifoOrder();
    std::cout << "\n";
    testConcurrentSendersNoLostOrDuplicateMessages();

    std::cout << "\n==========================================\n";
    if (g_failures == 0) {
        std::cout << "TAT CA TEST PASS.\n";
    } else {
        std::cout << g_failures << " TEST THAT BAI.\n";
    }
    return g_failures == 0 ? 0 : 1;
}
