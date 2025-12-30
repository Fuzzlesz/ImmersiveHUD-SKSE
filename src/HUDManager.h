#pragma once

class HUDManager : public ISingleton<HUDManager>
{
public:
	// Initialization and Hooks
	void InstallHooks();
	void Reset();

	// Widget Scanning and Discovery
	void ScanIfReady();
	void ForceScan();
	void RegisterNewMenu();

	// Input Handling
	void OnButtonDown();
	void OnButtonUp();

	// Core Logic Loop
	void Update(float a_delta);
	void UpdateContextualStealth(float a_detectionLevel, RE::GFxValue a_sneakAnim);

	// Visibility State Helpers
	bool ShouldHideHUD();

private:
	// Alpha Application Logic
	void ApplyAlphaToHUD(float a_globalAlpha);
	void ApplyHUDMenuSpecifics(RE::GPtr<RE::GFxMovieView> a_movie, float a_globalAlpha);

	// Child Visibility Enforcement
	void EnforceHMSMeterVisible(RE::GFxValue& a_parent, bool a_forcePermanent);
	void EnforceEnchantMeterVisible(RE::GFxValue& a_parent);

	// Internal Scanning Logic
	void ScanForWidgets(bool a_forceUpdate, bool a_deepScan);
	void ScanForContainers(RE::GFxMovieView* a_movie, int& a_foundCount, bool& a_changes);

	// Debugging
	void DumpHUDStructure();

	// State Flags
	bool _userWantsVisible = false;
	bool _installed = false;
	bool _hasScanned = false;
	bool _wasHidden = false;
	std::atomic_bool _isScanPending = false;

	// Alpha Transition Values
	float _currentAlpha = 0.0f;
	float _targetAlpha = 0.0f;
	float _ctxAlpha = 0.0f;
	float _ctxSneakAlpha = 0.0f;
	float _enchantAlphaL = 0.0f;
	float _enchantAlphaR = 0.0f;

	// Delta and Timer Tracking
	float _prevDelta = 0.0f;
	float _timer = 0.0f;
	float _scanTimer = 0.0f;
	float _loadGracePeriod = 0.0f;

	// Stealth State Cache
	RE::GFxValue _cachedSneakAnim;
	bool _hasCachedSneakAnim = false;
};
