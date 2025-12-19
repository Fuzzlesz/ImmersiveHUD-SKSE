#pragma once

#define SMOOTHCAM_API_COMMONLIB

#include "API/BTPS_API_decl.h"
#include "API/SmoothCamAPI.h"
#include "API/TrueDirectionalMovementAPI.h"

class HUDManager : public ISingleton<HUDManager>
{
public:
	void InstallHooks();
	void Reset();

	void ScanIfReady();
	void ForceScan();
	void RegisterNewMenu();

	void OnButtonDown();
	void OnButtonUp();

	void Update(float a_delta);
	void UpdateContextualStealth(float a_detectionLevel, RE::GFxValue a_sneakAnim);

	bool IsSystemMenu(const std::string& a_menuName);
	void InitIFPV();

	SmoothCamAPI::IVSmoothCam3* g_SmoothCam = nullptr;
	TDM_API::IVTDM2* g_TDM = nullptr;
	BTPS_API_decl::API_V0* g_BTPS = nullptr;
	HMODULE g_DetectionMeter = nullptr;
	RE::TESGlobal* g_IFPV = nullptr;

private:
	bool ShouldHideHUD();

	void ApplyAlphaToHUD(float a_globalAlpha);
	void ApplyHUDMenuSpecifics(RE::GPtr<RE::GFxMovieView> a_movie, float a_globalAlpha, bool a_hideAll);
	void EnforceChildMeterVisible(RE::GFxValue& a_parent, const char* a_childName);

	// a_deepScan = true: Recursively visit _root (slow, thorough).
	// a_deepScan = false: Only check menuMap and WidgetContainer (fast).
	void ScanForWidgets(bool a_forceUpdate, bool a_deepScan);

	void ScanForContainers(RE::GFxMovieView* a_movie, int& a_foundCount, bool& a_changes);
	void ScanArrayContainer(const std::string& a_path, const RE::GFxValue& a_container, int& a_foundCount, bool& a_changes);

	std::string GetMenuURL(RE::GPtr<RE::GFxMovieView> a_movie);
	void DumpHUDStructure();

	bool CompatibilityCheck_TDM();
	bool CompatibilityCheck_SmoothCam();
	bool CompatibilityCheck_DetectionMeter();
	bool CompatibilityCheck_BTPS();
	bool CompatibilityCheck_IFPV() const;
	bool IsFakeFirstPerson() const;

	bool ValidPickType();
	bool ValidCastType(RE::ActorMagicCaster* a_magicCaster);
	bool ValidAttackType(RE::PlayerCharacter* a_player);
	bool ValidSpellType(RE::MagicItem* a_magicItem);

	bool _userWantsVisible = false;
	bool _installed = false;
	bool _hasScanned = false;
	std::atomic_bool _isScanPending = false;

	float _currentAlpha = 0.0f;
	float _targetAlpha = 0.0f;
	float _ctxAlpha = 0.0f;
	float _ctxSneakAlpha = 0.0f;

	float _prevDelta = 0.0f;
	float _timer = 0.0f;
	float _scanTimer = 0.0f;
	float _loadGracePeriod = 0.0f;

	RE::GFxValue _cachedSneakAnim;
	bool _hasCachedSneakAnim = false;
};
