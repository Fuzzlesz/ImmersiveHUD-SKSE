#include "HUDManager.h"
#include "Events.h"
#include "HUDElements.h"
#include "MCMGen.h"
#include "Settings.h"
#include "Utils.h"
#include <numbers>

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
	Settings::GetSingleton()->Load();
	_userWantsVisible = false;
	_currentAlpha = 0.0f;
	_targetAlpha = 0.0f;
	_ctxAlpha = 0.0f;
	_ctxSneakAlpha = 0.0f;
	_timer = 0.0f;
	_scanTimer = 0.0f;
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
			ScanForWidgets(false, false);
		});
		_hasScanned = true;
	}
}

void HUDManager::RegisterNewMenu()
{
	SKSE::GetTaskInterface()->AddUITask([this]() {
		ScanForWidgets(false, false);
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

	// Grace period for initial load
	if (_loadGracePeriod > 0.0f) {
		_loadGracePeriod -= a_delta;
		if (_loadGracePeriod <= 0.0f) {
			_loadGracePeriod = 0.0f;

			SKSE::GetTaskInterface()->AddUITask([this]() {
				ScanForWidgets(false, true);  // Deep scan
			});
			logger::info("Grace period ended. Rescanning.");

			// Transition to Runtime state after scan
			SKSE::GetTaskInterface()->AddUITask([this]() {
				_loadGracePeriod = -1.0f;
			});
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

	SKSE::GetTaskInterface()->AddUITask([this, alpha = _currentAlpha]() {
		ApplyAlphaToHUD(alpha);
	});
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

bool HUDManager::ShouldHideHUD()
{
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		return false;
	}

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

void HUDManager::EnforceChildMeterVisible(RE::GFxValue& a_parent, const char* a_childName)
{
	RE::GFxValue child;
	if (a_parent.GetMember(a_childName, &child)) {
		RE::GFxValue::DisplayInfo childInfo;
		child.GetDisplayInfo(&childInfo);
		childInfo.SetVisible(true);
		childInfo.SetAlpha(100.0);
		child.SetDisplayInfo(childInfo);
	}
}

void HUDManager::ScanArrayContainer(const std::string& a_path, const RE::GFxValue& a_container, int& a_foundCount, bool& a_changes)
{
	for (int i = 0; i < 128; i++) {
		RE::GFxValue entry;
		std::string indexStr = std::to_string(i);

		if (!const_cast<RE::GFxValue&>(a_container).GetMember(indexStr.c_str(), &entry)) {
			continue;
		}
		if (!entry.IsObject()) {
			continue;
		}

		RE::GFxValue widget;
		if (!entry.GetMember("widget", &widget)) {
			if (entry.IsDisplayObject()) {
				widget = entry;
			} else {
				continue;
			}
		}
		if (!widget.IsDisplayObject()) {
			continue;
		}

		std::string widgetPath = a_path + "." + indexStr;
		std::string url = "Internal/SkyUI Widget";

		RE::GFxValue urlVal;
		if (widget.GetMember("_url", &urlVal) && urlVal.IsString()) {
			url = urlVal.GetString();
		}

		if (Settings::GetSingleton()->AddDiscoveredPath(widgetPath, url)) {
			a_changes = true;
			a_foundCount++;
			logger::info("Discovered SkyUI Element: {} [Source: {}]", widgetPath, url);
		}
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
					ScanArrayContainer("_root.WidgetContainer", widgetContainer, containerCount, changes);
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
	static std::unordered_set<std::string> vanillaPaths;

	for (const auto& def : HUDElements::Get()) {
		if (strcmp(def.id, "iMode_StealthMeter") == 0) {
			for (const auto& path : def.paths) {
				vanillaPaths.insert(path);
			}
			continue;
		}

		bool isHealth = (strcmp(def.id, "iMode_Health") == 0);
		bool isMagicka = (strcmp(def.id, "iMode_Magicka") == 0);
		bool isStamina = (strcmp(def.id, "iMode_Stamina") == 0);
		bool isResourceBar = isHealth || isMagicka || isStamina;

		bool isCrosshair = def.isCrosshair;

		for (const auto& path : def.paths) {
			vanillaPaths.insert(path);

			int mode = settings->GetWidgetMode(path);
			RE::GFxValue elem;

			if (!a_movie->GetVariable(&elem, path)) {
				continue;
			}

			RE::GFxValue::DisplayInfo dInfo;
			elem.GetDisplayInfo(&dInfo);

			bool shouldBeVisible = true;
			double targetAlpha = a_globalAlpha;

			if (a_hideAll || mode == Settings::kHidden) {
				shouldBeVisible = false;
				targetAlpha = 0.0;
			} else if (mode == Settings::kIgnored) {
				shouldBeVisible = true;
				targetAlpha = 100.0;
			} else {  // Immersive
				if (isCrosshair) {
					float ctxBased = (_ctxAlpha * 0.01f * 100.0f);
					if (CompatibilityCheck_SmoothCam() && ctxBased > 0.01f) {
						ctxBased = 0.01f;
					}
					targetAlpha = ctxBased;
					shouldBeVisible = (targetAlpha > 0.0);
				} else {
					shouldBeVisible = (a_globalAlpha > 0.0);
				}
			}

			// Apply to Parent
			dInfo.SetVisible(shouldBeVisible);
			dInfo.SetAlpha(targetAlpha)
			elem.SetDisplayInfo(dInfo);

			// Fix for Vanilla hiding meters when full
			// Force child to be visible, parent's alpha controls the overall fade
			if (isResourceBar && shouldBeVisible) {
				const char* childName = nullptr;
				if (isHealth)
					childName = "HealthMeter_mc";
				else if (isMagicka)
					childName = "MagickaMeter_mc";
				else if (isStamina)
					childName = "StaminaMeter_mc";

				if (childName) {
					EnforceChildMeterVisible(elem, childName);
				}
			}
		}
	}

	const auto& pathSet = settings->GetSubWidgetPaths();
	for (const auto& path : pathSet) {
		if (vanillaPaths.contains(path) ||
			path == "_root.HUDMovieBaseInstance.StealthMeterInstance") {
			continue;
		}
		if (path.find("markerData") != std::string::npos ||
			path.find("widgetLoaderContainer") != std::string::npos) {
			continue;
		}

		int mode = settings->GetWidgetMode(path);
		RE::GFxValue elem;

		if (!a_movie->GetVariable(&elem, path.c_str())) {
			continue;
		}

		RE::GFxValue::DisplayInfo dInfo;

		if (a_hideAll || mode == Settings::kHidden) {
			dInfo.SetVisible(false);
			dInfo.SetAlpha(0.0);
		} else if (mode == Settings::kIgnored) {
			dInfo.SetVisible(true);
			dInfo.SetAlpha(100.0);
		} else {
			RE::GFxValue::DisplayInfo currentInfo;
			elem.GetDisplayInfo(&currentInfo);
			if (currentInfo.GetVisible()) {
				dInfo.SetAlpha(a_globalAlpha);
			}
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

	bool shouldHideAll = ShouldHideHUD();
	bool tdmActive = CompatibilityCheck_TDM();

	for (auto& [name, entry] : ui->menuMap) {
		if (!entry.menu || !entry.menu->uiMovie) {
			continue;
		}

		std::string menuNameStr(name.c_str());
		if (menuNameStr == "HUD Menu") {
			ApplyHUDMenuSpecifics(entry.menu->uiMovie, a_alpha, shouldHideAll);
			continue;
		}

		if (Utils::IsSystemMenu(menuNameStr)) {
			continue;
		}

		int mode = settings->GetWidgetMode(menuNameStr);
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

		if (mode == Settings::kIgnored) {
			if (!entry.menu->uiMovie->GetVisible()) {
				entry.menu->uiMovie->SetVisible(true);
			}
			dInfo.SetAlpha(100.0);
			root.SetDisplayInfo(dInfo);
			continue;
		}

		if (mode == Settings::kHidden) {
			entry.menu->uiMovie->SetVisible(false);
		} else {
			if (!entry.menu->uiMovie->GetVisible()) {
				entry.menu->uiMovie->SetVisible(true);
			}

			if (menuNameStr == "TrueHUD") {
				if (tdmActive) {
					dInfo.SetAlpha(100.0);
				} else {
					dInfo.SetAlpha(a_alpha);
				}
			} else {
				dInfo.SetAlpha(a_alpha);
			}
			root.SetDisplayInfo(dInfo);
		}
	}
}
