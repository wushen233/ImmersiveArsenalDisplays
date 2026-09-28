#include "pch.h"
#include "UILocalization.h"
#include "UILogWindow.h"

namespace IAD::UI {

    void UILogWindow::Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        Buf.clear();
        LineOffsets.clear();
        LineOffsets.push_back(0);
    }

    void UILogWindow::AddLog(const char* fmt, ...) {
        int old_size = Buf.size();
        va_list args;
        va_start(args, fmt);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Buf.appendfv(fmt, args);
        }
        va_end(args);
        
        for (int new_size = Buf.size(); old_size < new_size; old_size++) {
            if (Buf[old_size] == '\n') {
                LineOffsets.push_back(old_size + 1);
            }
        }
    }

    void UILogWindow::Draw() {
        if (!m_show) return;

        ImGui::SetNextWindowSize(ImVec2(700, 450), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 0.95f)); // 给控制台一个深邃的极客黑
        
        if (!ImGui::Begin(TextLiteral("🖥️ 开发者控制台 (In-Game Log Console)"), &m_show)) {
            ImGui::PopStyleColor();
            ImGui::End();
            return;
        }

        if (ImGui::Button(TextLiteral("清空 (Clear)"))) Clear();
        ImGui::SameLine();
        bool copy = ImGui::Button(TextLiteral("复制全部 (Copy)"));
        ImGui::SameLine();
        ImGui::Checkbox(TextLiteral("自动滚动 (Auto-scroll)"), &AutoScroll);
        ImGui::SameLine();
        Filter.Draw(TextLiteral("搜索日志 (Filter)"), -150.0f);
        
        ImGui::Separator();
        
        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        if (copy) ImGui::LogToClipboard();

        std::lock_guard<std::mutex> lock(m_mutex);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
        
        // 渲染极客绿代码字体色
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f)); 

        const char* buf = Buf.begin();
        const char* buf_end = Buf.end();
        
        if (Filter.IsActive()) {
            for (int line_no = 0; line_no < LineOffsets.Size; line_no++) {
                const char* line_start = buf + LineOffsets[line_no];
                const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                if (Filter.PassFilter(line_start, line_end))
                    ImGui::TextUnformatted(line_start, line_end);
            }
        } else {
            // 使用极速 Clipper 裁剪技术，即使 10 万行日志也不会卡顿！
            ImGuiListClipper clipper;
            clipper.Begin(LineOffsets.Size);
            while (clipper.Step()) {
                for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++) {
                    const char* line_start = buf + LineOffsets[line_no];
                    const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                    ImGui::TextUnformatted(line_start, line_end);
                }
            }
            clipper.End();
        }
        
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        // 保持日志滚轮在最下方
        if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::End();
        ImGui::PopStyleColor();
    }
}