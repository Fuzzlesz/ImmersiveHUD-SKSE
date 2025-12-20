#include "Compat.h"
#include "HUDManager.h"
#include "MCMGen.h"
#include "PCH.h"
#include "Settings.h"

void OnInit(SKSE::MessagingInterface::Message* a_msg)
{
	auto compat = Compat::GetSingleton();

	switch (a_msg->type) {
	case SKSE::MessagingInterface::kPostLoad:
		if (!SmoothCamAPI::RegisterInterfaceLoaderCallback(SKSE::GetMessagingInterface(),
				[](void* interfaceInstance, SmoothCamAPI::InterfaceVersion interfaceVersion) {
					if (interfaceVersion == SmoothCamAPI::InterfaceVersion::V3) {
						Compat::GetSingleton()->g_SmoothCam = reinterpret_cast<SmoothCamAPI::IVSmoothCam3*>(interfaceInstance);
						logger::info("Obtained SmoothCam API");
					} else {
						logger::error("Unable to acquire requested SmoothCam API interface version");
					}
				})) {
			logger::warn("SmoothCamAPI::RegisterInterfaceLoaderCallback reported an error");
		}

		compat->g_TDM = reinterpret_cast<TDM_API::IVTDM2*>(TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V2));
		if (compat->g_TDM) {
			logger::info("Obtained TDM API");
		}

		compat->g_BTPS = reinterpret_cast<BTPS_API_decl::API_V0*>(BTPS_API_decl::RequestPluginAPI_V0());
		if (compat->g_BTPS) {
			logger::info("Obtained BTPS API");
		}

		compat->g_DetectionMeter = LoadLibraryA("Data/SKSE/Plugins/MaxsuDetectionMeter.dll");
		if (compat->g_DetectionMeter) {
			logger::info("Obtained Detection Meter DLL");
		}
		break;

	case SKSE::MessagingInterface::kPostPostLoad:
		if (!SmoothCamAPI::RequestInterface(SKSE::GetMessagingInterface(), SmoothCamAPI::InterfaceVersion::V3)) {
			logger::warn("SmoothCamAPI::RequestInterface reported an error");
		}
		break;

	case SKSE::MessagingInterface::kDataLoaded:
		HUDManager::GetSingleton()->InstallHooks();
		compat->InitExternalData();
		break;

	case SKSE::MessagingInterface::kPostLoadGame:
		HUDManager::GetSingleton()->ScanIfReady();
		HUDManager::GetSingleton()->Reset();
		break;
	}
}

#ifdef SKYRIM_AE
extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(Version::MAJOR);
	v.PluginName("ImmersiveHUD");
	v.AuthorName("Fuzzles");
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	v.CompatibleVersions({ SKSE::RUNTIME_LATEST });
	return v;
}();
#else
extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "ImmersiveHUD";
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver < SKSE::RUNTIME_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}
#endif

void InitializeLog()
{
	auto path = logger::log_directory();
	if (!path) {
		stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%H:%M:%S] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();

	logger::info("Game version : {}", a_skse->RuntimeVersion().string());

	SKSE::Init(a_skse);

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener(OnInit)) {
		return false;
	}

	return true;
}
