#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

// =========================================================
// 👑 第一步：优先加载 CommonLibF4 多版本库
// 注意：新库使用的是 .h 而不是旧库的 .hpp
// =========================================================
#include <RE/Fallout.h>
#include <F4SE/F4SE.h>
#include <REX/REX.h> // 替代了旧版的 spdlog 基础日志功能

// =========================================================
// 👑 第二步：立刻加载所有会冲突的 Windows/DX/ImGui 头文件
// 此时 `using namespace RE;` 还没有生效，它们可以干净、安全地解析
// =========================================================
#include <d3d11.h>
#include <dxgi1_2.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

// =========================================================
// 👑 第三步：最后才开放命名空间
// =========================================================
using namespace std::literals;

// 引入常用命名空间，方便写代码
using namespace REL;
using namespace REX;
using namespace F4SE;
using namespace RE; // 保留这个，因为你旧代码里应该大量使用了 RE::
