#pragma once
#include "UIWindow.h"
#include <imgui.h>
#include <mutex>
#include <string>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

namespace IAD::UI {
    class UILogWindow : public UIWindow {
    public:
        // 单例模式，方便全局抓取日志
        static UILogWindow* GetSingleton() {
            static UILogWindow instance;
            return &instance;
        }

        void Draw() override;
        void AddLog(const char* fmt, ...) IM_FMTARGS(2);
        void Clear();

        bool m_show = false; // 控制台显隐开关

    private:
        UILogWindow() { Clear(); }

        ImGuiTextBuffer     Buf;
        ImGuiTextFilter     Filter;
        ImVector<int>       LineOffsets;
        bool                AutoScroll = true;
        std::mutex          m_mutex;
    };

    // 👇========== 🌟 核心黑科技：自定义 spdlog 拦截器 ==========👇
    template<typename Mutex>
    class ImGuiSink : public spdlog::sinks::base_sink<Mutex> {
    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override {
            spdlog::memory_buf_t formatted;
            spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);

            // 🌟 修复：直接使用原生内存指针转 std::string，彻底摆脱 fmt 依赖限制！
            std::string logStr(formatted.data(), formatted.size());
            UILogWindow::GetSingleton()->AddLog("%s", logStr.c_str());
        }
        void flush_() override {}
    };
}