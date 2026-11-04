// Map Marker List - own code, GPL-3.0-or-later. A searchable list of the world map's markers, down the left of the map, on a
// toggle. See include/Panel.h and source/Panel.cpp.
#include "PCH.h"

#include "DevBenchTool.h"
#include "Panel.h"
#include "Settings.h"
#include "Strings.h"

#include "utils/AddressLibraryGuard.h"
#include "utils/Logger.h"

namespace
{
	// Only the world map's close matters: the panel closes with it. Nothing runs per frame for this.
	class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && !a_event->opening && a_event->menuName == RE::MapMenu::MENU_NAME) { panel::OnMapMenuClosed(); }
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) { return; }
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kPostLoad:
			DevBenchTool::Init(false);
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			panel::Register();
			break;
		case SKSE::MessagingInterface::kInputLoaded:
			settings::ApplyFrameworkReservedKeys();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			mml::Strings::Load();
			DevBenchTool::Init(true);
			if (auto* ui = RE::UI::GetSingleton())
			{
				static MenuSink sink;
				ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
			}
			else
			{
				logger::error("no UI singleton at data loaded; the panel will not see the world map close");
			}
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::log::init("MapMarkerList");
	// Address Library pre-check, BEFORE SKSE::Init, which opens the Address Library itself: a missing file gets a message
	// naming it and the plugin loads inert instead of CommonLibSSE-NG's bare failure line.
	if (!AddressLibraryGuard::Guard("Map Marker List")) { return true; }
	SKSE::Init(a_skse);

	settings::Load();
	settings::ApplyLogLevel();
	SKSE::log::describe_level("MapMarkerList.ini");
	logger::info("Map Marker List {} loading", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler))
	{
		logger::error("could not register the SKSE message listener; the panel will not work");
		return true;
	}
	return true;
}
