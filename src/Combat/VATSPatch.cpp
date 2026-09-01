#include "pch.h"
#include "VATSPatch.h"

#include <REL/Utility.h>
#include <REX/W32/KERNEL32.h>

namespace IAD::Combat
{
	bool PatchVATSBodyPartIndices()
	{
		// The 8 body part indices that VATS enumerates (discovered from
		// damageRootNode dump: [0]COM [1]Neck [6]LArm_UpperArm [8]RArm_UpperArm
		// [10]LLeg_Thigh [13]RLeg_Thigh [23]LLeg_Foot [24]RLeg_Foot)
		static constexpr std::uint32_t kSearchIndices[] = {
			0,   // kTorso
			1,   // kHead1
			6,   // kLeftArm1
			8,   // kRightArm1
			10,  // kLeftLeg1
			13,  // kRightLeg1
			23,  // kLeftFoot
			24,  // kRightFoot
		};

		static_assert(sizeof(kSearchIndices) == 32, "Pattern must be 32 bytes");

		auto base = reinterpret_cast<std::uintptr_t>(
			REX::W32::GetModuleHandleA("Fallout4.exe"));
		if (!base) {
			REX::WARN("[IAD VATS Patch] GetModuleHandleA(Fallout4.exe) failed");
			return false;
		}

		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		auto  size = nt->OptionalHeader.SizeOfImage;

		const auto* patBytes = reinterpret_cast<const std::uint8_t*>(kSearchIndices);

		for (std::uintptr_t i = base; i < base + size - sizeof(kSearchIndices); i++) {
			if (std::memcmp(reinterpret_cast<void*>(i), patBytes, sizeof(kSearchIndices)) == 0) {
				auto* indices = reinterpret_cast<std::uint32_t*>(i);

				// Replace RightFoot (24) with Weapon (17)
				constexpr std::uint32_t kNewIndex = 9;  // RightArm2 (matches CK ESP body part type)
				if (!REL::WriteSafeData(&indices[7], kNewIndex)) {
					REX::WARN("[IAD VATS Patch] Found array at 0x{:X} but WriteSafe failed (memory protected?)", i);
					return false;
				}

				REX::INFO("[IAD VATS Patch] SUCCESS: patched VATS index 7: 24(RFoot) -> 9(RightArm2) at 0x{:X}", i);
				return true;
			}
		}

		REX::WARN("[IAD VATS Patch] Pattern NOT FOUND in module (base=0x{:X}, size=0x{:X}). VATS weapon part will not show.",
			base, size);
		return false;
	}
}
