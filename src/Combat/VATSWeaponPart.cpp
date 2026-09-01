#include "pch.h"
#include "VATSWeaponPart.h"

#include <RE/T/TESRace.h>
#include <RE/T/TESDataHandler.h>
#include <RE/B/BGSBodyPartData.h>
#include <RE/B/BGSBodyPart.h>
#include <RE/B/BGSBodyPartDefs.h>
#include <RE/A/Actor.h>
#include <RE/A/AIProcess.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/N/NiNode.h>
#include <RE/A/ActorValue.h>

#include <unordered_map>
#include <mutex>

namespace IAD::Combat
{
	// ---- InjectWeaponBodyParts ----

	void InjectWeaponBodyParts()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) return;

		auto& races = dh->GetFormArray<RE::TESRace>();
		int injectedWpn = 0, skippedWpn = 0;
		int injectedRA2 = 0, skippedRA2 = 0;
		int fixedToHit = 0;

		for (auto* race : races) {
			if (!race || !race->bodyPartData)
				continue;

			auto* bpData = race->bodyPartData;

			// Force-set toHitChance on slot 5 (Head2 populated by CK ESP)
			// CK may fail to save this uint8 field correctly
			if (bpData->partArray[5]) {
				auto& d = bpData->partArray[5]->data;
				static bool s_loggedPartData = false;
				if (!s_loggedPartData) {
					REX::INFO("[IAD VATS Diag] partArray[5] BEFORE fix: dm={:.2f} flags=0x{:X} type=0x{:X} hp={} toHit={} av={}",
						d.damageMult, d.flags, d.type, d.healthPercent,
						d.toHitChance, static_cast<void*>(d.actorValue));
					s_loggedPartData = true;
				}
				// Force-set ALL fields (CK may set wrong values)
				d.damageMult = 1.0f;
				d.flags = 0x09;   // Severable + Explodable
				d.type = 0x08;    // RightArm1 (was Head2 0x05, might cause 0% hit)
				d.toHitChance = 100;
				d.healthPercent = 50;
				auto* av = RE::ActorValue::GetSingleton();
				if (av && av->rightAttackCondition)
					d.actorValue = av->rightAttackCondition;
				fixedToHit++;
			}

			// ---- Weapon (slot 17) ----
			if (bpData->partArray[17] == nullptr) {
				auto* raw = new std::byte[sizeof(RE::BGSBodyPart)]();
				auto* part = reinterpret_cast<RE::BGSBodyPart*>(raw);
				part->nodeName = "Weapon";
				part->targetName = "Weapon";
				part->partName = "Weapon";
				part->data.damageMult = 1.0f;
				part->data.flags = 0x01;
				part->data.type = 0x11;
				part->data.healthPercent = 100;
				part->data.toHitChance = 30;
				bpData->partArray[17] = part;
				injectedWpn++;
			} else {
				skippedWpn++;
			}

			// ---- RightArm2 (slot 9) ----
			// Needed because Fallout 4 VATS only shows body parts that exist
			// in BGSBodyPartData. Human race has RightArm1 (upper arm) but not
			// RightArm2 (forearm). We inject it so VATS can enumerate it,
			// then redirect damageRootNode[9] to Weapon in OverrideWeaponBodyPart.
			if (bpData->partArray[9] == nullptr) {
				auto* raw = new std::byte[sizeof(RE::BGSBodyPart)]();
				auto* part = reinterpret_cast<RE::BGSBodyPart*>(raw);
				part->nodeName = "RArm_ForeArm1";
				part->targetName = "RArm_ForeArm1";
				part->partName = "Forearm R";
				part->data.damageMult = 1.0f;
				part->data.flags = 0x01;
				part->data.type = 0x09;
				part->data.healthPercent = 100;
				part->data.toHitChance = 35;
				bpData->partArray[9] = part;
				injectedRA2++;
			} else {
				skippedRA2++;
			}
		}

		REX::INFO("[IAD VATS] Body part injection: Weapon {} new/{} exist, RightArm2 {} new/{} exist, toHit fixed on {} races, {} total races",
			injectedWpn, skippedWpn, injectedRA2, skippedRA2, fixedToHit,
			static_cast<int>(races.size()));
	}

	// ---- OverrideWeaponBodyPart (Plan B) ----

	namespace
	{
		// Keep track of the original damageRootNode[9] so we can restore it
		// if the actor loses their IAD models.
		std::unordered_map<RE::TESFormID, RE::NiNode*> g_originalRightArmNode;
		std::mutex g_overrideMutex;

		// One-time diagnostic: log what node the skeleton's "Weapon" name resolves to
		static bool s_diagDone = false;
	}

	void OverrideWeaponBodyPart(RE::Actor* a_actor, bool a_hasIAD)
	{
		if (!a_actor)
			return;

		auto* proc = a_actor->currentProcess;
		if (!proc || !proc->middleHigh)
			return;

		auto* root3D = a_actor->Get3D(false);
		if (!root3D)
			return;

		RE::TESFormID actorID = a_actor->GetFormID();
		std::lock_guard<std::mutex> lock(g_overrideMutex);

		constexpr int kOverrideSlot = 8;  // RightArm1 (standard ESM part, guaranteed VATS visible)

		if (a_hasIAD) {
			// Find the Weapon skeleton node in the actor's 3D tree
			RE::BSFixedString weaponName("Weapon");
			auto* weaponObj = root3D->GetObjectByName(weaponName);
			auto* weaponNiNode = weaponObj ? weaponObj->IsNode() : nullptr;

			// One-time diagnostic
			if (!s_diagDone) {
				const char* desc = weaponObj ? weaponObj->name.c_str() : "NULL";
				REX::INFO("[IAD VATS PlanB] Searching for 'Weapon' in skeleton...");
				REX::INFO("[IAD VATS PlanB]   GetObjectByName('Weapon') = {}", desc);
				if (weaponObj) {
					REX::INFO("[IAD VATS PlanB]   IsNode() = {}",
						weaponNiNode ? "yes" : "no");
				}
				// Also dump current damageRootNode entries
				REX::INFO("[IAD VATS PlanB] damageRootNode dump for actor 0x{:X}:", actorID);
				for (int i = 0; i < 26; i++) {
					auto* n = proc->middleHigh->damageRootNode[i];
					if (n) {
						REX::INFO("[IAD VATS PlanB]   [{}] '{}' (orig)", i, n->name.c_str());
					}
				}
				s_diagDone = true;
			}

			if (!weaponNiNode)
				return;

			// Save original if not already saved
			auto*& origSlot = g_originalRightArmNode[actorID];
			if (!origSlot) {
				origSlot = proc->middleHigh->damageRootNode[kOverrideSlot];
			}

			// Override RightArm2 -> Weapon
			auto*& slot = proc->middleHigh->damageRootNode[kOverrideSlot];
			if (slot != weaponNiNode) {
				REX::INFO("[IAD VATS PlanB] Actor 0x{:X}: damageRootNode[{}] '{}' -> 'Weapon'",
					actorID, kOverrideSlot,
					slot ? slot->name.c_str() : "NULL");
				slot = weaponNiNode;
			}
		} else {
			// No IAD models → restore original RightArm2
			auto it = g_originalRightArmNode.find(actorID);
			if (it != g_originalRightArmNode.end() && it->second) {
				auto*& slot = proc->middleHigh->damageRootNode[kOverrideSlot];
				if (slot != it->second) {
					REX::INFO("[IAD VATS PlanB] Actor 0x{:X}: restoring damageRootNode[{}] to '{}'",
						actorID, kOverrideSlot, it->second->name.c_str());
					slot = it->second;
				}
				g_originalRightArmNode.erase(it);
			}
		}
	}
}
