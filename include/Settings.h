#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>

// The mod's settings, read once from SKSE\Plugins\WorldMapMarkerFilter.ini when the plugin loads. There is no settings page: the
// mod's only settings are its two keys and the panel's size. Compiled defaults match the shipped INI (rule 16) and the
// keys are in DEFAULT-KEYS.md.
namespace settings
{
	struct Defaults
	{
		static constexpr std::uint32_t logLevel = 2;
		static constexpr std::uint32_t toggleKey = 0x35;    // DirectInput scan code: /
		static constexpr std::uint32_t togglePad = 0x0004;  // XInput mask: D-pad left
		static constexpr int widthPercent = 22;
		static constexpr int textScalePercent = 100;
		static constexpr int showKeyHint = 1;
	};

	inline std::atomic<std::uint32_t> logLevel{ Defaults::logLevel };    // uLogLevel:Debug
	inline std::atomic<std::uint32_t> toggleKey{ Defaults::toggleKey };  // iToggleKey:Keys, 0 = off
	inline std::atomic<std::uint32_t> togglePad{ Defaults::togglePad };  // iTogglePad:Keys, 0 = off
	inline std::atomic<int> widthPercent{ Defaults::widthPercent };      // iWidthPercent:Panel, 15 to 40
	inline std::atomic<int> textScalePercent{ Defaults::textScalePercent };  // iTextScalePercent:Panel, 80 to 160

	inline std::atomic<int> showKeyHint{ Defaults::showKeyHint };  // bShowKeyHint:Panel, 0 or 1

	void Load();
	void ApplyLogLevel();

	// The framework's reserved keys (SMF_GetReservedKeyCodes): a toggle key it holds is left unbound, with a warning.
	void ApplyFrameworkReservedKeys();

	// A plain INI's keys as "key:section" in lower case. The panel's style file uses it too.
	std::map<std::string, std::string> ReadIni(const std::string& a_path);
}
