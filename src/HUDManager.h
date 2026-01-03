#pragma once

class HUDManager : public ISingleton<HUDManager>
{
public:
	// Initialization and Hooks
	void InstallHooks();
	void Reset(bool a_refreshUserPreference = false);

	// Widget Scanning and Discovery
	void ScanIfReady();
	void ForceScan();
	void RegisterNewMenu();

	// Input Handling
	void OnButtonDown();
	void OnButtonUp();

	// Core Logic Loop
	void Update(float a_delta);
	void UpdateDetectionLevel(float a_level);

	// Visibility State Helpers
	bool ShouldHideHUD();

private:
	// Alpha Application Logic
	void ApplyAlphaToHUD(float a_globalAlpha);
	void ApplyHUDMenuSpecifics(RE::GPtr<RE::GFxMovieView> a_movie, float a_globalAlpha);

	// Enchantment Bar Helper Functions
	bool IsEnchantmentElement(const char* a_elementId, bool& a_isLeft, bool& a_isRight, bool& a_isSkyHUD) const;
	float CalculateEnchantmentIgnoredAlpha(bool a_isEnchantLeft,
		bool a_isEnchantSkyHUD, bool a_menuOpen, float a_alphaL, float a_alphaR) const;
	void ApplySkyHUDSubMeter(RE::GFxValue& a_parent, const char* a_memberName,
		bool a_shouldBeVisible, bool a_callHammer);
	void ApplySkyHUDEnchantment(RE::GFxValue& a_elem, float a_alphaL, float a_alphaR,
		float a_managedAlpha, int a_mode, bool a_isIgnoredMode);
	double CalculateEnchantmentTargetAlpha(bool a_isEnchantLeft,
		bool a_isEnchantSkyHUD, int a_mode, float a_alphaL, float a_alphaR, double a_managedAlpha) const;

	// Child Visibility Enforcement
	void EnforceHMSMeterVisible(RE::GFxValue& a_parent, bool a_forcePermanent = false);
	void EnforceEnchantMeterVisible(RE::GFxValue& a_parent);

	// Internal Scanning Logic
	void ScanForWidgets(bool a_forceUpdate, bool a_deepScan, bool a_isRuntime = true);
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
	float _interiorAlpha = 0.0f;
	float _exteriorAlpha = 0.0f;
	float _combatAlpha = 0.0f;
	float _notInCombatAlpha = 0.0f;
	float _weaponAlpha = 0.0f;

	// Delta and Timer Tracking
	float _prevDelta = 0.0f;
	float _timer = 0.0f;
	float _scanTimer = 0.0f;
	float _displayTimer = 0.0f;

	// Stealth State Tracker
	float _lastDetectionLevel = 0.0f;
};
