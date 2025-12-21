#include "Compat.h"
#include "Events.h"
#include "HUDElements.h"
#include "HUDManager.h"
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
		VisibilityHammer(bool a_skipFills = false) :
			_skipFills(a_skipFills) {}

		void Visit(const char* a_name, const RE::GFxValue& a_val) override
		{
			if (a_val.IsDisplayObject()) {
				std::string name = a_name ? a_name : "";
				std::transform(name.begin(), name.end(), name.begin(), ::tolower);

				if (_skipFills) {
					// Skip internal bar 'fills' for HMS and Enemy Health to allow draining animations.
					if (name == "metermovieclip" || name == "penaltymetermovieclip" ||
						name == "blinkmovieclip" || name.find("meter_mc") != std::string::npos ||
						name.find("penaltymeter_mc") != std::string::npos) {
						return;
					}
				}

				RE::GFxValue::DisplayInfo d;
				const_cast<RE::GFxValue&>(a_val).GetDisplayInfo(&d);
				d.SetVisible(true);
				d.SetAlpha(100.0);
				const_cast<RE::GFxValue&>(a_val).SetDisplayInfo(d);
				const_cast<RE::GFxValue&>(a_val).VisitMembers(this);
			}
		}

	private:
		bool _skipFills;
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
		HUDManager::GetSingleton()->UpdateContextualStealth(static_cast<float>(a_this->unk88), a_this->sneakAnim);
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

	Settings::GetSingleton()->Load();
	_userWantsVisible = false;
	_installed = true;
	logger::info("HUDManager hooks installed.");
}

void HUDManager::Reset()
{
	Settings::GetSingleton()->Load();
	_userWantsVisible = false;
	_currentAlpha = 0.0f;
	_targetAlpha = 0.0f;
	_ctxAlpha = 0.0f;
	_ctxSneakAlpha = 0.0f;
	_enchantAlphaL = 0.0f;
	_enchantAlphaR = 0.0f;
	_timer = 0.0f;
	_scanTimer = 0.0f;

	if (_loadGracePeriod == 0.0f) {
		_loadGracePeriod = 2.0f;
	}
	logger::info("HUDManager reset.");
}

void HUDManager::ScanIfReady()
{
	if (_hasScanned || _isScanPending) {
		return;
	}
	auto* ui = RE::UI::GetSingleton();
	if (ui && ui->GetMenu("HUD Menu")) {
		_isScanPending = true;
		SKSE::GetTaskInterface()->AddUITask([this]() {
			ScanForWidgets(false, false);
			_isScanPending = false;
		});
		_hasScanned = true;
	}
}

void HUDManager::RegisterNewMenu()
{
	if (_isScanPending) {
		return;
	}
	_isScanPending = true;
	SKSE::GetTaskInterface()->AddUITask([this]() {
		ScanForWidgets(false, false);
		_isScanPending = false;
	});
}

void HUDManager::ForceScan()
{
	ScanForWidgets(true, true);
}

void HUDManager::OnButtonDown()
{
	if (Settings::GetSingleton()->IsDumpHUDEnabled()) {
		SKSE::GetTaskInterface()->AddUITask([this]() {
			DumpHUDStructure();
		});
	}

	if (Settings::GetSingleton()->IsHoldMode()) {
		_userWantsVisible = true;
	} else {
		_userWantsVisible = !_userWantsVisible;
	}
}

void HUDManager::OnButtonUp()
{
	if (Settings::GetSingleton()->IsHoldMode()) {
		_userWantsVisible = false;
	}
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

	// Grace period for initial load
	if (_loadGracePeriod > 0.0f) {
		_loadGracePeriod -= a_delta;
		if (_loadGracePeriod <= 0.0f) {
			_loadGracePeriod = -1.0f; // Transition to Runtime state after scan

			SKSE::GetTaskInterface()->AddUITask([this]() {
				ScanForWidgets(false, true);  // Deep scan
			});
			logger::info("Grace period ended. Rescanning.");
		}
	}

	// Periodic scan
	_scanTimer += a_delta;
	if (_scanTimer > 2.0f) {
		_scanTimer = 0.0f;
		if (_hasScanned) {
			SKSE::GetTaskInterface()->AddUITask([this]() {
				ScanForWidgets(false, false);
			});
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
	const auto settings = Settings::GetSingleton();

	// 1. Determine Visibility Targets
	bool shouldBeVisible = _userWantsVisible;

	if (!shouldBeVisible) {
		if ((settings->IsAlwaysShowInCombat() && player->IsInCombat()) ||
			(settings->IsAlwaysShowWeaponDrawn() && player->IsWeaponDrawn())) {
			shouldBeVisible = true;
		}
	}
	_targetAlpha = shouldBeVisible ? 100.0f : 0.0f;

	// Crosshair Target Alpha
	float targetCtx = 0.0f;
	if (settings->GetCrosshairSettings().enabled) {
		if (shouldHide || compat->IsTDMActive()) {
			targetCtx = 0.0f;
		} else {
			bool actionActive = compat->IsPlayerCasting(player) || compat->IsPlayerAttacking(player);
			if (actionActive || compat->IsSmoothCamActive()) {
				targetCtx = 100.0f;
			} else if (compat->IsCrosshairTargetValid() && !compat->IsBTPSActive()) {
				targetCtx = 50.0f;
			}
		}
	} else {
		targetCtx = _targetAlpha;
	}

	// 2. Handle Hidden State & Transitions
	if (shouldHide) {
		_wasHidden = true;
		compat->ManageSmoothCamControl(true);
		SKSE::GetTaskInterface()->AddUITask([this]() { ApplyAlphaToHUD(0.0f); });
		return;
	}

	// Snap to target instantly when coming out of a menu to match vanilla behaviour
	if (_wasHidden) {
		_currentAlpha = _targetAlpha;
		_ctxAlpha = targetCtx;
		_enchantAlphaL = (compat->IsPlayerWeaponDrawn() && compat->HasEnchantedWeapon(true)) ? 100.0f : 0.0f;
		_enchantAlphaR = (compat->IsPlayerWeaponDrawn() && compat->HasEnchantedWeapon(false)) ? 100.0f : 0.0f;
		_wasHidden = false;
	}

	compat->ManageSmoothCamControl(false);

	// 3. Mixed Math Calculations
	const float fadeSpeed = settings->GetFadeSpeed();
	const float change = fadeSpeed * (a_delta * 60.0f);

	// Global HUD: Linear Math (Vanilla Feel)
	if (std::abs(_currentAlpha - _targetAlpha) <= change) {
		_currentAlpha = _targetAlpha;
	} else if (_currentAlpha < _targetAlpha) {
		_currentAlpha += change;
	} else {
		_currentAlpha -= change;
	}

	// Crosshair: Lerp Math (Smooth Feel)
	_ctxAlpha = std::lerp(_ctxAlpha, targetCtx, a_delta * fadeSpeed);
	if (std::abs(_ctxAlpha - targetCtx) < 0.1f) {
		_ctxAlpha = targetCtx;
	}

	// Enchantment: Linear Math
	float targetEnL = (compat->IsPlayerWeaponDrawn() && compat->HasEnchantedWeapon(true)) ? 100.0f : 0.0f;
	float targetEnR = (compat->IsPlayerWeaponDrawn() && compat->HasEnchantedWeapon(false)) ? 100.0f : 0.0f;

	auto UpdateLinear = [&](float& a_current, float a_target) {
		if (std::abs(a_current - a_target) <= change) {
			a_current = a_target;
		} else if (a_current < a_target) {
			a_current += change;
		} else {
			a_current -= change;
		}
	};
	UpdateLinear(_enchantAlphaL, targetEnL);
	UpdateLinear(_enchantAlphaR, targetEnR);

	_prevDelta = a_delta;
	_timer += _prevDelta;

	SKSE::GetTaskInterface()->AddUITask([this, alpha = _currentAlpha]() {
		ApplyAlphaToHUD(alpha);
	});
}

void HUDManager::UpdateContextualStealth(float a_detectionLevel, RE::GFxValue a_sneakAnim)
{
	_cachedSneakAnim = a_sneakAnim;
	_hasCachedSneakAnim = true;

	const auto settings = Settings::GetSingleton();
	const auto player = RE::PlayerCharacter::GetSingleton();
	const auto compat = Compat::GetSingleton();
	if (!player) {
		return;
	}

	const int widgetMode = settings->GetWidgetMode("_root.HUDMovieBaseInstance.StealthMeterInstance");

	// Force hidden if HUD should be hidden, mode is Hidden, or global in esp is set
	if (ShouldHideHUD() || widgetMode == Settings::kHidden || !compat->IsSneakAllowed()) {
		RE::GFxValue::DisplayInfo d;
		a_sneakAnim.GetDisplayInfo(&d);
		if (d.GetVisible()) {
			d.SetVisible(false);
			d.SetAlpha(0.0f);
			a_sneakAnim.SetDisplayInfo(d);
		}
		return;
	}

	const bool smoothCam = compat->IsSmoothCamActive();
	const bool tdm = compat->IsTDMActive();
	const bool detectionMeter = compat->IsDetectionMeterInstalled();

	float targetAlpha = 0.0f;

	if (player->IsSneaking()) {
		if (widgetMode == Settings::kVisible) {
			targetAlpha = 100.0f;
		} else if (widgetMode == Settings::kImmersive) {
			if (settings->GetSneakMeterSettings().enabled) {
				if (_userWantsVisible) {
					targetAlpha = 100.0f;
				} else {
					if (smoothCam || tdm) {
						targetAlpha = detectionMeter ? 0.0f : (a_detectionLevel / 2.0f);
					} else {
						targetAlpha = std::clamp(detectionMeter ? 0.0f : a_detectionLevel, 0.0f, 100.0f);
					}
				}
				targetAlpha = (targetAlpha * 0.01f * 90.0f);
			} else {
				targetAlpha = _currentAlpha;
			}
		}
	}

	// Snap transition if we just resumed from a menu
	if (_wasHidden) {
		_ctxSneakAlpha = targetAlpha;
	}

	// Stealth Meter: Lerp Math (Smooth Feel)
	_ctxSneakAlpha = std::lerp(_ctxSneakAlpha, targetAlpha, _prevDelta * settings->GetFadeSpeed());
	if (std::abs(_ctxSneakAlpha - targetAlpha) < 0.1f) {
		_ctxSneakAlpha = targetAlpha;
	}

	// Pulse logic
	float finalAlpha = _ctxSneakAlpha;
	if (widgetMode == Settings::kImmersive && settings->GetSneakMeterSettings().enabled && !_userWantsVisible && finalAlpha > 0.01f) {
		constexpr float kPulseRange = 0.05f;
		constexpr float kPulseFreq = 0.05f;
		auto detectionFreq = (a_detectionLevel / 200.0f) + 0.5f;
		auto pulse = (kPulseRange * std::sin(2.0f * (std::numbers::pi_v<float> * 2.0f) * detectionFreq * kPulseFreq * 0.25f * _timer)) + (1.0f - kPulseRange);
		finalAlpha *= std::min(pulse, 1.0f);
	}

	if (std::abs(finalAlpha - _ctxSneakAlpha) > 0.01f || _wasHidden) {
		RE::GFxValue::DisplayInfo displayInfo;
		a_sneakAnim.GetDisplayInfo(std::addressof(displayInfo));
		displayInfo.SetAlpha(static_cast<float>(finalAlpha));
		displayInfo.SetVisible(finalAlpha > 0.1f);
		a_sneakAnim.SetDisplayInfo(displayInfo);
	}
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

	// Always hide if these core conditions are met
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

void HUDManager::EnforceHMSMeterVisible(RE::GFxValue& a_parent)
{
	if (a_parent.IsObject()) {
		VisibilityHammer hammer(true);  // Protect HMS anims
		a_parent.VisitMembers(&hammer);
	}
}

void HUDManager::EnforceEnchantMeterVisible(RE::GFxValue& a_parent)
{
	if (a_parent.IsObject()) {
		VisibilityHammer hammer(false);  // Fix unlabelled enchant meter frames
		a_parent.VisitMembers(&hammer);
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

void HUDManager::ScanForWidgets(bool a_forceUpdate, bool a_deepScan)
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

		// Use _loadGracePeriod to determine Init vs Runtime state for MCMGen status
		MCMGen::Update(_loadGracePeriod < 0.0f);

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

void HUDManager::ApplyHUDMenuSpecifics(RE::GPtr<RE::GFxMovieView> a_movie, float a_globalAlpha, bool a_hideAll)
{
	const auto settings = Settings::GetSingleton();
	const auto compat = Compat::GetSingleton();

	// Local set to track paths processed in this frame and prevent growth leaks
	std::unordered_set<std::string> processedPaths;

	for (const auto& def : HUDElements::Get()) {
		if (strcmp(def.id, "iMode_StealthMeter") == 0) {
			for (const auto& path : def.paths) {
				processedPaths.insert(path);
			}
			continue;
		}

		bool isHealth = (strcmp(def.id, "iMode_Health") == 0);
		bool isMagicka = (strcmp(def.id, "iMode_Magicka") == 0);
		bool isStamina = (strcmp(def.id, "iMode_Stamina") == 0);
		bool isEnchantLeft = (strcmp(def.id, "iMode_EnchantLeft") == 0);
		bool isEnchantRight = (strcmp(def.id, "iMode_EnchantRight") == 0);
		bool isEnchantSkyHUD = (strcmp(def.id, "iMode_EnchantCombined") == 0);
		bool isEnemyHealth = (strcmp(def.id, "iMode_EnemyHealth") == 0);
		bool isResourceBar = isHealth || isMagicka || isStamina || isEnemyHealth;
		bool isCrosshair = def.isCrosshair;

		for (const auto& path : def.paths) {
			processedPaths.insert(path);

			int mode = settings->GetWidgetMode(path);
			RE::GFxValue elem;

			if (!a_movie->GetVariable(&elem, path)) {
				continue;
			}

			if (!elem.IsDisplayObject()) {
				continue;
			}

			RE::GFxValue::DisplayInfo dInfo;
			elem.GetDisplayInfo(&dInfo);

			bool shouldBeVisible = true;
			double targetAlpha = a_globalAlpha;

			if (a_hideAll || mode == Settings::kHidden) {
				shouldBeVisible = false;
				targetAlpha = 0.0;
			} else if (strcmp(def.id, "iMode_Compass") == 0 && !compat->IsCompassAllowed()) {
				shouldBeVisible = false;
				targetAlpha = 0.0;
			}
			// Enchantment Logic: Context-aware Linear Transitions
			else if (isEnchantLeft || isEnchantRight || isEnchantSkyHUD) {
				float trackedAlpha = 0.0f;
				if (isEnchantLeft)
					trackedAlpha = _enchantAlphaL;
				else if (isEnchantRight)
					trackedAlpha = _enchantAlphaR;
				else if (isEnchantSkyHUD)
					trackedAlpha = std::max(_enchantAlphaL, _enchantAlphaR);

				if (mode == Settings::kVisible) {
					targetAlpha = trackedAlpha;
				} else {  // Immersive
					targetAlpha = std::min(trackedAlpha, static_cast<float>(a_globalAlpha));
				}
				shouldBeVisible = (targetAlpha > 0.01);
			} else if (mode == Settings::kVisible) {
				shouldBeVisible = true;
				targetAlpha = 100.0;
			} else {  // Immersive
				if (isCrosshair) {
					float ctxBased = (_ctxAlpha * 0.01f * 100.0f);
					if (compat->IsSmoothCamActive() && ctxBased > 0.01f) {
						ctxBased = 0.01f;
					}
					targetAlpha = ctxBased;
					shouldBeVisible = (targetAlpha > 0.0);
				} else {
					shouldBeVisible = (a_globalAlpha > 0.0);
					targetAlpha = a_globalAlpha;
				}
			}

			dInfo.SetVisible(shouldBeVisible);
			dInfo.SetAlpha(targetAlpha);
			elem.SetDisplayInfo(dInfo);

			// Hammer fixes for nested children
			if (shouldBeVisible && targetAlpha > 0.1) {
				if (isResourceBar) {
					EnforceHMSMeterVisible(elem);
				} else if (isEnchantLeft || isEnchantRight || isEnchantSkyHUD) {
					EnforceEnchantMeterVisible(elem);
				}
			}
		}
	}

	const auto& pathSet = settings->GetSubWidgetPaths();
	for (const auto& path : pathSet) {
		if (processedPaths.contains(path) ||
			path == "_root.HUDMovieBaseInstance.StealthMeterInstance") {
			continue;
		}

		if (path.find("markerData") != std::string::npos ||
			path.find("widgetLoaderContainer") != std::string::npos) {
			continue;
		}

		int mode = settings->GetWidgetMode(path);
		RE::GFxValue elem;

		if (!a_movie->GetVariable(&elem, path.c_str()) || !elem.IsDisplayObject()) {
			continue;
		}

		RE::GFxValue::DisplayInfo dInfo;
		if (a_hideAll || mode == Settings::kHidden) {
			dInfo.SetVisible(false);
			dInfo.SetAlpha(0.0);
		} else if (mode == Settings::kVisible) {
			dInfo.SetVisible(true);
			dInfo.SetAlpha(100.0);
		} else {
			dInfo.SetVisible(a_globalAlpha > 0.1f);
			dInfo.SetAlpha(a_globalAlpha);
		}
		elem.SetDisplayInfo(dInfo);
	}
}

void HUDManager::ApplyAlphaToHUD(float a_alpha)
{
	const auto ui = RE::UI::GetSingleton();
	const auto settings = Settings::GetSingleton();
	const auto compat = Compat::GetSingleton();
	if (!ui) {
		return;
	}

	bool shouldHideAll = ShouldHideHUD();
	bool tdmActive = compat->IsTDMActive();

	for (auto& [name, entry] : ui->menuMap) {
		if (!entry.menu || !entry.menu->uiMovie) {
			continue;
		}

		std::string menuNameStr(name.c_str());
		if (menuNameStr == "HUD Menu") {
			// Explicitly set the visibility of the whole HUD movie to prevent elements leaking through
			if (shouldHideAll) {
				if (entry.menu->uiMovie->GetVisible()) {
					entry.menu->uiMovie->SetVisible(false);
				}
			} else {
				if (!entry.menu->uiMovie->GetVisible()) {
					entry.menu->uiMovie->SetVisible(true);
				}
			}
			ApplyHUDMenuSpecifics(entry.menu->uiMovie, a_alpha, shouldHideAll);
			continue;
		}

		if (Utils::IsSystemMenu(menuNameStr)) {
			continue;
		}

		int mode = settings->GetWidgetMode(menuNameStr);

		// Skip external menu if Ignored
		if (mode == Settings::kIgnored) {
			continue;
		}

		RE::GFxValue root;
		if (!entry.menu->uiMovie->GetVariable(&root, "_root")) {
			continue;
		}

		RE::GFxValue::DisplayInfo dInfo;

		// Force hide for external menus if system menus are open.
		if (shouldHideAll) {
			if (entry.menu->uiMovie->GetVisible()) {
				entry.menu->uiMovie->SetVisible(false);
			}
			dInfo.SetAlpha(0.0);
			root.SetDisplayInfo(dInfo);
			continue;
		}

		if (mode == Settings::kVisible) {
			if (!entry.menu->uiMovie->GetVisible()) {
				entry.menu->uiMovie->SetVisible(true);
			}
			dInfo.SetAlpha(100.0);
			root.SetDisplayInfo(dInfo);
			continue;
		}

		if (mode == Settings::kHidden) {
			if (entry.menu->uiMovie->GetVisible()) {
				entry.menu->uiMovie->SetVisible(false);
			}
		} else {  // Immersive
			if (!entry.menu->uiMovie->GetVisible()) {
				entry.menu->uiMovie->SetVisible(true);
			}

			if (menuNameStr == "TrueHUD") {
				dInfo.SetAlpha(tdmActive ? 100.0 : a_alpha);
			} else {
				dInfo.SetAlpha(a_alpha);
			}
			root.SetDisplayInfo(dInfo);
		}
	}
}
