#include "PCH.h"

#include "Settings.h"

#include "utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

#include <Windows.h>

namespace settings
{
	namespace
	{
		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		std::string Trim(const std::string& a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) { return {}; }
			return a_text.substr(first, a_text.find_last_not_of(" \t\r\n") - first + 1);
		}

		void ReadUInt(const std::map<std::string, std::string>& a_keys, const char* a_key, std::atomic<std::uint32_t>& a_out)
		{
			const auto it = a_keys.find(a_key);
			if (it == a_keys.end()) { return; }
			try { a_out = static_cast<std::uint32_t>(std::stoul(it->second, nullptr, 0)); }
			catch (...) { logger::warn("INI {}: \"{}\" is not a number; the default stays", a_key, it->second); }
		}

		void ReadInt(const std::map<std::string, std::string>& a_keys, const char* a_key, std::atomic<int>& a_out, int a_min, int a_max)
		{
			const auto it = a_keys.find(a_key);
			if (it == a_keys.end()) { return; }
			try
			{
				const int value = std::stoi(it->second, nullptr, 0);
				if (value >= a_min && value <= a_max) { a_out = value; return; }
			}
			catch (...)
			{
			}
			logger::warn("INI {}: \"{}\" is not a number from {} to {}; the default stays", a_key, it->second, a_min, a_max);
		}
	}

	std::map<std::string, std::string> ReadIni(const std::string& a_path)
	{
		std::map<std::string, std::string> out;
		std::ifstream file(a_path);
		std::string section;
		for (std::string raw; std::getline(file, raw);)
		{
			const std::string line = Trim(raw);
			if (line.empty() || line[0] == ';' || line[0] == '#') { continue; }
			if (line.front() == '[' && line.back() == ']')
			{
				section = Lower(line.substr(1, line.size() - 2));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) { continue; }
			out[Lower(Trim(line.substr(0, eq))) + ":" + section] = Trim(line.substr(eq + 1));
		}
		return out;
	}

	void Load()
	{
		const auto path = (std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "WorldMapMarkerFilter.ini").string();
		if (!std::filesystem::exists(path))
		{
			logger::warn("{} is not there; the compiled defaults are in force", path);
			return;
		}
		const auto keys = ReadIni(path);
		ReadUInt(keys, "uloglevel:debug", logLevel);
		ReadUInt(keys, "itogglekey:keys", toggleKey);
		ReadUInt(keys, "itogglepad:keys", togglePad);
		ReadInt(keys, "iwidthpercent:panel", widthPercent, 15, 40);
		ReadInt(keys, "itextscalepercent:panel", textScalePercent, 80, 160);
		ReadInt(keys, "bshowkeyhint:panel", showKeyHint, 0, 1);
		logger::info("settings: {} read (key 0x{:02X}, button 0x{:04X}, width {}%, text {}%)", path, toggleKey.load(), togglePad.load(),
			widthPercent.load(), textScalePercent.load());
	}

	void ApplyLogLevel()
	{
		const auto level = logLevel.load();
		SKSE::log::set_level(static_cast<spdlog::level::level_enum>(level <= 6 ? level : 2), spdlog::level::info);
	}

	void ApplyFrameworkReservedKeys()
	{
		HMODULE framework = nullptr;
		for (const wchar_t* name : { L"!ApocryphaMenuFramework", L"ApocryphaMenuFramework", L"SKSEMenuFramework" })
		{
			if ((framework = GetModuleHandleW(name))) { break; }
		}
		if (!framework)
		{
			logger::debug("no menu framework loaded; no reserved keys to avoid");
			return;
		}
		using func_t = std::uint32_t (*)(std::int32_t*, std::uint32_t);
		const auto func = reinterpret_cast<func_t>(GetProcAddress(framework, "SMF_GetReservedKeyCodes"));
		if (!func)
		{
			logger::debug("the menu framework does not export SMF_GetReservedKeyCodes; no reserved keys to avoid");
			return;
		}
		std::vector<std::int32_t> reserved(func(nullptr, 0));
		if (!reserved.empty()) { reserved.resize(func(reserved.data(), static_cast<std::uint32_t>(reserved.size()))); }
		logger::debug("the menu framework reserves {} key(s)", reserved.size());
		if (toggleKey != 0 && std::ranges::find(reserved, static_cast<std::int32_t>(toggleKey.load())) != reserved.end())
		{
			logger::warn("iToggleKey 0x{:02X} is reserved by the menu framework; the keyboard toggle is left unbound", toggleKey.load());
			toggleKey = 0;
		}
	}
}
