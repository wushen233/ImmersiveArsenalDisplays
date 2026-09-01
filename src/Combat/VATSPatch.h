#pragma once

namespace IAD::Combat
{
	/// Scan Fallout4.exe for the hardcoded 8-index VATS body part array
	/// and replace RightFoot (index 24 = 0x18) with Weapon (index 17 = 0x11).
	///
	/// The known array: {0=Torso, 1=Head, 6=LArm, 8=RArm, 10=LLeg, 13=RLeg, 23=LFoot, 24=RFoot}
	/// After patch:     {0=Torso, 1=Head, 6=LArm, 8=RArm, 10=LLeg, 13=RLeg, 23=LFoot, 17=Weapon}
	///
	/// Call once at startup (kGameDataReady).
	/// Returns true if the pattern was found and patched.
	bool PatchVATSBodyPartIndices();
}
