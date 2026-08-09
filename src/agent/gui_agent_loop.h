#pragma once
#include "src/agent/agent_loop.h"   // TODO: doi duong dan neu khac trong repo (chua thay file nay)
#include "utils/encode_base64.h"    // ImageUtils::imageToBase64String

#include <optional>
#include <string>

// ════════════════════════════════════════════════════════════════
//  GUIAgentLoop
//  Ke thua TRUC TIEP AgentLoop (khong qua ReActAgentLoop). think()/act()
//  duoc viet lai o day, dua theo CUNG 1 dinh dang ReAct (Action:/Action
//  Input:/Final Answer:) ma ReActAgentLoop dang dung - de giu 1 chuan
//  chung cho moi loai AgentLoop, du GUI hay text-only.
//
//  LUU Y VE TRUNG LAP: vi khong con ke thua ReActAgentLoop, act() o day
//  la BAN COPY cua ReActAgentLoop::act() (cung 2 regex tool_regex/
//  done_regex). Neu sau nay sua dinh dang parse o 1 noi, nho sua dong bo
//  noi con lai - hoac cân nhac tach phan parse chung ra 1 ham rieng (vd
//  agent/react_format_parser.h) de 2 class cung goi, tranh drift.
//
//  observe() la noi khac biet CHINH: chup man hinh qua ToolRegistry, roi
//  dung ImageUtils::imageToBase64String() (tu utils/encode_base64.h,
//  file cua ban) de encode, nhet vao Message.images_base64.
//
// GIA DINH CAN KIEM TRA: constructor AgentLoop nhan dung 4 tham so nhu
// AgentLoop.h da cho thay (llm, tools, skills, loop_detector) - neu
// AgentLoop.h thuc te da doi, chi can sua constructor GUIAgentLoop.
// ════════════════════════════════════════════════════════════════
class GUIAgentLoop : public AgentLoop {
public:
    GUIAgentLoop(std::unique_ptr<LLMClient> llm_ptr,
                 std::shared_ptr<ToolRegistry> tools_ptr,
                 std::unique_ptr<SkillLoader> skills_ptr,
                 std::unique_ptr<LoopDetector> loop_det_ptr,
                 std::string screenshot_tool_name = "capture_screenshot");

protected:
    void        observe() override;
    std::string think()   override;
    Step        act()     override;
    std::string buildSystemPrompt(const Task& task) const override;

public:
    // AgentLoop::run() la NON-VIRTUAL (dung tinh than Template Method) va
    // KHONG goi observe() o turn 1 (xem comment trong agent_loop.cpp:
    // "khong goi observe cho step 1" - thiet ke cho ReAct thuan text, noi
    // task.instruction da dong vai tro observation dau tien). Vi toan bo
    // logic chup man hinh nam trong observe(), turn 1 cua GUIAgentLoop se
    // HOAN TOAN MU neu goi thang run(task) - model phai quyet dinh hanh
    // dong dau tien ma khong he thay man hinh.
    //
    // Khong the override run() de sua (non-virtual), nen wrapper nay chup
    // san 1 anh MAN HINH BAN DAU va gan vao task.images_base64 truoc khi
    // goi run() - dung dung co che run() da co san cho truong hop nay
    // (xem nhanh if(task.images_base64.empty())... trong agent_loop.cpp).
    // Dung ham nay THAY CHO run(task) khi chay GUIAgentLoop.
    // Trajectory run(Task task);

private:
    std::string screenshot_tool_name_;
};