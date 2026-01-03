#include "HUDManager.h"
#include "Compat.h"
#include "Events.h"
#include "HUDElements.h"
#include "MCMGen.h"
#include "Settings.h"
#include "Utils.h"
#include <numbers>

// ==========================================
// Internal Helpers
// ==========================================

namespace
{
	// Aggressively forces any DisplayObject found to be visible and at 100 alpha.
	// Used to fix vanilla enchantment charge meter visibility issues, required for unlabeled children.
	class VisibilityHammer : public RE::GFxValue::ObjectVisitor
	{
	public:
		VisibilityHammer(bool a_forceVisible = false, int a_depth = 1) :
			_forceVisible(a_forceVisible),
			_depth(a_depth)
		{}

		void Visit(const char* a_name, const RE::GFxValue& a_val) override
		{
			if (a_val.IsDisplayObject()) {
				if (_forceVisible) {
					std::string lowerName = a_name ? a_name : "unnamed";
					std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

					RE::GFxValue::DisplayInfo d;
					auto& obj = const_cast<RE::GFxValue&>(a_val);
					obj.GetDisplayInfo(&d);

					bool changed = false;
					if (!d.GetVisible()) {
						d.SetVisible(true);
						changed = true;
					}

					// Protect penalty bars: low-health/survival blinking uses these names.
					bool isAnimated = (lowerName.find("flash") != std::string::npos ||
									   lowerName.find("blink") != std::string::npos ||
									   lowerName.find("penalty") != std::string::npos);

					// Force 100 alpha to skip vanilla fade-ins while ScaleX handles draining.
					if (!isAnimated && d.GetAlpha() < 100.0) {
						d.SetAlpha(100.0);
						changed = true;
					}
					if (changed) {
						obj.SetDisplayInfo(d);
					}

					// Recurse to handle nested clips (e.g. ChargeMeter_mc).
					if (_depth > 0) {
						VisibilityHammer subHammer(_forceVisible, _depth - 1);
						obj.VisitMembers(&subHammer);
					}
				}
			}
		}

	private:
		bool _forceVisible;
		int _depth;
	};
}

// ==========================================
// Hooks
// ==========================================

struct PlayerUpdateHook
{
	static void thunk(RE::PlayerCharacter* a_this, float a_delta)
	{
		func(a_this, a_delta);
		HUDManager::GetSingleton()->Update(a_delta);
	}
	static inline REL::Relocation<decltype(thunk)> func;
	static constexpr std::size_t size = 0xAD;
};

struct StealthMeterHook
{
	static char thunk(RE::StealthMeter* a_this, int64_t a2, int64_t a3, int64_t a4)
	{
		auto result = func(a_this, a2, a3, a4);
		// Minimal task: store the detection level for the main update loop to poll
		HUDManager::GetSingleton()->UpdateDetectionLevel(static_cast<float>(a_this->unk88));
		return result;
	}
	static inline REL::Relocation<decltype(thunk)> func;
	static constexpr std::size_t size = 0x1;
};

// ==========================================
// HUDManager Implementation
// ==========================================

void HUDManager::InstallHooks()
{
	Events::InputEventSink::Register();
	Events::MenuOpenCloseEventSink::Register();

	stl::write_vfunc<RE::PlayerCharacter, PlayerUpdateHook>();
	stl::write_vfunc<RE::StealthMeter, StealthMeterHook>();

	// Load settings first so we can read the preference
	Settings::GetSingleton()->Load();
	_userWantsVisible = Settings::GetSingleton()->IsStartVisible();

	_installed = true;

	// Initial state load and snap (Hard Reset)
	Reset(true);

	logger::info("HUDManager hooks installed. StartVisible: {}", _userWantsVisible);
}

void HUDManager::Reset(bool a_refreshUserPreference)
{
	auto settings = Settings::GetSingleton();
	settings->Load();

	// Overwrite the toggle state on startup or save load
	if (a_refreshUserPreference) {
		_userWantsVisible = settings->IsStartVisible();
	}

	_wasHidden = true;
	_currentAlpha = 0.0f;
	_targetAlpha = 0.0f;
	_ctxAlpha = 0.0f;
	_ctxSneakAlpha = 0.0f;
	_enchantAlphaL = 0.0f;
	_enchantAlphaR = 0.0f;
	_interiorAlpha = 0.0f;
	_exteriorAlpha = 0.0f;
	_combatAlpha = 0.0f;
	_notInCombatAlpha = 0.0f;
	_weaponAlpha = 0.0f;
	_lockedOnAlpha = 0.0f;
	_timer = 0.0f;
	_scanTimer = 0.0f;
	_displayTimer = 0.0f;
	_lastDetectionLevel = 0.0f;

	// Call Update with 0 delta to calculate state and snap UI immediately.
	// This eliminates delay/flicker when coming out of load screens or menus.
	Update(0.0f);
}

void HUDManager::ScanIfReady()
{
	if (_hasScanned || _isScanPending) {
		return;
	}
	auto* ui = RE::UI::GetSingleton();
	if (ui && ui->GetMenu("HUD Menu")) {
		_isScanPending = true;

		// Session initial scan: capture runtime status then flip scanned flag
		bool isRuntime = _hasScanned;
		_hasScanned = true;

		SKSE::GetTaskInterface()->AddUITask([this, isRuntime]() {
			ScanForWidgets(false, true, isRuntime);
			_isScanPending = false;
		});
	}
}

void HUDManager::RegisterNewMenu()
{
	if (_isScanPending) {
		return;
	}
	_isScanPending = true;
	SKSE::GetTaskInterface()->AddUITask([this]() {
		ScanForWidgets(false, true, true);
		_isScanPending = false;
	});
}

void HUDManager::ForceScan()
{
	ScanForWidgets(true, true, true);
}

void HUDManager::OnButtonDown()
{
	const auto settings = Settings::GetSingleton();

	if (settings->IsDumpHUDEnabled()) {
		SKSE::GetTaskInterface()->AddUITask([this]() {
			DumpHUDStructure();
		});
	}

	if (settings->IsHoldMode()) {
		_userWantsVisible = true;
	} else {
		_userWantsVisible = !_userWantsVisible;

		// If turning ON and a duration is set, start the countdown
		if (_userWantsVisible && settings->GetDisplayDuration() > 0.0f) {
			_displayTimer = settings->GetDisplayDuration();
		} else {
			// Turning OFF manually kills any active timer
			_displayTimer = 0.0f;
		}
	}
}

void HUDManager::OnButtonUp()
{
	if (Settings::GetSingleton()->IsHoldMode()) {
		_userWantsVisible = false;
		_displayTimer = 0.0f;
	}
}

void HUDManager::UpdateDetectionLevel(float a_level)
{
	_lastDetectionLevel = a_level;
}

// ==========================================
// Update Loop
// ==========================================

void HUDManager::Update(float a_delta)
{
	if (!_installed) {
		return;
	}
	const auto player = RE::PlayerCharacter::GetSingleton();
	const auto compat = Compat::GetSingleton();
	if (!player) {
		return;
	}

	const auto settings = Settings::GetSingleton();

	// Timed display logic: decrement timer and toggle visibility off when expired
	if (_displayTimer > 0.0f && !settings->IsHoldMode()) {
		_displayTimer -= a_delta;
		if (_displayTimer <= 0.0f) {
			_displayTimer = 0.0f;
			_userWantsVisible = false;
		}
	}

	// Periodic scan
	if (a_delta > 0.0f) {
		_scanTimer += a_delta;
		if (_scanTimer > 2.0f) {
			_scanTimer = 0.0f;
			if (_hasScanned) {
				SKSE::GetTaskInterface()->AddUITask([this]() {
					ScanForWidgets(false, true, true);
				});
			}
		}
	}

	if (!_hasScanned) {
		auto* ui = RE::UI::GetSingleton();
		if (ui && ui->GetMenu("HUD Menu")) {
			ScanIfReady();
		} else {
			return;
		}
	}

	const bool shouldHide = ShouldHideHUD();

	// Cache immediate responsive states
	const bool isInterior = player->GetParentCell() ? player->GetParentCell()->IsInteriorCell() : false;
	const bool isInCombat = player->IsInCombat();
	const bool isWeaponDrawn = compat->IsPlayerWeaponDrawn();
	const bool isSneaking = player->IsSneaking();
	const bool isLockedOn = compat->IsTDMActive();
	const bool isSmoothCam = compat->IsSmoothCamActive();
	const bool isTDM = compat->IsTDMActive();
	const bool isBTPS = compat->IsBTPSActive();

	// 1. Determine Visibility Targets
	bool shouldBeVisible = _userWantsVisible;
	if (!shouldBeVisible) {
		if ((settings->IsAlwaysShowInCombat() && isInCombat) ||
			(settings->IsAlwaysShowWeaponDrawn() && isWeaponDrawn)) {
			shouldBeVisible = true;
		}
	}
	_targetAlpha = shouldBeVisible ? 100.0f : 0.0f;

	// Per-element state targets for fancy linear fading
	float targetInterior = isInterior ? 100.0f : 0.0f;
	float targetExterior = !isInterior ? 100.0f : 0.0f;
	float targetCombat = isInCombat ? 100.0f : 0.0f;
	float targetNotInCombat = !isInCombat ? 100.0f : 0.0f;
	float targetWeapon = isWeaponDrawn ? 100.0f : 0.0f;
	float targetLockedOn = isLockedOn ? 100.0f : 0.0f;

	// Crosshair Target Alpha
	float targetCtx = 0.0f;
	if (settings->GetCrosshairSettings().enabled) {
		// Contextual: Active combat state (Ranged / Magic)
		bool isActionActive = compat->IsPlayerCasting(player) || compat->IsPlayerAttacking(player);

		// Contextual: Interaction state (valid pick target)
		bool isLookActive = compat->IsCrosshairTargetValid() && !isBTPS;

		// Visibility Authority: Merge contextual states with global settings
		bool shouldDrawCrosshair = (isActionActive || isLookActive || (_targetAlpha > 0.1f));

		// SmoothCam API: Request control (block) to hide, release (unblock) to draw
		if (isSmoothCam && !shouldHide) {
			compat->ManageSmoothCamControl(!shouldDrawCrosshair);
		}

		// Alpha Calculation
		if (shouldDrawCrosshair) {
			if (isTDM) {
				// TDM handles rendering; vanilla is suppressed
				targetCtx = 0.0f;
			} else {
				// Target 100% for active use/SmoothCam, 50% for passive interaction
				if (isActionActive || isSmoothCam) {
					targetCtx = 100.0f;
				} else {
					targetCtx = 50.0f;
				}
			}
		} else {
			targetCtx = 0.0f;
		}
	} else {
		targetCtx = _targetAlpha;
	}

	// Sneak Meter Target Alpha
	float targetSneak = 0.0f;
	float sneakFadeSpeed = settings->GetFadeSpeed();
	if (isSneaking && compat->IsSneakAllowed()) {
		if (settings->GetSneakMeterSettings().enabled) {
			// Contextual Authority: detection level math mixed with global toggle state
			float detectionAlpha = 0.0f;

			// Contextual: Target 85% opacity based on detection level (Unified 1st/3rd person)
			if (compat->IsDetectionMeterInstalled()) {
				detectionAlpha = 0.0f;
			} else {
				detectionAlpha = _lastDetectionLevel * 0.85f;
			}

			targetSneak = std::max(detectionAlpha, _currentAlpha);
			// ActionScript scaling (Detection modes only).
			targetSneak = (targetSneak * 0.01f * 90.0f);
		} else {
			// Manual Authority: follow linear state synchronization trackers
			int widgetMode = settings->GetWidgetMode("_root.HUDMovieBaseInstance.StealthMeterInstance");
			switch (widgetMode) {
			case Settings::kVisible:
				targetSneak = 100.0f;
				break;
			case Settings::kInterior:
				targetSneak = targetInterior;
				break;
			case Settings::kExterior:
				targetSneak = targetExterior;
				break;
			case Settings::kInCombat:
				targetSneak = targetCombat;
				break;
			case Settings::kNotInCombat:
				targetSneak = targetNotInCombat;
				break;
			case Settings::kWeaponDrawn:
				targetSneak = targetWeapon;
				break;
			case Settings::kHidden:
				targetSneak = 0.0f;
				break;
			case Settings::kIgnored:
				targetSneak = 100.0f;
				break;
			default:
				targetSneak = _currentAlpha;
				break;  // kImmersive
			}
		}
	} else {
		// Player stood up or globally disabled: target hard 0
		targetSneak = 0.0f;
		sneakFadeSpeed = 16.0f;  // Fast-fade exit
	}

	// Enchantment Target Logic
	const bool weaponsActive = isWeaponDrawn;
	const bool leftEnch = compat->HasEnchantedWeapon(true);
	const bool rightEnch = compat->HasEnchantedWeapon(false);

	float targetEnL = (weaponsActive && leftEnch) ? 100.0f : 0.0f;
	float targetEnR = (weaponsActive && rightEnch) ? 100.0f : 0.0f;

	// Dual-wield handshaking: forces synchronous fading regardless of equipment delay.
	if (weaponsActive && leftEnch && rightEnch) {
		float highest = std::max(_enchantAlphaL, _enchantAlphaR);
		_enchantAlphaL = highest;
		_enchantAlphaR = highest;
	}

	// 2. Handle Hidden State & Transitions
	if (shouldHide && a_delta > 0.0f) {
		_wasHidden = true;
		compat->ManageSmoothCamControl(true);
		SKSE::GetTaskInterface()->AddUITask([this]() { ApplyAlphaToHUD(0.0f); });
		return;
	}

	// Snap to target instantly when coming out of a menu or loading to match vanilla behaviour
	if (_wasHidden) {
		_currentAlpha = _targetAlpha;
		_enchantAlphaL = targetEnL;
		_enchantAlphaR = targetEnR;
		_interiorAlpha = targetInterior;
		_exteriorAlpha = targetExterior;
		_combatAlpha = targetCombat;
		_notInCombatAlpha = targetNotInCombat;
		_weaponAlpha = targetWeapon;
		_lockedOnAlpha = targetLockedOn;
		_ctxAlpha = targetCtx;
		_ctxSneakAlpha = targetSneak;
		_wasHidden = false;
	}

	// 3. Mixed Math Calculations (Skip if a_delta is 0)
	if (a_delta > 0.0f) {
		const float fadeSpeed = settings->GetFadeSpeed();
		const float change = fadeSpeed * (a_delta * 60.0f);

		// Helper lambda for consistent linear transitions
		auto UpdateLinear = [&](float& a_currentAlpha, float a_targetAlpha) {
			if (std::abs(a_currentAlpha - a_targetAlpha) <= change) {
				a_currentAlpha = a_targetAlpha;
			} else if (a_currentAlpha < a_targetAlpha) {
				a_currentAlpha += change;
			} else {
				a_currentAlpha -= change;
			}
		};

		// Global HUD & State Conditions: Linear Math (Vanilla Feel)
		UpdateLinear(_currentAlpha, _targetAlpha);
		UpdateLinear(_enchantAlphaL, targetEnL);
		UpdateLinear(_enchantAlphaR, targetEnR);
		UpdateLinear(_interiorAlpha, targetInterior);
		UpdateLinear(_exteriorAlpha, targetExterior);
		UpdateLinear(_combatAlpha, targetCombat);
		UpdateLinear(_notInCombatAlpha, targetNotInCombat);
		UpdateLinear(_weaponAlpha, targetWeapon);
		UpdateLinear(_lockedOnAlpha, targetLockedOn);

		// Crosshair: Lerp Math (Smooth Feel)
		_ctxAlpha = std::lerp(_ctxAlpha, targetCtx, a_delta * fadeSpeed);
		if (std::abs(_ctxAlpha - targetCtx) < 0.1f) {
			_ctxAlpha = targetCtx;
		}

		// Stealth Meter: Mixed Math depending on mode
		if (settings->GetSneakMeterSettings().enabled || !isSneaking) {
			_ctxSneakAlpha = std::lerp(_ctxSneakAlpha, targetSneak, a_delta * sneakFadeSpeed);
			if (std::abs(_ctxSneakAlpha - targetSneak) < 0.1f) {
				_ctxSneakAlpha = targetSneak;
			}
		} else {
			UpdateLinear(_ctxSneakAlpha, targetSneak);
		}

		_prevDelta = a_delta;
		_timer += _prevDelta;
	}

	SKSE::GetTaskInterface()->AddUITask([this, alpha = _currentAlpha]() {
		ApplyAlphaToHUD(alpha);
	});
}

// ==========================================
// Menu & Widget Management
// ==========================================

bool HUDManager::ShouldHideHUD()
{
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		return false;
	}

	// 1. Camera State Check (VATS, FreeCam, Auto-Vanity)
	if (Compat::GetSingleton()->CameraStateCheck()) {
		return true;
	}

	// 2. System UI Checks
	if (ui->IsApplicationMenuOpen() || ui->IsItemMenuOpen()) {
		return true;
	}

	for (const auto& [name, entry] : ui->menuMap) {
		if (entry.menu && entry.menu->OnStack() && Utils::IsSystemMenu(name.c_str())) {
			return true;
		}
	}
	return false;
}

// Depth 0 is critical: protects Survival penalties and resource blinking from freezing.
void HUDManager::EnforceHMSMeterVisible(RE::GFxValue& a_parent, bool a_forcePermanent)
{
	if (a_parent.IsObject()) {
		VisibilityHammer hammer(a_forcePermanent, 0);
		a_parent.VisitMembers(&hammer);
	}
}

// Depth 1 is critical: reaches ChargeMeter_mc to kill vanilla auto-hide logic.
void HUDManager::EnforceEnchantMeterVisible(RE::GFxValue& a_parent)
{
	if (a_parent.IsObject()) {
		VisibilityHammer hammer(true, 1);
		a_parent.VisitMembers(&hammer);
	}
}

// ==========================================
// Enchantment Bar Helpers
// ==========================================

bool HUDManager::IsEnchantmentElement(const char* a_elementId, bool& a_isLeft, bool& a_isRight, bool& a_isSkyHUD) const
{
	a_isLeft = (strcmp(a_elementId, "iMode_EnchantLeft") == 0);
	a_isRight = (strcmp(a_elementId, "iMode_EnchantRight") == 0);
	a_isSkyHUD = (strcmp(a_elementId, "iMode_EnchantCombined") == 0);

	return a_isLeft || a_isRight || a_isSkyHUD;
}

// kIgnored block: simulates vanilla hide-when-full while fixing the reappear bug.
float HUDManager::CalculateEnchantmentIgnoredAlpha(bool a_isEnchantLeft,
	bool a_isEnchantSkyHUD, bool a_menuOpen, float a_alphaL, float a_alphaR) const
{
	const auto compat = Compat::GetSingleton();

	if (a_isEnchantSkyHUD) {
		return std::max(a_alphaL, a_alphaR);
	} else {
		bool full = a_isEnchantLeft ? compat->IsEnchantmentFull(true) : compat->IsEnchantmentFull(false);
		float tracked = a_isEnchantLeft ? a_alphaL : a_alphaR;
		return (a_menuOpen || full) ? 0.0f : tracked;
	}
}

// Helper that handles visibility logic for a single SkyHUD sub-meter
void HUDManager::ApplySkyHUDSubMeter(RE::GFxValue& a_parent, const char* a_memberName,
	bool a_shouldBeVisible, bool a_callHammer)
{
	RE::GFxValue sub;
	if (a_parent.GetMember(a_memberName, &sub)) {
		RE::GFxValue::DisplayInfo s;
		sub.GetDisplayInfo(&s);
		s.SetVisible(a_shouldBeVisible);
		s.SetAlpha(a_shouldBeVisible ? 100.0 : 0.0);
		sub.SetDisplayInfo(s);
		if (a_shouldBeVisible && a_callHammer) {
			EnforceEnchantMeterVisible(sub);
		}
	}
}

// Unified method that handles both IgnoredMode and Hammer cases
void HUDManager::ApplySkyHUDEnchantment(RE::GFxValue& a_elem, float a_alphaL, float a_alphaR,
	float a_managedAlpha, int a_mode, bool a_isIgnoredMode)
{
	const auto compat = Compat::GetSingleton();

	bool lVal, rVal;

	if (a_isIgnoredMode) {
		// IgnoredMode: check fullness state
		bool lFull = compat->IsEnchantmentFull(true);
		bool rFull = compat->IsEnchantmentFull(false);
		lVal = (a_alphaL > 0.01f) && !lFull;
		rVal = (a_alphaR > 0.01f) && !rFull;
	} else {
		// Hammer mode: check weapon drawn state and mode
		bool drawn = compat->IsPlayerWeaponDrawn();
		lVal = drawn && compat->HasEnchantedWeapon(true);
		rVal = drawn && compat->HasEnchantedWeapon(false);

		if (a_mode == Settings::kImmersive && a_managedAlpha < 0.1f) {
			lVal = false;
			rVal = false;
		}
	}

	// Apply to all three sub-meters using helper
	ApplySkyHUDSubMeter(a_elem, "ChargeMeterFrameAlt", lVal || rVal, true);
	ApplySkyHUDSubMeter(a_elem, "ChargeMeterLeftAlt", lVal, true);
	ApplySkyHUDSubMeter(a_elem, "ChargeMeterRightAlt", rVal, true);
}

double HUDManager::CalculateEnchantmentTargetAlpha(bool a_isEnchantLeft,
	bool a_isEnchantSkyHUD, int a_mode, float a_alphaL, float a_alphaR, double a_managedAlpha) const
{
	float tracked = a_isEnchantSkyHUD ? std::max(a_alphaL, a_alphaR) : (a_isEnchantLeft ? a_alphaL : a_alphaR);

	if (a_mode == Settings::kVisible) {
		return tracked;
	} else {
		return std::min(tracked, static_cast<float>(a_managedAlpha));
	}
}

void HUDManager::ScanForContainers(RE::GFxMovieView* a_movie, int& a_foundCount, bool& a_changes)
{
	if (!a_movie) {
		return;
	}
	RE::GFxValue root;
	if (!a_movie->GetVariable(&root, "_root")) {
		return;
	}

	Utils::ContainerDiscoveryVisitor visitor(a_foundCount, a_changes, "_root");
	root.VisitMembers(&visitor);
}

void HUDManager::ScanForWidgets(bool a_forceUpdate, bool a_deepScan, bool a_isRuntime)
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}

	bool changes = false;
	int externalCount = 0;
	int containerCount = 0;
	auto* settings = Settings::GetSingleton();

	auto hud = ui->GetMenu("HUD Menu");
	RE::GFxMovieView* hudMovie = (hud && hud->uiMovie) ? hud->uiMovie.get() : nullptr;

	// Detect SkyHUD presence before iterating elements
	_isSkyHUDActive = false;
	if (hudMovie) {
		RE::GFxValue test;
		if (hudMovie->GetVariable(&test, "_root.HUDMovieBaseInstance.ChargeMeterBaseAlt")) {
			_isSkyHUDActive = true;
		}
	}

	if (hudMovie) {
		for (const auto& def : HUDElements::Get()) {
			for (const auto& path : def.paths) {
				RE::GFxValue test;
				if (hudMovie->GetVariable(&test, path)) {
					if (settings->AddDiscoveredPath(path, "Internal/Vanilla")) {
						changes = true;
					}
				}
			}
		}
	}

	// Scan External Menus
	for (auto& [name, entry] : ui->menuMap) {
		if (!entry.menu || !entry.menu->uiMovie) {
			continue;
		}
		std::string menuName(name.c_str());
		if (menuName == "HUD Menu" || Utils::IsSystemMenu(menuName)) {
			continue;
		}
		if (entry.menu->menuFlags.any(RE::IMenu::Flag::kApplicationMenu)) {
			continue;
		}

		std::string url = Utils::GetMenuURL(entry.menu->uiMovie);
		if (settings->AddDiscoveredPath(menuName, url)) {
			changes = true;
			externalCount++;
			logger::info("Discovered External Menu: {} [Source: {}]", menuName, url);
		}
	}

	// Scan Widget Containers
	if (hudMovie) {
		if (a_deepScan) {
			ScanForContainers(hudMovie, containerCount, changes);
		} else {
			RE::GFxValue root;
			if (hudMovie->GetVariable(&root, "_root")) {
				RE::GFxValue widgetContainer;
				if (root.GetMember("WidgetContainer", &widgetContainer)) {
					Utils::ScanArrayContainer("_root.WidgetContainer", widgetContainer, containerCount, changes);
				}
			}
		}
	}

	if (changes || a_forceUpdate) {
		Settings::GetSingleton()->Load();

		// Use a_isRuntime to determine Init vs Runtime state for MCMGen status
		MCMGen::Update(a_isRuntime);

		if (changes) {
			logger::info("Scan complete (Deep={}). Found {} external, {} internal.", a_deepScan, externalCount, containerCount);
		}
	}
}

void HUDManager::DumpHUDStructure()
{
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}

	logger::info("=== DUMPING MENUS ===");
	for (auto& [name, entry] : ui->menuMap) {
		if (entry.menu && entry.menu->uiMovie) {
			logger::info("[Menu] {} [Source: {}]", name.c_str(), Utils::GetMenuURL(entry.menu->uiMovie));
		}
	}

	auto hud = ui->GetMenu("HUD Menu");
	if (hud && hud->uiMovie) {
		RE::GFxValue root;
		if (hud->uiMovie->GetVariable(&root, "_root")) {
			logger::info("=== DUMPING HUD ROOT ===");
			Utils::DebugVisitor visitor("_root", 3);
			root.VisitMembers(&visitor);
		}
	}
}

void HUDManager::ApplyHUDMenuSpecifics(RE::GPtr<RE::GFxMovieView> a_movie, float a_globalAlpha)
{
	const auto settings = Settings::GetSingleton();
	const auto compat = Compat::GetSingleton();
	const auto player = RE::PlayerCharacter::GetSingleton();
	const bool menuOpen = ShouldHideHUD();

	// Management of vanilla elements; target 0 alpha while menus are open to respect engine hiding.
	const float managedAlpha = menuOpen ? 0.0f : a_globalAlpha;
	const float alphaL = menuOpen ? 0.0f : _enchantAlphaL;
	const float alphaR = menuOpen ? 0.0f : _enchantAlphaR;
	const float interiorAlpha = menuOpen ? 0.0f : _interiorAlpha;
	const float exteriorAlpha = menuOpen ? 0.0f : _exteriorAlpha;
	const float combatAlpha = menuOpen ? 0.0f : _combatAlpha;
	const float notInCombatAlpha = menuOpen ? 0.0f : _notInCombatAlpha;
	const float weaponAlpha = menuOpen ? 0.0f : _weaponAlpha;
	const float lockedOnAlpha = menuOpen ? 0.0f : _lockedOnAlpha;

	// Immediate state checks for Visibility Hammer logic
	const bool isInterior = player && player->GetParentCell() ? player->GetParentCell()->IsInteriorCell() : false;
	const bool isInCombat = player && player->IsInCombat();
	const bool isWeaponDrawn = compat->IsPlayerWeaponDrawn();
	const bool isLockedOn = compat->IsTDMActive();
	const bool isSneaking = player && player->IsSneaking();

	// Local set to track paths processed in this frame and prevent growth leaks
	std::unordered_set<std::string> processedPaths;

	for (const auto& def : HUDElements::Get()) {
		bool isCompass = (strcmp(def.id, "iMode_Compass") == 0);
		bool isStealthMeter = (strcmp(def.id, "iMode_StealthMeter") == 0);
		bool isHealth = (strcmp(def.id, "iMode_Health") == 0);
		bool isMagicka = (strcmp(def.id, "iMode_Magicka") == 0);
		bool isStamina = (strcmp(def.id, "iMode_Stamina") == 0);
		bool isEnchantLeft, isEnchantRight, isEnchantSkyHUD;
		bool isEnchantElement = IsEnchantmentElement(def.id, isEnchantLeft, isEnchantRight, isEnchantSkyHUD);
		bool isResourceBar = isHealth || isMagicka || isStamina;
		bool isCrosshair = def.isCrosshair;

		for (const auto& path : def.paths) {
			processedPaths.insert(path);

			int mode = settings->GetWidgetMode(path);
			RE::GFxValue elem;
			if (!a_movie->GetVariable(&elem, path) || !elem.IsDisplayObject()) {
				continue;
			}

			RE::GFxValue::DisplayInfo dInfo;
			elem.GetDisplayInfo(&dInfo);

			// Mutual Exclusion: SkyHUD Combined mode vs Vanilla Left/Right
			if ((_isSkyHUDActive && (isEnchantLeft || isEnchantRight)) || (!_isSkyHUDActive && isEnchantSkyHUD)) {
				dInfo.SetVisible(false);
				dInfo.SetAlpha(0.0);
				elem.SetDisplayInfo(dInfo);
				continue;
			}

			// TESGlobal in esp ensures compass is always hidden if set.
			if (isCompass && !compat->IsCompassAllowed()) {
				dInfo.SetVisible(false);
				dInfo.SetAlpha(0.0);
				elem.SetDisplayInfo(dInfo);
				continue;
			}

			// Stealth Meter Handling (Unified Logic)
			if (isStealthMeter) {
				if (mode == Settings::kIgnored) {
					// Manual Vanilla Authority
					float finalAlpha = _ctxSneakAlpha;

					// Apply Pulse logic (Ensures vanilla mode still breathes when detected)
					if (isSneaking && _lastDetectionLevel > 0.1f && finalAlpha > 0.01f) {
						constexpr float kPulseRange = 0.05f;
						constexpr float kPulseFreq = 0.05f;
						auto detectionFreq = (_lastDetectionLevel / 200.0f) + 0.5f;
						auto pulse = (kPulseRange * std::sin(2.0f * (std::numbers::pi_v<float> * 2.0f) * detectionFreq * kPulseFreq * 0.25f * _timer)) + (1.0f - kPulseRange);
						finalAlpha *= std::min(static_cast<float>(pulse), 1.0f);
					}

					bool finalVisible = (finalAlpha > 0.1f) && !menuOpen;
					dInfo.SetVisible(finalVisible);
					dInfo.SetAlpha(finalAlpha);
					elem.SetDisplayInfo(dInfo);

					// Sync sub-clips
					const char* subPaths[] = { "SneakAnimInstance", "SneakTextHolder" };
					for (auto p : subPaths) {
						RE::GFxValue sub;
						if (elem.GetMember(p, &sub) && sub.IsDisplayObject()) {
							RE::GFxValue::DisplayInfo sd;
							sd.SetVisible(finalVisible);
							sd.SetAlpha(finalAlpha);
							sub.SetDisplayInfo(sd);
						}
					}
					continue;
				}

				// Managed/Contextual Authority
				float finalAlpha = _ctxSneakAlpha;
				if (settings->GetSneakMeterSettings().enabled && isSneaking && _lastDetectionLevel > 0.1f && finalAlpha > 0.01f) {
					constexpr float kPulseRange = 0.05f;
					constexpr float kPulseFreq = 0.05f;
					auto detectionFreq = (_lastDetectionLevel / 200.0f) + 0.5f;
					auto pulse = (kPulseRange * std::sin(2.0f * (std::numbers::pi_v<float> * 2.0f) * detectionFreq * kPulseFreq * 0.25f * _timer)) + (1.0f - kPulseRange);
					finalAlpha *= std::min(static_cast<float>(pulse), 1.0f);
				}

				double targetAlpha = finalAlpha;
				bool shouldBeVisible = (targetAlpha > 0.1f) && !menuOpen;
				dInfo.SetVisible(shouldBeVisible);
				dInfo.SetAlpha(targetAlpha);
				elem.SetDisplayInfo(dInfo);

				// Clip Injection: override eye/text clips
				const char* subPaths[] = { "SneakAnimInstance", "SneakTextHolder" };
				for (auto p : subPaths) {
					RE::GFxValue sub;
					if (elem.GetMember(p, &sub) && sub.IsDisplayObject()) {
						RE::GFxValue::DisplayInfo sd;
						sd.SetVisible(shouldBeVisible);
						sd.SetAlpha(targetAlpha);
						sub.SetDisplayInfo(sd);
					}
				}
				continue;
			}

			// Enchantment kIgnored block
			if (mode == Settings::kIgnored && isEnchantElement) {
				float target = CalculateEnchantmentIgnoredAlpha(isEnchantLeft, isEnchantSkyHUD, menuOpen, alphaL, alphaR);
				if (isEnchantSkyHUD) {
					ApplySkyHUDEnchantment(elem, alphaL, alphaR, 0.0f, 0, true);
				}
				dInfo.SetVisible(target > 0.01);
				dInfo.SetAlpha(target);
				elem.SetDisplayInfo(dInfo);
				if (target > 0.1 && !isEnchantSkyHUD) {
					EnforceEnchantMeterVisible(elem);
				}
				continue;
			}

			// Handle reset for other ignored elements
			if (mode == Settings::kIgnored) {
				if (!dInfo.GetVisible() || dInfo.GetAlpha() < 100.0) {
					dInfo.SetVisible(true);
					dInfo.SetAlpha(100.0);
					elem.SetDisplayInfo(dInfo);
				}
				continue;
			}

			bool shouldBeVisible = true;
			double targetAlpha = managedAlpha;
			if (mode == Settings::kHidden) {
				shouldBeVisible = false;
				targetAlpha = 0.0;
			} else if (mode == Settings::kInterior) {
				targetAlpha = interiorAlpha;
				shouldBeVisible = isInterior && !menuOpen;
			} else if (mode == Settings::kExterior) {
				targetAlpha = exteriorAlpha;
				shouldBeVisible = !isInterior && !menuOpen;
			} else if (mode == Settings::kInCombat) {
				targetAlpha = combatAlpha;
				shouldBeVisible = isInCombat && !menuOpen;
			} else if (mode == Settings::kNotInCombat) {
				targetAlpha = notInCombatAlpha;
				shouldBeVisible = !isInCombat && !menuOpen;
			} else if (mode == Settings::kWeaponDrawn) {
				targetAlpha = weaponAlpha;
				shouldBeVisible = isWeaponDrawn && !menuOpen;
			} else if (mode == Settings::kLockedOn) {
				targetAlpha = lockedOnAlpha;
				shouldBeVisible = isLockedOn && !menuOpen;
			} else if (isEnchantElement) {
				targetAlpha = CalculateEnchantmentTargetAlpha(isEnchantLeft, isEnchantSkyHUD, mode, alphaL, alphaR, managedAlpha);
				shouldBeVisible = (targetAlpha > 0.01);
			} else if (mode == Settings::kVisible) {
				shouldBeVisible = !menuOpen;
				targetAlpha = menuOpen ? 0.0 : 100.0;
			} else {
				if (isCrosshair) {
					float ctxBased = (menuOpen ? 0.0f : _ctxAlpha);
					if (compat->IsSmoothCamActive() && ctxBased > 0.01f) {
						ctxBased = 0.01f;
					}
					targetAlpha = ctxBased;
					shouldBeVisible = (targetAlpha > 0.0);
				} else {
					// Check master toggle and transient state triggers
					bool shouldShow = _userWantsVisible;
					if (!shouldShow) {
						if ((settings->IsAlwaysShowInCombat() && isInCombat) ||
							(settings->IsAlwaysShowWeaponDrawn() && isWeaponDrawn)) {
							shouldShow = true;
						}
					}
					// Synchronize with global HUD fade and menu safety
					shouldBeVisible = shouldShow && !menuOpen;
					targetAlpha = shouldShow ? managedAlpha : 0.0;
				}
			}

			dInfo.SetVisible(shouldBeVisible);
			dInfo.SetAlpha(targetAlpha);
			elem.SetDisplayInfo(dInfo);

			// Visibility Hammer logic: Override engine hiding
			if (shouldBeVisible && (targetAlpha > 0.1 || _wasHidden)) {
				if (isResourceBar) {
					// Pass true if the mode is Immersive/Visible to override "hide when full"
					bool forceOverride = (mode == Settings::kVisible || mode == Settings::kImmersive ||
										  mode == Settings::kInterior || mode == Settings::kExterior ||
										  mode == Settings::kInCombat || mode == Settings::kNotInCombat ||
										  mode == Settings::kWeaponDrawn || mode == Settings::kLockedOn);
					EnforceHMSMeterVisible(elem, forceOverride);
				} else if (isEnchantSkyHUD) {
					ApplySkyHUDEnchantment(elem, 0.0f, 0.0f, static_cast<float>(targetAlpha), mode, false);
				} else if (isEnchantLeft || isEnchantRight) {
					EnforceEnchantMeterVisible(elem);
				}
			}
		}
	}

	const auto& pathSet = settings->GetSubWidgetPaths();
	for (const auto& path : pathSet) {
		// Generic HUD check: prevents dynamic scan from stomping on processed vanilla elements
		if (processedPaths.contains(path) ||
			path == "_root.HUDMovieBaseInstance.StealthMeterInstance") {
			continue;
		}

		if (path.find("markerData") != std::string::npos ||
			path.find("widgetLoaderContainer") != std::string::npos) {
			continue;
		}

		int mode = settings->GetWidgetMode(path);
		// Menus active: relinquish control of dynamic widgets to allow 3rd party function.
		if (menuOpen && mode != Settings::kHidden) {
			continue;
		}

		RE::GFxValue elem;
		if (!a_movie->GetVariable(&elem, path.c_str()) || !elem.IsDisplayObject()) {
			continue;
		}
		RE::GFxValue::DisplayInfo dInfo;
		elem.GetDisplayInfo(&dInfo);

		// Handle reset for ignored widgets.
		if (mode == Settings::kIgnored) {
			if (!dInfo.GetVisible() || dInfo.GetAlpha() < 100.0) {
				dInfo.SetVisible(true);
				dInfo.SetAlpha(100.0);
				elem.SetDisplayInfo(dInfo);
			}
			continue;
		}

		if (mode == Settings::kHidden) {
			dInfo.SetVisible(false);
			dInfo.SetAlpha(0.0);
		} else if (mode == Settings::kVisible) {
			dInfo.SetVisible(true);
			dInfo.SetAlpha(100.0);
		} else if (mode == Settings::kInterior) {
			dInfo.SetVisible(interiorAlpha > 0.01);
			dInfo.SetAlpha(interiorAlpha);
		} else if (mode == Settings::kExterior) {
			dInfo.SetVisible(exteriorAlpha > 0.01);
			dInfo.SetAlpha(exteriorAlpha);
		} else if (mode == Settings::kInCombat) {
			dInfo.SetVisible(combatAlpha > 0.01);
			dInfo.SetAlpha(combatAlpha);
		} else if (mode == Settings::kNotInCombat) {
			dInfo.SetVisible(notInCombatAlpha > 0.01);
			dInfo.SetAlpha(notInCombatAlpha);
		} else if (mode == Settings::kWeaponDrawn) {
			dInfo.SetVisible(weaponAlpha > 0.01);
			dInfo.SetAlpha(weaponAlpha);
		} else if (mode == Settings::kLockedOn) {
			dInfo.SetVisible(lockedOnAlpha > 0.01);
			dInfo.SetAlpha(lockedOnAlpha);
		} else {
			dInfo.SetVisible(managedAlpha > 0.1f);
			dInfo.SetAlpha(managedAlpha);
		}
		elem.SetDisplayInfo(dInfo);
	}
}

void HUDManager::ApplyAlphaToHUD(float a_alpha)
{
	const auto ui = RE::UI::GetSingleton();
	const auto settings = Settings::GetSingleton();
	if (!ui) {
		return;
	}

	const bool menuOpen = ShouldHideHUD();

	// Use already calculated fading alphas
	const float interiorAlpha = menuOpen ? 0.0f : _interiorAlpha;
	const float exteriorAlpha = menuOpen ? 0.0f : _exteriorAlpha;
	const float combatAlpha = menuOpen ? 0.0f : _combatAlpha;
	const float weaponAlpha = menuOpen ? 0.0f : _weaponAlpha;
	const float notInCombatAlpha = menuOpen ? 0.0f : _notInCombatAlpha;
	const float lockedOnAlpha = menuOpen ? 0.0f : _lockedOnAlpha;

	for (auto& [name, entry] : ui->menuMap) {
		if (!entry.menu || !entry.menu->uiMovie) {
			continue;
		}
		std::string menuNameStr(name.c_str());
		if (menuNameStr == "HUD Menu") {
			ApplyHUDMenuSpecifics(entry.menu->uiMovie, a_alpha);
			continue;
		}
		if (Utils::IsSystemMenu(menuNameStr)) {
			continue;
		}

		int mode = settings->GetWidgetMode(menuNameStr);
		// Menus active: relinquish control of external menus.
		if (menuOpen && mode != Settings::kHidden) {
			continue;
		}
		if (mode == Settings::kIgnored) {
			continue;
		}

		RE::GFxValue root;
		if (!entry.menu->uiMovie->GetVariable(&root, "_root")) {
			continue;
		}
		RE::GFxValue::DisplayInfo dInfo;
		if (mode == Settings::kVisible) {
			dInfo.SetAlpha(100.0);
		} else if (mode == Settings::kHidden) {
			dInfo.SetAlpha(0.0);
		} else if (mode == Settings::kInterior) {
			dInfo.SetAlpha(interiorAlpha);
		} else if (mode == Settings::kExterior) {
			dInfo.SetAlpha(exteriorAlpha);
		} else if (mode == Settings::kInCombat) {
			dInfo.SetAlpha(combatAlpha);
		} else if (mode == Settings::kNotInCombat) {
			dInfo.SetAlpha(notInCombatAlpha);
		} else if (mode == Settings::kWeaponDrawn) {
			dInfo.SetAlpha(weaponAlpha);
		} else if (mode == Settings::kLockedOn) {
			dInfo.SetAlpha(lockedOnAlpha);
		} else {
			dInfo.SetAlpha(a_alpha);
		}
		root.SetDisplayInfo(dInfo);
	}
}
