#include "HUDManager.h"
#include "MCMGen.h"
#include "Settings.h"
#include "Utils.h"
#include <numbers>

// ==========================================
// Event Handlers
// ==========================================

class InputHandler : public RE::BSTEventSink<RE::InputEvent*>
{
public:
	static InputHandler* GetSingleton()
	{
		static InputHandler singleton;
		return &singleton;
	}

	RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, [[maybe_unused]] RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override
	{
		if (!a_event || !*a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const auto settings = Settings::GetSingleton();
		const auto key = settings->GetToggleKey();

		if (key == static_cast<std::uint32_t>(-1)) {
			return RE::BSEventNotifyControl::kContinue;
		}

		for (auto event = *a_event; event; event = event->next) {
			if (const auto button = event->AsButtonEvent()) {
				if (button->GetIDCode() == key) {
					if (button->IsDown()) {
						HUDManager::GetSingleton()->OnButtonDown();
					} else if (button->IsUp()) {
						HUDManager::GetSingleton()->OnButtonUp();
					}
				}
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}
};

class MenuHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	static MenuHandler* GetSingleton()
	{
		static MenuHandler singleton;
		return &singleton;
	}

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override
	{
		if (a_event && a_event->menuName == "Journal Menu") {
			if (a_event->opening) {
			} else {
				// Reload settings when leaving the menu to apply any changes made.
				Settings::GetSingleton()->Load();
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}
};

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
	auto* deviceManager = RE::BSInputDeviceManager::GetSingleton();
	if (deviceManager) {
		deviceManager->AddEventSink(InputHandler::GetSingleton());
	}

	auto* ui = RE::UI::GetSingleton();
	if (ui) {
		ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuHandler::GetSingleton());
	}

	stl::write_vfunc<RE::PlayerCharacter, PlayerUpdateHook>();
	stl::write_vfunc<RE::StealthMeter, StealthMeterHook>();

	Settings::GetSingleton()->Load();
	_userWantsVisible = false;
	_installed = true;
	logger::info("HUDManager hooks installed.");
}

void HUDManager::InitIFPV()
{
	if (!g_IFPV) {
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (dataHandler) {
			g_IFPV = dataHandler->LookupForm<RE::TESGlobal>(0x801, "IFPVDetector.esl");
		}
	}
}

void HUDManager::Reset()
{
	MCMGen::ResetSession();
	Settings::GetSingleton()->ResetCache();
	_userWantsVisible = false;
	_currentAlpha = 0.0f;
	_targetAlpha = 0.0f;
	_ctxAlpha = 0.0f;
	_ctxSneakAlpha = 0.0f;
	_timer = 0.0f;
	_loadGracePeriod = 2.0f;
	logger::info("HUDManager reset.");
}

void HUDManager::ScanIfReady()
{
	if (_hasScanned) {
		return;
	}
	auto* ui = RE::UI::GetSingleton();
	if (ui && ui->GetMenu("HUD Menu")) {
		SKSE::GetTaskInterface()->AddUITask([this]() {
			ScanForWidgets(false);
		});
		_hasScanned = true;
	}
}

void HUDManager::ForceScan()
{
	// Can still be called manually if needed, but not attached to Journal anymore.
	ScanForWidgets(true);
}

void HUDManager::OnButtonDown()
{
	// Scan whenever the button is pressed.
	// We pass 'false' for forceUpdate so it only logs if new widgets are found.
	SKSE::GetTaskInterface()->AddUITask([this]() {
		ScanForWidgets(false);
	});

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
// Compatibility & Logic Checks
// ==========================================

bool HUDManager::CompatibilityCheck_IFPV() const
{
	return g_IFPV && (g_IFPV->value != 0.0f);
}

bool HUDManager::IsFakeFirstPerson() const
{
	auto camera = RE::PlayerCamera::GetSingleton();
	if (!camera) {
		return false;
	}
	if (camera->IsInFirstPerson() || camera->IsInFreeCameraMode()) {
		return false;
	}

	auto thirdPersonState = static_cast<RE::ThirdPersonState*>(camera->currentState.get());
	return thirdPersonState && thirdPersonState->currentZoomOffset == -0.275f;
}

bool HUDManager::CompatibilityCheck_TDM()
{
	return g_TDM && g_TDM->GetTargetLockState();
}

bool HUDManager::CompatibilityCheck_SmoothCam()
{
	if (CompatibilityCheck_IFPV()) {
		return false;
	}
	if (IsFakeFirstPerson()) {
		return false;
	}

	if (g_SmoothCam && g_SmoothCam->IsCameraEnabled()) {
		if (auto playerCamera = RE::PlayerCamera::GetSingleton(); playerCamera) {
			return playerCamera->currentState == playerCamera->cameraStates[RE::CameraState::kThirdPerson];
		}
	}
	return false;
}

bool HUDManager::CompatibilityCheck_DetectionMeter()
{
	return g_DetectionMeter != nullptr;
}

bool HUDManager::CompatibilityCheck_BTPS()
{
	return g_BTPS && g_BTPS->GetWidget3DEnabled();
}

bool HUDManager::ValidPickType()
{
	if (auto crosshairPickData = RE::CrosshairPickData::GetSingleton()) {
		if (auto refr = crosshairPickData->target.get()) {
			return refr->GetFormType() != RE::FormType::ActorCharacter || refr.get()->As<RE::Actor>()->IsDead();
		}
	}
	return false;
}

bool HUDManager::ValidCastType(RE::ActorMagicCaster* a_magicCaster)
{
	return a_magicCaster && ValidSpellType(a_magicCaster->currentSpell);
}

bool HUDManager::ValidAttackType(RE::PlayerCharacter* a_player)
{
	auto equipped = a_player->GetEquippedObject(true);
	auto attackState = a_player->actorState1.meleeAttackState;

	if (equipped && equipped->GetFormType() == RE::FormType::Weapon) {
		auto weapon = equipped->As<RE::TESObjectWEAP>();
		if (weapon->IsBow()) {
			return attackState >= RE::ATTACK_STATE_ENUM::kBowAttached && attackState <= RE::ATTACK_STATE_ENUM::kBowNextAttack;
		}
		if (weapon->IsCrossbow()) {
			return attackState >= RE::ATTACK_STATE_ENUM::kBowDraw && attackState <= RE::ATTACK_STATE_ENUM::kNextAttack;
		}
	}
	return false;
}

bool HUDManager::ValidSpellType(RE::MagicItem* a_magicItem)
{
	if (!a_magicItem) {
		return false;
	}

	auto isTelekinesis = false;
	for (auto effect : a_magicItem->effects) {
		if (effect->baseEffect && effect->baseEffect->HasArchetype(RE::EffectSetting::Archetype::kTelekinesis)) {
			isTelekinesis = true;
			break;
		}
	}

	return ((a_magicItem->GetDelivery() == RE::MagicSystem::Delivery::kAimed) &&
			   (a_magicItem->GetCastingType() != RE::MagicSystem::CastingType::kConcentration)) ||
	       isTelekinesis;
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
	if (!player) {
		return;
	}

	// Grace Period: Count down, then trigger rescan.
	// This ensures late widgets are caught after the delay.
	if (_loadGracePeriod > 0.0f) {
		_loadGracePeriod -= a_delta;

		if (_loadGracePeriod <= 0.0f) {
			_loadGracePeriod = 0.0f;
			SKSE::GetTaskInterface()->AddUITask([this]() {
				ScanForWidgets(false);
			});
			logger::info("Grace period ended. Rescanning for late widgets.");
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

	const bool smoothCam = CompatibilityCheck_SmoothCam();
	const bool tdm = CompatibilityCheck_TDM();
	const bool btps = CompatibilityCheck_BTPS();

	const auto settings = Settings::GetSingleton();
	bool shouldBeVisible = _userWantsVisible;

	if (!_userWantsVisible) {
		if ((settings->IsAlwaysShowInCombat() && player->IsInCombat()) ||
			(settings->IsAlwaysShowWeaponDrawn() && player->IsWeaponDrawn())) {
			shouldBeVisible = true;
		}
	}

	_targetAlpha = shouldBeVisible ? 100.0f : 0.0f;
	const float change = settings->GetFadeSpeed() * (a_delta * 60.0f);

	if (std::abs(_currentAlpha - _targetAlpha) <= change) {
		_currentAlpha = _targetAlpha;
	} else if (_currentAlpha < _targetAlpha) {
		_currentAlpha += change;
	} else {
		_currentAlpha -= change;
	}

	_prevDelta = a_delta;
	_timer += _prevDelta;

	if (settings->GetCrosshairSettings().enabled) {
		float targetCtx = 0.0f;

		if (tdm) {
			targetCtx = 0.0f;  // Lock-on active, hide vanilla crosshair.
		} else {
			bool actionActive = ValidCastType(player->magicCasters[0]) ||
			                    ValidCastType(player->magicCasters[1]) ||
			                    ValidAttackType(player);

			if (actionActive || smoothCam) {
				targetCtx = 100.0f;
			} else if (ValidPickType() && !btps) {
				targetCtx = 50.0f;
			}
		}
		_ctxAlpha = std::lerp(_ctxAlpha, targetCtx, _prevDelta * settings->GetFadeSpeed());
	} else {
		_ctxAlpha = _currentAlpha;
	}

	ApplyAlphaToHUD(_currentAlpha);
}

void HUDManager::UpdateContextualStealth(float a_detectionLevel, RE::GFxValue a_sneakAnim)
{
	_cachedSneakAnim = a_sneakAnim;
	_hasCachedSneakAnim = true;

	const auto settings = Settings::GetSingleton();
	if (!RE::PlayerCharacter::GetSingleton()) {
		return;
	}

	const bool smoothCam = CompatibilityCheck_SmoothCam();
	const bool tdm = CompatibilityCheck_TDM();
	const bool detectionMeter = CompatibilityCheck_DetectionMeter();

	const int widgetMode = settings->GetWidgetMode("_root.HUDMovieBaseInstance.StealthMeterInstance");
	float targetAlpha = 0.0f;

	if (RE::PlayerCharacter::GetSingleton()->IsSneaking()) {
		if (widgetMode == Settings::kIgnored) {
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

	_ctxSneakAlpha = std::lerp(_ctxSneakAlpha, targetAlpha, _prevDelta * settings->GetFadeSpeed());

	if (widgetMode == Settings::kImmersive && settings->GetSneakMeterSettings().enabled && !_userWantsVisible && _ctxSneakAlpha > 0.01f) {
		constexpr float kPulseRange = 0.05f;
		constexpr float kPulseFreq = 0.05f;
		auto detectionFreq = (a_detectionLevel / 200.0f) + 0.5f;
		auto pulse = (kPulseRange * std::sin(2.0f * (std::numbers::pi_v<float> * 2.0f) * detectionFreq * kPulseFreq * 0.25f * _timer)) + (1.0f - kPulseRange);
		_ctxSneakAlpha *= std::min(pulse, 1.0f);
	}

	RE::GFxValue::DisplayInfo displayInfo;
	a_sneakAnim.GetDisplayInfo(std::addressof(displayInfo));
	displayInfo.SetAlpha(static_cast<float>(_ctxSneakAlpha));
	a_sneakAnim.SetDisplayInfo(displayInfo);
}

// ==========================================
// Menu & Widget Management
// ==========================================

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

void HUDManager::ScanForWidgets(bool a_forceUpdate)
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}

	bool changes = false;
	int externalCount = 0;
	int containerCount = 0;
	auto* settings = Settings::GetSingleton();

	// Manual registration.
	if (settings->AddDiscoveredPath("_root.HUDMovieBaseInstance.StealthMeterInstance", "Internal/StealthMeter")) {
		changes = true;
	}

	// Scan external menus.
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

	// Scan internal HUD.
	auto hud = ui->GetMenu("HUD Menu");
	if (hud && hud->uiMovie) {
		ScanForContainers(hud->uiMovie.get(), containerCount, changes);
	}

	if (changes || a_forceUpdate) {
		MCMGen::Update(_loadGracePeriod <= 0.0f);

		// Reload settings immediately to consume the new ID map generated by MCMGen.
		Settings::GetSingleton()->Load();

		if (changes) {
			logger::info("Scan complete: Found {} external, {} internal.", externalCount, containerCount);
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
	static const std::vector<const char*> kBaseElements = {
		"_root.HUDMovieBaseInstance.CompassShoutMeterHolder",
		"_root.HUDMovieBaseInstance.Health",
		"_root.HUDMovieBaseInstance.Magica",
		"_root.HUDMovieBaseInstance.Stamina",
		"_root.HUDMovieBaseInstance.EnemyHealth_mc",
		"_root.HUDMovieBaseInstance.ChargeMeterBaseAlt",
		"_root.HUDMovieBaseInstance.RolloverName_mc",
		"_root.HUDMovieBaseInstance.RolloverInfo_mc",
		"_root.HUDMovieBaseInstance.ArrowInfoInstance",
		"_root.HUDMovieBaseInstance.FavorBackButtonBase"
	};

	RE::GFxValue root;
	if (!a_movie->GetVariable(&root, "_root")) {
		return;
	}

	for (const auto& path : kBaseElements) {
		RE::GFxValue elem;
		if (a_movie->GetVariable(&elem, path)) {
			elem.SetMember("_alpha", a_globalAlpha);
		}
	}

	float crosshairAlpha = (_ctxAlpha * 0.01f * 100.0f);
	// SmoothCam logic: minimal opacity if active to prevent game hiding it.
	if (CompatibilityCheck_SmoothCam() && crosshairAlpha > 0.01f) {
		crosshairAlpha = 0.01f;
	}

	RE::GFxValue crosshairInstance;
	if (a_movie->GetVariable(&crosshairInstance, "_root.HUDMovieBaseInstance.CrosshairInstance")) {
		RE::GFxValue::DisplayInfo displayInfo;
		crosshairInstance.GetDisplayInfo(std::addressof(displayInfo));
		displayInfo.SetAlpha(crosshairAlpha);
		crosshairInstance.SetDisplayInfo(displayInfo);
	}

	RE::GFxValue crosshairAlert;
	if (a_movie->GetVariable(&crosshairAlert, "_root.HUDMovieBaseInstance.CrosshairAlert")) {
		RE::GFxValue::DisplayInfo displayInfo;
		crosshairAlert.GetDisplayInfo(std::addressof(displayInfo));
		displayInfo.SetAlpha(crosshairAlpha);
		crosshairAlert.SetDisplayInfo(displayInfo);
	}

	std::vector<std::string> paths;
	const auto& pathSet = settings->GetSubWidgetPaths();
	paths.reserve(pathSet.size());
	paths.assign(pathSet.begin(), pathSet.end());

	for (const auto& path : paths) {
		if (!path.starts_with("_root.") || path == "_root.HUDMovieBaseInstance.StealthMeterInstance") {
			continue;
		}
		if (path.find("markerData") != std::string::npos || path.find("widgetLoaderContainer") != std::string::npos) {
			continue;
		}

		int mode = settings->GetWidgetMode(path);
		RE::GFxValue elem;
		if (!a_movie->GetVariable(&elem, path.c_str())) {
			continue;
		}

		if (mode == Settings::kIgnored) {
			elem.SetMember("_alpha", 100.0);
		} else if (mode == Settings::kHidden) {
			elem.SetMember("_visible", false);
		} else {
			RE::GFxValue visVal;
			if (elem.GetMember("_visible", &visVal) && visVal.GetBool()) {
				elem.SetMember("_alpha", a_globalAlpha);
			}
		}
	}
}

void HUDManager::ApplyAlphaToHUD(float a_alpha)
{
	const auto ui = RE::UI::GetSingleton();
	const auto settings = Settings::GetSingleton();
	if (!ui) {
		return;
	}

	bool tdmActive = CompatibilityCheck_TDM();

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

		if (mode == Settings::kIgnored) {
			if (!entry.menu->uiMovie->GetVisible()) {
				entry.menu->uiMovie->SetVisible(true);
			}
			RE::GFxValue root;
			if (entry.menu->uiMovie->GetVariable(&root, "_root")) {
				root.SetMember("_alpha", 100.0);
			}
			continue;
		}

		if (mode == Settings::kHidden) {
			entry.menu->uiMovie->SetVisible(false);
		} else {
			if (!entry.menu->uiMovie->GetVisible()) {
				entry.menu->uiMovie->SetVisible(true);
			}

			RE::GFxValue root;
			if (entry.menu->uiMovie->GetVariable(&root, "_root")) {
				// TrueHUD Specific Handling
				if (menuNameStr == "TrueHUD") {
					// If TDM lock-on is active, force TrueHUD visible (100% alpha) to show reticle/target.
					// If not locked, it obeys global alpha (allowing health bars to hide/show via TrueHUD logic + iHUD fade).
					if (tdmActive) {
						root.SetMember("_alpha", 100.0);
					} else {
						root.SetMember("_alpha", a_alpha);
					}
				} else {
					// Standard External Widget
					root.SetMember("_alpha", a_alpha);
				}
			}
		}
	}
}
