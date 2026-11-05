#include "PCH.h"

#include "DevBenchTool.h"

#include "DevBench/DevBenchAPI.h"
#include "Panel.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <format>
#include <string>
#include <string_view>

// DevBench tool "wmmf.control" (rules 31 and 64). A real test still presses the real key or button; these prove the panel,
// not the input path.
//   {}                      state; names=N adds that many of the rows the filter leaves, with their icon frames
//   {"op":"open"}           asks for the panel through the same gate a press uses (the world map must be open)
//   {"op":"close"}          closes it
//   {"op":"nav","rows":N}   moves the highlight
//   {"op":"select"}         chooses the highlighted row: centres the map and closes
//   {"op":"type","text":T}  adds T to the search; {"op":"backspace"}, {"op":"clear"}
//   {"op":"grid"}           opens or closes the on-screen keyboard; {"op":"gridmove","dx":N,"dy":N}, {"op":"gridpress"}
namespace DevBenchTool
{
	namespace
	{
		// The value of a top-level JSON member, as text: a quoted string or a bare word. Empty when absent.
		std::string Get(std::string_view a_json, const char* a_name)
		{
			const std::string key = std::format("\"{}\"", a_name);
			auto pos = a_json.find(key);
			if (pos == std::string_view::npos) { return {}; }
			pos = a_json.find(':', pos + key.size());
			if (pos == std::string_view::npos) { return {}; }
			++pos;
			while (pos < a_json.size() && (a_json[pos] == ' ' || a_json[pos] == '\t')) { ++pos; }
			if (pos >= a_json.size()) { return {}; }
			if (a_json[pos] == '"')
			{
				const auto close = a_json.find('"', pos + 1);
				return close == std::string_view::npos ? std::string{} : std::string(a_json.substr(pos + 1, close - pos - 1));
			}
			std::string out;
			while (pos < a_json.size() && a_json[pos] != ',' && a_json[pos] != '}' && a_json[pos] != ' ') { out += a_json[pos++]; }
			return out;
		}

		int GetInt(std::string_view a_json, const char* a_name, int a_fallback)
		{
			const std::string text = Get(a_json, a_name);
			if (text.empty()) { return a_fallback; }
			try { return std::stoi(text); }
			catch (...) { return a_fallback; }
		}

		void ControlTool(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			if (!a_write) { return; }
			const std::string_view args = a_args ? a_args : "";
			const std::string op = Get(args, "op");
			if (op == "open") { panel::DriveOpen(); }
			else if (op == "close") { panel::DriveClose(); }
			else if (op == "nav") { panel::DriveNav(GetInt(args, "rows", 1)); }
			else if (op == "select") { panel::DriveSelect(); }
			else if (op == "type") { panel::DriveType(Get(args, "text")); }
			else if (op == "backspace") { panel::DriveBackspace(); }
			else if (op == "clear") { panel::DriveClear(); }
			else if (op == "grid") { panel::DriveGridToggle(); }
			else if (op == "gridmove") { panel::DriveGridMove(GetInt(args, "dx", 0), GetInt(args, "dy", 0)); }
			else if (op == "gridpress") { panel::DriveGridPress(); }
			else if (op == "menu") { panel::DriveMenuToggle(); }
			else if (op == "menumove") { panel::DriveMenuMove(GetInt(args, "rows", 1)); }
			else if (op == "menupress") { panel::DriveMenuPress(); }
			else if (op == "types") { panel::DriveTypesToggle(); }
			auto* ui = RE::UI::GetSingleton();
			const bool mapOpen = ui && ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
			const std::string out = std::format(R"({{"ok":true,"op":"{}","mapMenuOpen":{},"toggleKey":{},"togglePad":{},"panel":{}}})", op.empty() ? "state" : op, mapOpen,
				settings::toggleKey.load(), settings::togglePad.load(), panel::StateJson(GetInt(args, "names", 0)));
			a_write(a_sink, out.c_str());
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }
		DevBenchAPI::IDevBenchInterface001* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench)
		{
			if (a_lastAttempt) { logger::info("DevBench not detected; skipping the \"wmmf.control\" tool"); }
			return;
		}
		constexpr const char* descriptor =
			"{"
			"\"description\":\"World Map Marker Filter: the panel's live state and driver. op=state (default): open, listed and filtered rows, query, highlight, icon state, rectangle, counters; names=N lists rows. "
			"op=open asks through the same gate a press uses (the world map must be open). op=close. op=nav rows=N. op=select centres the map on the highlighted row and closes. "
			"op=type text=T, op=backspace, op=clear edit the search. op=grid toggles the on-screen keyboard, op=gridmove dx dy, op=gridpress types the highlighted key. op=menu toggles the highlighted row's menu, op=menumove rows=N, op=menupress does its highlighted item. op=types toggles the marker types view (op=select hides or shows the highlighted type).\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"rows\":{\"type\":\"integer\"},\"text\":{\"type\":\"string\"},\"names\":{\"type\":\"integer\"},\"dx\":{\"type\":\"integer\"},\"dy\":{\"type\":\"integer\"}}},"
			"\"readOnly\":false"
			"}";
		if (devBench->RegisterTool("wmmf.control", descriptor, &ControlTool, nullptr))
		{
			logger::info("registered \"wmmf.control\" with DevBench (build {})", devBench->GetBuildNumber());
			registered = true;
		}
	}
}
