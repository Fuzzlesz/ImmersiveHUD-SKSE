#pragma once

#include "HUDManager.h"
#include "PCH.h"
#include "Settings.h"
#include "Utils.h"

	namespace Events
{
	class InputEventSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputEventSink* GetSingleton()
		{
			static InputEventSink singleton;
			return &singleton;
		}

		static void Register()
		{
			if (auto* deviceManager = RE::BSInputDeviceManager::GetSingleton()) {
				deviceManager->AddEventSink(GetSingleton());
				logger::info("Registered Input Event Sink");
			}
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, [[maybe_unused]] RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override
		{
			if (!a_event || !*a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto settings = Settings::GetSingleton();
			auto key = settings->GetToggleKey();

			if (key == static_cast<std::uint32_t>(-1) || key == 0) {
				return RE::BSEventNotifyControl::kContinue;
			}

			for (auto event = *a_event; event; event = event->next) {
				if (auto button = event->AsButtonEvent()) {
					auto idCode = button->GetIDCode();
					auto device = button->GetDevice();

					// Normalize Gamepad Input (Raw XInput Bitmask -> SkyUI Key Code)
					if (device == RE::INPUT_DEVICE::kGamepad) {
						idCode = RemapGamepadCode(idCode);
					}

					if (idCode == key) {
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

	private:
		// Helper to map XInput driver masks to SkyUI integer codes
		static std::uint32_t RemapGamepadCode(std::uint32_t a_rawCode)
		{
			using namespace REX::W32;

			switch (a_rawCode) {
			case XINPUT_GAMEPAD_DPAD_UP:
				return 266;
			case XINPUT_GAMEPAD_DPAD_DOWN:
				return 267;
			case XINPUT_GAMEPAD_DPAD_LEFT:
				return 268;
			case XINPUT_GAMEPAD_DPAD_RIGHT:
				return 269;
			case XINPUT_GAMEPAD_START:
				return 270;
			case XINPUT_GAMEPAD_BACK:
				return 271;
			case XINPUT_GAMEPAD_LEFT_THUMB:
				return 272;
			case XINPUT_GAMEPAD_RIGHT_THUMB:
				return 273;
			case XINPUT_GAMEPAD_LEFT_SHOULDER:
				return 274;
			case XINPUT_GAMEPAD_RIGHT_SHOULDER:
				return 275;
			case XINPUT_GAMEPAD_A:
				return 276;
			case XINPUT_GAMEPAD_B:
				return 277;
			case XINPUT_GAMEPAD_X:
				return 278;
			case XINPUT_GAMEPAD_Y:
				return 279;
			case 0x9:
				return 280;  // LT
			case 0xA:
				return 281;  // RT
			default:
				return a_rawCode;
			}
		}

		InputEventSink() = default;
		InputEventSink(const InputEventSink&) = delete;
		InputEventSink(InputEventSink&&) = delete;
		~InputEventSink() = default;
	};

	class MenuOpenCloseEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static MenuOpenCloseEventSink* GetSingleton()
		{
			static MenuOpenCloseEventSink singleton;
			return &singleton;
		}

		static void Register()
		{
			if (auto* ui = RE::UI::GetSingleton()) {
				ui->AddEventSink(GetSingleton());
				logger::info("Registered Menu Open/Close Event Sink");
			}
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const char* menuName = a_event->menuName.c_str();

			if (!a_event->opening) {
				// Capture when menus close to refresh HUD state/reload settings
				if (Utils::IsSystemMenu(menuName)) {
					HUDManager::GetSingleton()->Reset(false);
				}
			}

			if (a_event->opening) {
				// 1. Snap HUD hidden immediately if a system menu opens
				if (Utils::IsSystemMenu(menuName)) {
					HUDManager::GetSingleton()->Reset(false);
				}
				// 2. Force scan when HUD opens
				else if (a_event->menuName == RE::HUDMenu::MENU_NAME) {
					HUDManager::GetSingleton()->ScanIfReady();
				}
				// 3. Catch widgets appearing late
				else {
					HUDManager::GetSingleton()->RegisterNewMenu();
				}
			}

			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		MenuOpenCloseEventSink() = default;
		MenuOpenCloseEventSink(const MenuOpenCloseEventSink&) = delete;
		MenuOpenCloseEventSink(MenuOpenCloseEventSink&&) = delete;
		~MenuOpenCloseEventSink() = default;
	};
}
