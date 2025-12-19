#pragma once

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

	bool ShouldHideHUD();

private:
	void ApplyAlphaToHUD(float a_globalAlpha);
	void ApplyHUDMenuSpecifics(RE::GPtr<RE::GFxMovieView> a_movie, float a_globalAlpha, bool a_hideAll);
	void EnforceChildMeterVisible(RE::GFxValue& a_parent, const char* a_childName);

	// a_deepScan = true: Recursively visit _root (slow, thorough).
	// a_deepScan = false: Only check menuMap and WidgetContainer (fast).
	void ScanForWidgets(bool a_forceUpdate, bool a_deepScan);

	void ScanForContainers(RE::GFxMovieView* a_movie, int& a_foundCount, bool& a_changes);

	void DumpHUDStructure();

	bool _userWantsVisible = false;
	bool _installed = false;
	bool _hasScanned = false;
	bool _wasHidden = false;
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
