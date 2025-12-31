#pragma once

#define SMOOTHCAM_API_COMMONLIB
#include "API/BTPS_API_decl.h"
#include "API/SmoothCamAPI.h"
#include "API/TrueDirectionalMovementAPI.h"

class Compat : public ISingleton<Compat>
{
public:
	// Initialization
	void InitExternalData();
	void ManageSmoothCamControl(bool a_shouldBlock);

	// Compatibility Checks
	bool IsTDMActive();
	bool IsSmoothCamActive();
	bool IsDetectionMeterInstalled();
	bool IsBTPSActive();
	bool IsIFPVActive();
	bool IsFakeFirstPerson();

	// External Control Logic
	bool IsCompassAllowed();
	bool IsSneakAllowed();

	// State Context Helpers
	bool HasEnchantedWeapon(bool a_leftHand);
	bool IsEnchantmentFull(bool a_leftHand);
	bool IsPlayerWeaponDrawn();

	// Player State Logic
	bool CameraStateCheck();
	bool IsCrosshairTargetValid();
	bool IsPlayerCasting(RE::PlayerCharacter* a_player);
	bool IsPlayerAttacking(RE::PlayerCharacter* a_player);
	bool IsSpellContextual(RE::MagicItem* a_magicItem);

	// API Handles
	SmoothCamAPI::IVSmoothCam3* g_SmoothCam = nullptr;
	TDM_API::IVTDM2* g_TDM = nullptr;
	BTPS_API_decl::API_V0* g_BTPS = nullptr;
	HMODULE g_DetectionMeter = nullptr;
	RE::TESGlobal* g_IFPV = nullptr;

	// Generic HUD Control Globals (from ImmersiveHUD.esp)
	RE::TESGlobal* g_DisableCompass = nullptr;  // 0xEEE
	RE::TESGlobal* g_DisableSneak = nullptr;    // 0xFFF

private:
	bool _hasSmoothCamCrosshairControl = false;
	bool _hasSmoothCamStealthControl = false;
};
