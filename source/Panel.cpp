// The panel. Own code, GPL-3.0-or-later (2026-11-04). See include/Panel.h.
//
// WHERE THE MARKERS COME FROM. SkyUI's map movie (and everything built on it: Norden UI, Dragonborn UI) keeps a clip per
// marker in _root.WorldMap._markerList, in the same order as the game's MapMenu::mapMarkers, so a clip's `index` is the number
// skse.ShowOnMap takes. Its Location Finder lists the same clips: types above 0 and below Map.LocationFinder.TYPE_RANGE
// (60 in SkyUI; CoMAP raises it), named by the clip's _label. This panel lists what that list lists, and leaves out a marker
// whose IconClip another mod has hidden (Legendary Map hides by setting IconClip._visible). A marker the game does not put on
// the map has no clip, so an undiscovered one is listed only if the map shows it, with the icon the map gives it. The clips
// are made ten a frame after the map opens, so the list is read again while that runs.
//
// THE ICONS ARE THE MAP'S OWN. ImGui cannot draw a Scaleform clip, and copying the art out would miss every icon replacer and
// CoMAP's added icons, so the panel is two layers. Under it, in the map movie, a clip of this mod's holds the panel's fill and
// one "LocationListEntry" per visible row: that symbol is the Location Finder's row, its `icon` child is MapMarkerArt, and
// icon.gotoAndStop(marker.iconFrame) is how SkyUI itself shows a marker in a list. Over it, the framework's window draws the
// frame, text and highlight with no background of its own. The framework draws above every Scaleform movie, so the two only
// have to agree on positions: the window publishes the row positions in screen pixels when they change, and a UI task puts
// them in the stage's coordinates (GetVisibleFrameRect is the whole screen in stage units). Clips are found by name on every
// update and no GFxValue is kept, so nothing can outlive the movie. The clip is removed when the panel closes.
//
// THE KEYBOARD. Apocrypha Menu Framework's on-screen keyboard closes itself unless the framework's own window is up
// (Keyboard.cpp, Draw: !renderer::IsMainWindowVisible()), so it cannot serve a mod's window. So the panel has its own: QWERTY,
// then any other character the listed names use, which covers every language, then Space and Delete. On a real keyboard the
// panel asks the engine for its character events (ControlMap::AllowTextInput, as SkyUI's search does) and takes every key while
// open, so typing "c" cannot also fire another mod's hotkey. Both edit one query string.
//
// HIDING AND FILTERING. Y on a row opens a small menu. Hide and "show only" set a marker's IconClip._visible, as Legendary Map
// hides its types, on the map itself. Only a clip this panel hid is ever shown again, so another mod's hidden type stays hidden.
// Hidden markers stay in the list, dimmed, so they can be brought back; they are kept in MapMarkerList_Hidden.txt, as icon frame
// and name, so markers of one name and icon go together. A "show only" lasts until Show all or the game closes.
// RB shows the marker types, as Legendary Map's legend does: A hides or shows every marker of a type. A type is kept as "kind|frame".
//
// THREADS. The input callback and UI tasks run on the game's main thread. Render runs on the render thread inside the
// framework's frame. The marker list is an immutable shared vector swapped under the lock, so nothing is copied per frame.
#include "PCH.h"

#include "SKSEMenuFramework.h"

#include "Fold.h"
#include "Panel.h"
#include "PanelStyle.h"
#include "Settings.h"
#include "Strings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace PanelStyle = mml::PanelStyle;

namespace panel
{
	namespace
	{
		constexpr const char* kClipName = "MapMarkerList_Panel";
		constexpr const char* kRowSymbol = "LocationListEntry";
		constexpr int kClipDepth = 16000;
		constexpr int kDefaultTypeRange = 60;
		constexpr std::size_t kMaxQueryBytes = 96;
		constexpr int kGridRowsShown = 6;
		constexpr int kGridColumns = 10;
		constexpr const char* kKeyboardBase = "1234567890QWERTYUIOPASDFGHJKL'ZXCVBNM-.,";
		constexpr const char* kHiddenFile = "Data/SKSE/Plugins/MapMarkerList_Hidden.txt";
		constexpr int kReadEveryFrames = 30;   // while the map is still making its marker clips

		struct Marker
		{
			int index = 0;       // into MapMenu::mapMarkers, and what skse.ShowOnMap takes
			int frame = 1;       // the clip's iconFrame
			std::string label;
			std::wstring folded;
			std::string key;     // what a hide is kept under: icon frame and name
			bool hidden = false;
		};

		struct Kind
		{
			int frame = 0;
			int markers = 0;
			bool hidden = false;
			std::string name;    // from the map's icon name: CaveMarker is "Cave"
		};

		struct MarkerList
		{
			std::vector<Marker> markers;     // by name
			std::vector<std::string> grid;   // the on-screen keyboard's characters, without its last row (Space, Delete)
			std::vector<Kind> kinds;         // the marker types on this map, by name
		};

		// The panel's life: closed; open (drawn, taking input); draining (it closed, and an invisible window keeps the input
		// until the button that did it is let go, so its release cannot reach the map).
		enum class State : int { kClosed = 0, kOpen = 1, kDraining = 2 };

		SKSEMenuFramework::Model::WindowInterface* window = nullptr;
		std::atomic<int> state{ static_cast<int>(State::kClosed) };

		// ---- the data the main thread hands the render thread (guarded by g_lock) --------------------------------
		std::mutex g_lock;
		std::shared_ptr<const MarkerList> g_list = std::make_shared<MarkerList>();
		std::string g_query;
		std::uint32_t g_queryGeneration = 0;
		std::atomic<std::uint32_t> g_listGeneration{ 0 };
		std::atomic<bool> g_loading{ false };     // the map is still making marker clips
		std::atomic<bool> g_hasList{ false };     // this map movie has _root.WorldMap._markerList
		std::atomic<bool> g_readPosted{ false };

		// What the map shows. g_lock.
		enum class Only : int { kNone = 0, kOne = 1, kKind = 2 };
		std::set<std::string> g_hidden;
		Only g_only = Only::kNone;
		std::string g_onlyKey;
		int g_onlyFrame = 0;

		// ---- input to the render thread ---------------------------------------------------------------------------
		std::atomic<int> pendingNavigate{ 0 };
		std::atomic<int> pendingGridX{ 0 }, pendingGridY{ 0 };
		std::atomic<bool> pendingSelect{ false }, pendingCancel{ false }, pendingGridPress{ false };
		std::atomic<bool> gridOpen{ false };
		std::atomic<bool> menuOpen{ false };
		std::atomic<bool> typesOpen{ false };
		std::atomic<int> pendingMenuMove{ 0 };
		std::atomic<bool> pendingMenuPress{ false };
		std::atomic<bool> lastDeviceGamepad{ false };
		enum : std::uint32_t { kHeldConfirm = 1, kHeldCancel = 2 };
		std::atomic<std::uint32_t> heldButtons{ 0 };

		// ---- the render thread's own --------------------------------------------------------------------------------
		std::shared_ptr<const MarkerList> r_list;
		std::vector<int> r_filtered;
		std::uint32_t r_listGeneration = ~0u;
		std::uint32_t r_queryGeneration = ~0u;
		std::string r_query;
		int r_highlight = 0;
		int r_top = 0;
		int r_gridRow = 0, r_gridCol = 0, r_gridTop = 0;
		Only r_only = Only::kNone;
		std::string r_onlyKey;
		int r_onlyFrame = 0;
		bool r_typesShown = false;
		bool r_menuHasTarget = false;
		int r_menuRow = 0;
		int r_menuFrame = 0;
		bool r_menuHidden = false;
		std::string r_menuKey, r_menuLabel;
		int r_readCountdown = 0;
		int r_drainFrames = 0;
		std::uint32_t r_opens = 0;
		int r_framesOpen = 0;

		// ---- the map movie's layer, published by the render thread, applied by a UI task ------------------------------
		struct Slot
		{
			float centreY = 0.0f;
			int frame = 0;
			bool operator==(const Slot&) const = default;
		};
		struct Layout
		{
			float screenW = 0, screenH = 0;
			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			float r = 0, g = 0, b = 0, a = 0;
			float iconX = 0, iconSize = 0;
			std::vector<Slot> slots;
			bool operator==(const Layout&) const = default;
		};
		std::mutex g_layoutLock;
		Layout g_layout;                         // the latest wanted
		std::atomic<bool> g_layoutPosted{ false };
		Layout r_lastLayout;                     // render thread: what it last published
		Layout m_appliedBack;                    // main thread: the fill last drawn (only the rect and colour are compared)
		std::vector<int> m_slotFrame;            // main thread: the frame each row's icon is on
		bool m_iconsMissingLogged = false;       // main thread
		std::atomic<bool> g_iconsOk{ false };
		std::atomic<int> g_iconSlots{ 0 };
		std::unordered_set<int> m_hiddenClips;   // main thread: the clips this panel hid, by place in _markerList
		int m_watchFrames = 0;                   // main thread: the wait for a newly opened map's clips
		std::atomic<int> g_hiddenClipCount{ 0 };

		// ---- what the running game had (rule 30), for the state --------------------------------------------------------
		std::atomic<int> g_clipCount{ 0 }, g_typeRange{ kDefaultTypeRange };
		std::atomic<std::uint32_t> opens{ 0 }, selections{ 0 }, showOnMapCalls{ 0 };
		std::atomic<float> lastX{ 0 }, lastY{ 0 }, lastW{ 0 }, lastH{ 0 };
		std::atomic<int> lastHighlight{ 0 }, lastTop{ 0 }, lastFiltered{ 0 }, lastVisible{ 0 }, lastGridChars{ 0 };

		bool g_textInputHeld = false;            // main thread: this panel's one AllowTextInput(true)

		// Every export Render and the panel call. The framework header does not null-check, so a missing export would be a
		// jump to address zero (past-mistakes library 3876): refuse the panel instead.
		bool HasRequiredExports()
		{
			constexpr const char* required[] = {
				"AddWindow", "RegisterInpoutEvent", "IsAnyBlockingWindowOpened", "igBegin", "igEnd", "igSelectable_Bool",
				"igSetNextWindowPos", "igSetNextWindowSize", "igGetMainViewport", "igGetForegroundDrawList_Nil", "igGetIO",
				"igIsMouseDown_Nil", "igGetStyle", "igPushStyleColor_Vec4", "igPopStyleColor", "igTextWrappedV", "igSetWindowFontScale",
				"igIsItemHovered", "igTextV", "igSeparator", "igGetTextLineHeightWithSpacing", "igGetTextLineHeight", "igGetFrameHeight",
				"igGetCursorScreenPos", "igSetCursorScreenPos", "igGetContentRegionAvail", "igGetWindowDrawList", "igSameLine", "igDummy",
				"igCalcTextSize", "igIsWindowHovered", "igGetColorU32_Col", "igTextUnformatted", "ImDrawList_AddRectFilled", "ImDrawList_AddText_Vec2"
			};
			for (const char* name : required)
			{
				if (!GetMenuFrameworkFunction<void*>(name))
				{
					logger::warn("the menu framework does not export \"{}\"; the panel is off", name);
					return false;
				}
			}
			return true;
		}

		using DrawThemeFrameFn = bool (*)(void*, float, float, float, float);

		void RunUI(std::function<void()> a_task)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddUITask(std::move(a_task)); }
			else { logger::error("no SKSE task interface; a panel task was dropped"); }
		}

		RE::GPtr<RE::GFxMovieView> MapMovie()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui || !ui->IsMenuOpen(RE::MapMenu::MENU_NAME)) { return nullptr; }
			return ui->GetMovieView(RE::MapMenu::MENU_NAME);
		}

		double NumberMember(const RE::GFxValue& a_object, const char* a_name, double a_fallback)
		{
			RE::GFxValue value;
			return a_object.GetMember(a_name, &value) && value.IsNumber() ? value.GetNumber() : a_fallback;
		}

		// ---- the map movie, UI tasks only ------------------------------------------------------------------------------

		std::string MarkerKey(int a_frame, std::string_view a_label) { return std::format("{}|{}", a_frame, a_label); }
		std::string KindKey(int a_frame) { return std::format("kind|{}", a_frame); }

		// CaveMarker -> Cave, DwemerRuinMarker -> Dwemer Ruin.
		std::string KindName(std::string_view a_iconName)
		{
			if (a_iconName.ends_with("Marker")) { a_iconName.remove_suffix(6); }
			std::string out;
			for (const char c : a_iconName)
			{
				if (!out.empty() && c >= 'A' && c <= 'Z') { out += ' '; }
				out += c;
			}
			return out;
		}

		void LoadHidden()
		{
			std::ifstream in(kHiddenFile, std::ios::binary);
			std::string line;
			std::scoped_lock lock(g_lock);
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') { line.pop_back(); }
				if (line.find('|') != std::string::npos) { g_hidden.insert(line); }
			}
			logger::info("{} marker name(s) hidden, from {}", g_hidden.size(), kHiddenFile);
		}

		void SaveHidden()
		{
			std::set<std::string> copy;
			{
				std::scoped_lock lock(g_lock);
				copy = g_hidden;
			}
			std::ofstream out(kHiddenFile, std::ios::binary | std::ios::trunc);
			for (const auto& line : copy) { out << line << '\n'; }
			if (!out) { logger::warn("could not write {}; the hidden markers last until the game closes", kHiddenFile); }
		}

		// Is this clip a place the Location Finder would list? Reads its label and frame when it is.
		bool ReadLocation(RE::GFxValue& a_clip, int a_typeRange, std::string& a_label, int& a_frame)
		{
			const int type = static_cast<int>(NumberMember(a_clip, "markerType", 0.0));
			if (type <= 0 || type >= a_typeRange) { return false; }
			// Where CoMAP has raised the range, the quest targets, doors and the player's own marker are still not locations.
			RE::GFxValue iconName;
			if (a_clip.GetMember("_iconName", &iconName) && iconName.IsString() && iconName.GetString())
			{
				const std::string_view name = iconName.GetString();
				if (name == "EmptyMarker" || name == "DoorMarker" || name == "QuestTargetMarker" || name == "QuestTargetDoorMarker" ||
					name == "MultipleQuestTargetMarker" || name == "PlayerSetMarker" || name == "YouAreHereMarker")
				{
					return false;
				}
			}
			RE::GFxValue label;
			if ((!a_clip.GetMember("_label", &label) || !label.IsString()) && (!a_clip.GetMember("label", &label) || !label.IsString())) { return false; }
			const char* text = label.GetString();
			if (!text || !*text) { return false; }
			a_label = text;
			a_frame = static_cast<int>(NumberMember(a_clip, "iconFrame", type + 1.0));
			return true;
		}

		int TypeRange(RE::GFxMovieView* a_movie)
		{
			// CoMAP raises the finder's range to take in the icons it adds, so it is asked, not assumed.
			RE::GFxValue range;
			return a_movie->GetVariable(&range, "_global.Map.LocationFinder.TYPE_RANGE") && range.IsNumber() ? static_cast<int>(range.GetNumber()) : kDefaultTypeRange;
		}

		// Puts the hides and the "show only" on the map's clips. Returns how many clips the map has made. Main thread.
		int ApplyVisibility()
		{
			auto movie = MapMovie();
			RE::GFxValue worldMap, clips;
			if (!movie || !movie->GetVariable(&worldMap, "_root.WorldMap") || !worldMap.IsObject() || !worldMap.GetMember("_markerList", &clips) || !clips.IsArray()) { return 0; }

			std::set<std::string> hidden;
			Only only;
			std::string onlyKey;
			int onlyFrame;
			{
				std::scoped_lock lock(g_lock);
				hidden = g_hidden;
				only = g_only;
				onlyKey = g_onlyKey;
				onlyFrame = g_onlyFrame;
			}

			const int typeRange = TypeRange(movie.get());
			const auto size = clips.GetArraySize();
			int made = 0;
			for (std::uint32_t i = 0; i < size; ++i)
			{
				RE::GFxValue clip;
				if (!clips.GetElement(i, &clip) || !clip.IsDisplayObject()) { continue; }
				++made;
				std::string label;
				int frame = 0;
				if (!ReadLocation(clip, typeRange, label, frame)) { continue; }
				const auto key = MarkerKey(frame, label);
				const bool hide = hidden.contains(key) || hidden.contains(KindKey(frame)) || (only == Only::kOne && key != onlyKey) || (only == Only::kKind && frame != onlyFrame);
				RE::GFxValue icon, shown;
				if (!clip.GetMember("IconClip", &icon) || !icon.IsDisplayObject()) { continue; }
				const int place = static_cast<int>(i);
				if (hide)
				{
					if (icon.GetMember("_visible", &shown) && shown.IsBool() && shown.GetBool())
					{
						icon.SetMember("_visible", RE::GFxValue(false));
						m_hiddenClips.insert(place);
					}
				}
				else if (m_hiddenClips.erase(place) > 0)
				{
					icon.SetMember("_visible", RE::GFxValue(true));
				}
			}
			g_hiddenClipCount = static_cast<int>(m_hiddenClips.size());
			return made;
		}

		// A newly opened map makes its clips ten a frame, so the hides go on as they appear, every few frames until it is done.
		void WatchNewMap()
		{
			{
				std::scoped_lock lock(g_lock);
				if (g_hidden.empty() && g_only == Only::kNone) { return; }
			}
			auto* ui = RE::UI::GetSingleton();
			const int frame = ++m_watchFrames;
			if (!ui || frame > 1800 || (frame > 120 && !ui->IsMenuOpen(RE::MapMenu::MENU_NAME))) { return; }
			if (frame % 10 == 0)
			{
				const int made = ApplyVisibility();
				auto movie = MapMovie();
				RE::GFxValue worldMap;
				if (made > 0 && movie && movie->GetVariable(&worldMap, "_root.WorldMap") && worldMap.IsObject() && NumberMember(worldMap, "_nextCreateIndex", -1.0) == -1.0)
				{
					logger::debug("the new map's {} clips are made; {} hidden", made, g_hiddenClipCount.load());
					return;
				}
			}
			RunUI(WatchNewMap);
		}

		// Reads the marker clips. Main thread.
		void ReadMarkers()
		{
			g_readPosted = false;
			if (static_cast<State>(state.load()) != State::kOpen) { return; }
			auto movie = MapMovie();
			if (!movie)
			{
				logger::debug("marker read: the map movie is not there");
				return;
			}
			RE::GFxValue worldMap, clips;
			if (!movie->GetVariable(&worldMap, "_root.WorldMap") || !worldMap.IsObject() || !worldMap.GetMember("_markerList", &clips) || !clips.IsArray())
			{
				static bool logged = false;
				if (!logged)
				{
					logged = true;
					logger::warn("this map movie has no _root.WorldMap._markerList, so its markers cannot be listed (SkyUI's map is needed)");
				}
				g_hasList = false;
				g_loading = false;
				return;
			}
			g_hasList = true;

			const int typeRange = TypeRange(movie.get());
			g_typeRange = typeRange;
			std::set<std::string> hidden;
			{
				std::scoped_lock lock(g_lock);
				hidden = g_hidden;
			}

			auto list = std::make_shared<MarkerList>();
			const auto size = clips.GetArraySize();
			int madeClips = 0;
			std::vector<std::wstring> names;
			for (std::uint32_t i = 0; i < size; ++i)
			{
				RE::GFxValue clip;
				if (!clips.GetElement(i, &clip) || !clip.IsDisplayObject()) { continue; }
				++madeClips;
				Marker marker;
				if (!ReadLocation(clip, typeRange, marker.label, marker.frame)) { continue; }
				// Hidden by another mod: not listed. Hidden by this panel: listed, so it can be brought back.
				RE::GFxValue icon, shown;
				if (clip.GetMember("IconClip", &icon) && icon.IsDisplayObject() && icon.GetMember("_visible", &shown) && shown.IsBool() && !shown.GetBool() && !m_hiddenClips.contains(static_cast<int>(i))) { continue; }
				marker.index = static_cast<int>(NumberMember(clip, "index", static_cast<double>(i)));
				marker.folded = fold::Lower(marker.label);
				marker.key = MarkerKey(marker.frame, marker.label);
				marker.hidden = hidden.contains(marker.key) || hidden.contains(KindKey(marker.frame));
				auto kind = std::ranges::find(list->kinds, marker.frame, &Kind::frame);
				if (kind == list->kinds.end())
				{
					RE::GFxValue iconName;
					Kind fresh;
					fresh.frame = marker.frame;
					fresh.hidden = hidden.contains(KindKey(marker.frame));
					fresh.name = clip.GetMember("_iconName", &iconName) && iconName.IsString() && iconName.GetString() ? KindName(iconName.GetString()) : std::format("Type {}", marker.frame);
					list->kinds.push_back(std::move(fresh));
					kind = list->kinds.end() - 1;
				}
				++kind->markers;
				names.push_back(marker.folded);
				list->markers.push_back(std::move(marker));
			}
			std::ranges::stable_sort(list->markers, [](const Marker& a, const Marker& b) { return a.folded < b.folded; });
			std::ranges::sort(list->kinds, {}, &Kind::name);
			for (const char* c = kKeyboardBase; *c; ++c) { list->grid.emplace_back(1, *c); }
			for (auto& character : fold::Alphabet(names))
			{
				if (std::ranges::find(list->grid, character) == list->grid.end()) { list->grid.push_back(std::move(character)); }
			}

			const bool loading = NumberMember(worldMap, "_nextCreateIndex", -1.0) != -1.0;
			const auto listed = list->markers.size();
			{
				std::scoped_lock lock(g_lock);
				g_list = std::move(list);
			}
			++g_listGeneration;
			g_clipCount = madeClips;
			if (g_loading.exchange(loading) && !loading) { logger::debug("the map finished making its marker clips"); }
			logger::debug("marker read: {} slots, {} clips made, {} listed, type range {}, still making: {}", size, madeClips, listed, typeRange, loading);
		}

		// The panel's clip in the map movie, made when it is not there.
		bool GetPanelClip(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip)
		{
			RE::GFxValue root;
			if (!a_movie->GetVariable(&root, "_root") || !root.IsDisplayObject())
			{
				logger::warn("the map movie has no _root display object");
				return false;
			}
			if (root.GetMember(kClipName, &a_clip) && a_clip.IsDisplayObject()) { return true; }
			if (!root.CreateEmptyMovieClip(&a_clip, kClipName, kClipDepth) || !a_clip.IsDisplayObject())
			{
				logger::warn("could not make the panel's clip in the map movie");
				return false;
			}
			a_clip.SetMember("hitTestDisable", RE::GFxValue(true));   // a GFx extension: the map's own hover and click go on as they did
			return true;
		}

		void DrawFill(RE::GFxValue& a_clip, const Layout& a_layout, const RE::GRectF& a_stage)
		{
			RE::GFxValue fill;
			if (!a_clip.GetMember("fill", &fill) || !fill.IsDisplayObject())
			{
				if (!a_clip.CreateEmptyMovieClip(&fill, "fill", 1) || !fill.IsDisplayObject()) { return; }
			}
			const double sx = (a_stage.right - a_stage.left) / (std::max)(a_layout.screenW, 1.0f);
			const double sy = (a_stage.bottom - a_stage.top) / (std::max)(a_layout.screenH, 1.0f);
			const double x0 = a_stage.left + a_layout.x0 * sx, x1 = a_stage.left + a_layout.x1 * sx;
			const double y0 = a_stage.top + a_layout.y0 * sy, y1 = a_stage.top + a_layout.y1 * sy;
			const auto channel = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
			const double rgb = static_cast<double>((channel(a_layout.r) << 16) | (channel(a_layout.g) << 8) | channel(a_layout.b));
			fill.Invoke("clear");
			fill.Invoke("beginFill", std::array{ RE::GFxValue(rgb), RE::GFxValue(static_cast<double>(a_layout.a) * 100.0) });
			fill.Invoke("moveTo", std::array{ RE::GFxValue(x0), RE::GFxValue(y0) });
			fill.Invoke("lineTo", std::array{ RE::GFxValue(x1), RE::GFxValue(y0) });
			fill.Invoke("lineTo", std::array{ RE::GFxValue(x1), RE::GFxValue(y1) });
			fill.Invoke("lineTo", std::array{ RE::GFxValue(x0), RE::GFxValue(y1) });
			fill.Invoke("lineTo", std::array{ RE::GFxValue(x0), RE::GFxValue(y0) });
			fill.Invoke("endFill");
		}

		// One row's icon: the frame, then sized to the row and centred on the entry's origin (the art's own origin varies).
		void SetIconFrame(RE::GFxValue& a_entry, int a_frame, double a_size)
		{
			RE::GFxValue icon;
			if (!a_entry.GetMember("icon", &icon) || !icon.IsDisplayObject()) { return; }
			icon.Invoke("gotoAndStop", std::array{ RE::GFxValue(static_cast<double>(a_frame)) });
			icon.SetMember("_x", RE::GFxValue(0.0));
			icon.SetMember("_y", RE::GFxValue(0.0));
			icon.SetMember("_xscale", RE::GFxValue(100.0));
			icon.SetMember("_yscale", RE::GFxValue(100.0));
			const double width = NumberMember(icon, "_width", 0.0);
			const double height = NumberMember(icon, "_height", 0.0);
			const double largest = (std::max)(width, height);
			if (largest <= 0.0)
			{
				logger::debug("icon frame {} has no size", a_frame);
				return;
			}
			const double scale = 100.0 * a_size / largest;
			icon.SetMember("_xscale", RE::GFxValue(scale));
			icon.SetMember("_yscale", RE::GFxValue(scale));
			RE::GFxValue bounds;
			if (icon.Invoke("getBounds", &bounds, std::array{ a_entry }) && bounds.IsObject())
			{
				icon.SetMember("_x", RE::GFxValue(-(NumberMember(bounds, "xMin", 0.0) + NumberMember(bounds, "xMax", 0.0)) * 0.5));
				icon.SetMember("_y", RE::GFxValue(-(NumberMember(bounds, "yMin", 0.0) + NumberMember(bounds, "yMax", 0.0)) * 0.5));
			}
		}

		// Puts the published layout into the map movie. Main thread, only when the layout changed.
		void ApplyLayout()
		{
			g_layoutPosted = false;
			if (static_cast<State>(state.load()) != State::kOpen) { return; }
			Layout layout;
			{
				std::scoped_lock lock(g_layoutLock);
				layout = g_layout;
			}
			auto movie = MapMovie();
			RE::GFxValue clip;
			if (!movie || !GetPanelClip(movie.get(), clip)) { return; }
			const RE::GRectF stage = movie->GetVisibleFrameRect();

			const bool fillChanged = layout.x0 != m_appliedBack.x0 || layout.y0 != m_appliedBack.y0 || layout.x1 != m_appliedBack.x1 || layout.y1 != m_appliedBack.y1 ||
			                         layout.r != m_appliedBack.r || layout.g != m_appliedBack.g || layout.b != m_appliedBack.b || layout.a != m_appliedBack.a;
			if (fillChanged)
			{
				DrawFill(clip, layout, stage);
				m_appliedBack = layout;
				logger::debug("panel fill drawn: screen {}x{}, stage ({:.1f},{:.1f})-({:.1f},{:.1f})", layout.screenW, layout.screenH, stage.left, stage.top, stage.right, stage.bottom);
			}

			const double sx = (stage.right - stage.left) / (std::max)(layout.screenW, 1.0f);
			const double sy = (stage.bottom - stage.top) / (std::max)(layout.screenH, 1.0f);
			const double size = layout.iconSize * sy;

			if (m_slotFrame.size() < layout.slots.size()) { m_slotFrame.resize(layout.slots.size(), -1); }
			int attached = 0;
			for (std::size_t i = 0; i < m_slotFrame.size(); ++i)
			{
				const auto name = std::format("r{}", i);
				RE::GFxValue entry;
				const bool exists = clip.GetMember(name.c_str(), &entry) && entry.IsDisplayObject();
				if (i >= layout.slots.size())
				{
					if (exists) { entry.SetMember("_visible", RE::GFxValue(false)); }
					continue;
				}
				if (!exists)
				{
					if (!clip.AttachMovie(&entry, kRowSymbol, name.c_str(), static_cast<std::int32_t>(i) + 2) || !entry.IsDisplayObject())
					{
						// A map movie that is not SkyUI's has no such symbol: the panel still works, with names only.
						if (!m_iconsMissingLogged)
						{
							m_iconsMissingLogged = true;
							logger::warn("the map movie has no \"{}\" symbol; the rows show names without icons", kRowSymbol);
						}
						g_iconsOk = false;
						break;
					}
					for (const char* part : { "background", "selectIndicator", "textField", "triggerArea" })
					{
						RE::GFxValue child;
						if (entry.GetMember(part, &child) && child.IsDisplayObject()) { child.SetMember("_visible", RE::GFxValue(false)); }
					}
					m_slotFrame[i] = -1;
				}
				entry.SetMember("_x", RE::GFxValue(stage.left + layout.iconX * sx));
				entry.SetMember("_y", RE::GFxValue(stage.top + layout.slots[i].centreY * sy));
				entry.SetMember("_visible", RE::GFxValue(true));
				if (m_slotFrame[i] != layout.slots[i].frame)
				{
					SetIconFrame(entry, layout.slots[i].frame, size);
					m_slotFrame[i] = layout.slots[i].frame;
				}
				++attached;
			}
			if (attached == static_cast<int>(layout.slots.size())) { g_iconsOk = true; }
			g_iconSlots = attached;
		}

		// Everything the panel put in the map movie goes. Main thread.
		void RemoveMovieLayer()
		{
			m_slotFrame.clear();
			m_appliedBack = Layout{};
			auto movie = MapMovie();
			if (!movie) { return; }
			RE::GFxValue root, clip;
			if (movie->GetVariable(&root, "_root") && root.IsDisplayObject() && root.GetMember(kClipName, &clip) && clip.IsDisplayObject())
			{
				clip.Invoke("removeMovieClip");
				logger::debug("the panel's clip left the map movie");
			}
		}

		// Asks the engine for character events. Not when the panel opens: the framework reads a rise in the text-entry count as a
		// GAME menu starting to take text unless a window already holds the input (GameField.cpp OnDispatch), and it only
		// publishes that a frame after the window opens. Its universal keyboard would come up over the map's Location Finder.
		void HoldTextInput()
		{
			if (static_cast<State>(state.load()) != State::kOpen || g_textInputHeld) { return; }
			if (auto* controls = RE::ControlMap::GetSingleton())
			{
				controls->AllowTextInput(true);
				g_textInputHeld = true;
				logger::debug("engine text input held");
			}
			else
			{
				logger::warn("no ControlMap; typing on the keyboard will do nothing");
			}
		}

		void ReleaseTextInput()
		{
			if (!g_textInputHeld) { return; }
			g_textInputHeld = false;
			if (auto* controls = RE::ControlMap::GetSingleton()) { controls->AllowTextInput(false); }
			logger::debug("engine text input released");
		}

		// Centres the map on the marker, as SkyUI's Location Finder does: its found-marker ring, then SKSE's ShowOnMap.
		void ShowMarker(int a_index, const std::string& a_label)
		{
			auto movie = MapMovie();
			if (!movie)
			{
				logger::warn("\"{}\" chosen, but the map movie is gone", a_label);
				return;
			}
			RE::GFxValue worldMap, clips, finder, clip;
			if (movie->GetVariable(&worldMap, "_root.WorldMap") && worldMap.IsObject() && worldMap.GetMember("_markerList", &clips) && clips.IsArray() &&
				worldMap.GetMember("_locationFinder", &finder) && finder.IsDisplayObject() && clips.GetElement(static_cast<std::uint32_t>(a_index), &clip) && clip.IsDisplayObject())
			{
				finder.Invoke("setFoundMarker", std::array{ clip });
			}
			RE::GFxValue skse;
			if (!movie->GetVariable(&skse, "_global.skse") || !skse.IsObject())
			{
				logger::warn("\"{}\" chosen, but the map movie has no skse object, so the map cannot be centred", a_label);
				return;
			}
			const bool called = skse.Invoke("ShowOnMap", std::array{ RE::GFxValue(static_cast<double>(a_index)) });
			if (called) { ++showOnMapCalls; }
			logger::info("centred the map on \"{}\" (marker {}): ShowOnMap {}", a_label, a_index, called ? "called" : "refused");
		}

		// ---- closing ------------------------------------------------------------------------------------------------------

		void BeginDrain(const char* a_why)
		{
			r_drainFrames = 0;
			state = static_cast<int>(State::kDraining);
			logger::debug("panel closing ({})", a_why);
			RunUI([] {
				RemoveMovieLayer();
				ReleaseTextInput();
			});
		}

		void FinishClose()
		{
			state = static_cast<int>(State::kClosed);
			if (window) { window->IsOpen = false; }
			logger::debug("panel closed; input handed back");
		}

		// ---- input ------------------------------------------------------------------------------------------------------

		enum class Key { kNone, kUp, kDown, kLeft, kRight, kConfirm, kCancel, kGrid, kBackspace, kToggle, kTypes };

		bool IsToggle(const RE::ButtonEvent& a_button)
		{
			const auto code = a_button.GetIDCode();
			if (a_button.GetDevice() == RE::INPUT_DEVICE::kKeyboard) { return settings::toggleKey != 0 && code == settings::toggleKey; }
			if (a_button.GetDevice() == RE::INPUT_DEVICE::kGamepad) { return settings::togglePad != 0 && code == settings::togglePad; }
			return false;
		}

		Key Classify(const RE::ButtonEvent& a_button)
		{
			const auto code = a_button.GetIDCode();
			if (a_button.GetDevice() == RE::INPUT_DEVICE::kGamepad)
			{
				// With the grid or the row menu open the D-pad walks them, so a toggle on the D-pad only toggles with both closed.
				if (IsToggle(a_button) && !gridOpen && !menuOpen) { return Key::kToggle; }
				if (code == 0x0200) { return Key::kTypes; }
				switch (code)
				{
				case 0x0001: return Key::kUp;
				case 0x0002: return Key::kDown;
				case 0x0004: return Key::kLeft;
				case 0x0008: return Key::kRight;
				case 0x1000: return Key::kConfirm;
				case 0x2000: return Key::kCancel;
				case 0x4000: return Key::kGrid;
				case 0x8000: return Key::kBackspace;
				default: return Key::kNone;
				}
			}
			if (a_button.GetDevice() == RE::INPUT_DEVICE::kKeyboard)
			{
				if (IsToggle(a_button)) { return Key::kToggle; }
				switch (code)
				{
				case 0xC8: return Key::kUp;
				case 0xD0: return Key::kDown;
				case 0xCD: return Key::kRight;
				case 0xCB: return Key::kLeft;
				case 0x1C: case 0x9C: return Key::kConfirm;
				case 0x01: case 0x0F: return Key::kCancel;  // Escape, Tab
				case 0x0E: return Key::kBackspace;
				default: return Key::kNone;                 // a letter: its character event types it
				}
			}
			return Key::kNone;
		}

		bool RepeatDue(float a_heldSecs, float& a_lastRepeatAt)
		{
			if (a_heldSecs < 0.35f || a_heldSecs - a_lastRepeatAt < 0.08f) { return false; }
			a_lastRepeatAt = a_heldSecs;
			return true;
		}

		// A press the panel took is remembered until its release, and the repeats and the release are taken too, even after the
		// panel has closed (World Map Selector for Controller's rule, for the same reasons: Papyrus OnKeyUp and the game's
		// release-triggered actions). A fresh press of a remembered button ends the old claim, so nothing new is ever swallowed
		// once the panel is closed. Game thread only.
		std::vector<std::uint64_t> claimedHeld;

		std::uint64_t ButtonId(const RE::ButtonEvent& a_button)
		{
			return (static_cast<std::uint64_t>(a_button.GetDevice()) << 32) | a_button.GetIDCode();
		}

		bool ClaimUntilReleased(const RE::ButtonEvent& a_button)
		{
			const auto id = ButtonId(a_button);
			const auto it = std::ranges::find(claimedHeld, id);
			if (it == claimedHeld.end()) { return false; }
			if (a_button.IsDown())
			{
				claimedHeld.erase(it);
				logger::debug("device {} code 0x{:X} pressed again before its release was seen; the old claim is dropped", static_cast<std::uint32_t>(a_button.GetDevice()), a_button.GetIDCode());
				return false;
			}
			if (a_button.Value() == 0.0f)
			{
				claimedHeld.erase(it);
				heldButtons = 0;
			}
			return true;
		}

		void RememberClaimed(const RE::ButtonEvent& a_button)
		{
			const auto id = ButtonId(a_button);
			if (std::ranges::find(claimedHeld, id) == claimedHeld.end()) { claimedHeld.push_back(id); }
		}

		void ReadSoon()
		{
			if (!g_readPosted.exchange(true)) { RunUI(ReadMarkers); }
		}

		// Opens on the main thread; the map movie is read in a UI task right after, which also backs out in the local map.
		bool TryOpen(const char* a_how)
		{
			if (!window || static_cast<State>(state.load()) != State::kClosed) { return false; }
			auto* ui = RE::UI::GetSingleton();
			if (!ui || !ui->IsMenuOpen(RE::MapMenu::MENU_NAME))
			{
				logger::debug("{}: the world map is not open; left to the game", a_how);
				return false;
			}
			if (ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) || SKSEMenuFramework::IsAnyBlockingWindowOpened())
			{
				logger::debug("{}: a message box or another mod's window is up; not opening", a_how);
				return false;
			}
			{
				std::scoped_lock lock(g_lock);
				g_list = std::make_shared<MarkerList>();
				g_query.clear();
				++g_queryGeneration;
			}
			++g_listGeneration;
			pendingNavigate = 0;
			pendingGridX = pendingGridY = 0;
			pendingSelect = pendingCancel = pendingGridPress = false;
			gridOpen = false;
			menuOpen = false;
			typesOpen = false;
			pendingMenuMove = 0;
			pendingMenuPress = false;
			heldButtons = 0;
			g_loading = true;
			g_iconsOk = false;
			g_iconSlots = 0;
			m_iconsMissingLogged = false;
			state = static_cast<int>(State::kOpen);
			window->IsOpen = true;
			++opens;
			logger::info("panel opened ({})", a_how);
			RunUI([] {
				// The local map and SkyUI's own finder sit on this same menu; the panel belongs to the world map itself.
				auto movie = MapMovie();
				RE::GFxValue worldMap, local, finder, flag;
				if (movie && movie->GetVariable(&worldMap, "_root.WorldMap") && worldMap.IsObject())
				{
					const bool localShown = worldMap.GetMember("LocalMapMenu", &local) && local.IsDisplayObject() && local.GetMember("_bShow", &flag) && flag.IsBool() && flag.GetBool();
					const bool finderShown = worldMap.GetMember("_locationFinder", &finder) && finder.IsDisplayObject() && finder.GetMember("_bShown", &flag) && flag.IsBool() && flag.GetBool();
					if (localShown || finderShown)
					{
						logger::info("panel backed out: the {} is showing", localShown ? "local map" : "Location Finder");
						pendingCancel = true;
						return;
					}
				}
				ApplyVisibility();
				ReadMarkers();
			});
			return true;
		}

		void EditQuery(const std::function<void(std::string&)>& a_edit)
		{
			std::scoped_lock lock(g_lock);
			a_edit(g_query);
			++g_queryGeneration;
		}

		bool __stdcall OnInputEvent(RE::InputEvent* a_event)
		{
			if (!a_event) { return false; }

			if (const auto* pressed = a_event->AsButtonEvent())
			{
				// While open the repeats and the release come down to the switch below, so a held arrow keeps moving.
				if (static_cast<State>(state.load()) != State::kOpen && ClaimUntilReleased(*pressed)) { return true; }
				if (static_cast<State>(state.load()) == State::kClosed)
				{
					if (pressed->IsDown() && IsToggle(*pressed))
					{
						lastDeviceGamepad = pressed->GetDevice() == RE::INPUT_DEVICE::kGamepad;
						if (TryOpen(lastDeviceGamepad ? "controller button" : "keyboard key"))
						{
							RememberClaimed(*pressed);
							return true;
						}
					}
					return false;
				}
			}
			else if (static_cast<State>(state.load()) == State::kClosed)
			{
				return false;
			}

			const auto type = a_event->GetEventType();
			if (type == RE::INPUT_EVENT_TYPE::kChar)
			{
				if (static_cast<State>(state.load()) == State::kOpen && !menuOpen && !typesOpen)
				{
					const auto text = fold::FromCodePoint(static_cast<const RE::CharEvent*>(a_event)->keyCode);
					if (!text.empty())
					{
						lastDeviceGamepad = false;
						EditQuery([&](std::string& q) { if (q.size() + text.size() <= kMaxQueryBytes) { q += text; } });
					}
				}
				return true;
			}

			if (type == RE::INPUT_EVENT_TYPE::kThumbstick)
			{
				const auto* stick = static_cast<const RE::ThumbstickEvent*>(a_event);
				if (!stick->IsLeft()) { return false; }
				if (static_cast<State>(state.load()) != State::kOpen) { return true; }
				// One step per push past half way, repeating while held (the event carries no hold time, so count time here).
				static int lastDirX = 0, lastDirY = 0;
				static std::chrono::steady_clock::time_point pushedAt, lastStep;
				const int dirX = stick->xValue > 0.5f ? 1 : (stick->xValue < -0.5f ? -1 : 0);
				const int dirY = stick->yValue > 0.5f ? -1 : (stick->yValue < -0.5f ? 1 : 0);
				const auto now = std::chrono::steady_clock::now();
				const auto step = [&] {
					if (menuOpen) { pendingMenuMove += dirY; }
					else if (gridOpen)
					{
						pendingGridX += dirX;
						pendingGridY += dirY;
					}
					else
					{
						pendingNavigate += dirY;
					}
				};
				if (dirX != 0 || dirY != 0) { lastDeviceGamepad = true; }
				if (dirX != lastDirX || dirY != lastDirY)
				{
					lastDirX = dirX;
					lastDirY = dirY;
					if (dirX != 0 || dirY != 0)
					{
						step();
						pushedAt = lastStep = now;
					}
				}
				else if ((dirX != 0 || dirY != 0) && now - pushedAt > std::chrono::milliseconds(350) && now - lastStep > std::chrono::milliseconds(80))
				{
					step();
					lastStep = now;
				}
				return true;
			}

			const auto* button = a_event->AsButtonEvent();
			if (!button) { return false; }
			const auto device = button->GetDevice();
			if (device != RE::INPUT_DEVICE::kKeyboard && device != RE::INPUT_DEVICE::kGamepad) { return false; }  // the mouse stays with ImGui

			// While open every key and button is the panel's: its letters are typing, so they must not fire a hotkey.
			if (button->IsDown()) { RememberClaimed(*button); }
			if (static_cast<State>(state.load()) != State::kOpen) { return true; }
			if (button->Value() == 0.0f)
			{
				if (const auto it = std::ranges::find(claimedHeld, ButtonId(*button)); it != claimedHeld.end()) { claimedHeld.erase(it); }
				heldButtons = 0;
				return true;
			}

			const Key key = Classify(*button);
			if (device == RE::INPUT_DEVICE::kGamepad) { lastDeviceGamepad = true; }
			else if (key != Key::kNone) { lastDeviceGamepad = false; }

			static float lastRepeatAt = 0.0f;
			const bool fresh = button->IsDown();
			if (fresh) { lastRepeatAt = 0.0f; }
			const bool step = fresh || (button->IsPressed() && RepeatDue(button->HeldDuration(), lastRepeatAt));
			switch (key)
			{
			case Key::kUp:
				if (step) { if (menuOpen) { --pendingMenuMove; } else if (gridOpen) { --pendingGridY; } else { --pendingNavigate; } }
				break;
			case Key::kDown:
				if (step) { if (menuOpen) { ++pendingMenuMove; } else if (gridOpen) { ++pendingGridY; } else { ++pendingNavigate; } }
				break;
			case Key::kLeft:
				if (step && gridOpen) { --pendingGridX; }
				else if (fresh && !gridOpen && !menuOpen && device == RE::INPUT_DEVICE::kKeyboard) { typesOpen = !typesOpen; }
				break;
			case Key::kRight:
				if (step && gridOpen) { ++pendingGridX; }
				else if (fresh && !gridOpen && !typesOpen && device == RE::INPUT_DEVICE::kKeyboard) { menuOpen = !menuOpen; }
				break;
			case Key::kConfirm:
				if (fresh)
				{
					heldButtons |= kHeldConfirm;
					if (menuOpen) { pendingMenuPress = true; }
					else if (gridOpen) { pendingGridPress = true; }
					else { pendingSelect = true; }
				}
				break;
			case Key::kCancel:
				if (fresh)
				{
					if (menuOpen) { menuOpen = false; }
					else if (typesOpen) { typesOpen = false; }
					else if (gridOpen && device == RE::INPUT_DEVICE::kGamepad) { gridOpen = false; }
					else
					{
						heldButtons |= kHeldCancel;
						pendingCancel = true;
					}
				}
				break;
			case Key::kToggle:
				if (fresh)
				{
					heldButtons |= kHeldCancel;
					pendingCancel = true;
				}
				break;
			case Key::kGrid:
				if (fresh && !menuOpen && !typesOpen) { gridOpen = !gridOpen; }
				break;
			case Key::kTypes:
				if (fresh && !menuOpen && !gridOpen) { typesOpen = !typesOpen; }
				break;
			case Key::kBackspace:
				// Y: on the list the row's menu, on the keyboard a delete. The real Backspace always deletes.
				if (device == RE::INPUT_DEVICE::kGamepad && !gridOpen)
				{
					if (fresh && !typesOpen) { menuOpen = !menuOpen; }
				}
				else if (step) { EditQuery([](std::string& q) { fold::DropLast(q); }); }
				break;
			default:
				break;
			}
			return true;
		}

		// ---- drawing ------------------------------------------------------------------------------------------------------

		struct Geometry { float x, y, w, h, screenW, screenH; };

		// As World Map Selector for Controller's panel: below the map's top edge, above its button bar, here on the left.
		Geometry ComputeGeometry()
		{
			const auto* viewport = ImGuiMCP::GetMainViewport();
			const float screenW = viewport ? viewport->Size.x : 1920.0f;
			const float screenH = viewport ? viewport->Size.y : 1080.0f;
			const float vx = viewport ? viewport->Pos.x : 0.0f;
			const float vy = viewport ? viewport->Pos.y : 0.0f;
			const float top = screenH * 0.08f;
			const float bottom = screenH * 0.12f;
			return { vx + screenW * 0.015f, vy + top, screenW * static_cast<float>(settings::widthPercent.load()) / 100.0f, screenH - top - bottom, screenW, screenH };
		}

		void RenderDrain(const Geometry& a_g)
		{
			using namespace ImGuiMCP;
			// An invisible window that still takes the mouse, so the framework keeps the input until the press is let go.
			SetNextWindowPos(ImVec2{ a_g.x, a_g.y }, ImGuiCond_Always);
			SetNextWindowSize(ImVec2{ a_g.w, a_g.h }, ImGuiCond_Always);
			if (Begin("##MMLPanelDrain", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {}
			End();
			if (heldButtons.load() == 0 && !IsMouseDown(ImGuiMouseButton_Left)) { FinishClose(); }
			else if (++r_drainFrames > 30) { FinishClose(); }
		}

		void Refilter()
		{
			r_filtered.clear();
			if (!r_list) { return; }
			const std::wstring query = fold::Lower(r_query);
			const auto& markers = r_list->markers;
			r_filtered.reserve(markers.size());
			for (int i = 0; i < static_cast<int>(markers.size()); ++i)
			{
				if (r_only == Only::kOne && markers[i].key != r_onlyKey) { continue; }
				if (r_only == Only::kKind && markers[i].frame != r_onlyFrame) { continue; }
				if (query.empty() || markers[i].folded.find(query) != std::wstring::npos) { r_filtered.push_back(i); }
			}
		}

		// The search line: what has been typed, or the hint, in a box as tall as a framework control.
		void DrawSearchLine()
		{
			using namespace ImGuiMCP;
			const ImVec2 at = GetCursorScreenPos();
			const ImVec2 room = GetContentRegionAvail();
			const float height = GetFrameHeight();
			auto* list = GetWindowDrawList();
			if (list)
			{
				ImDrawListManager::AddRectFilled(list, at, ImVec2{ at.x + room.x, at.y + height }, GetColorU32(ImGuiCol_FrameBg), 0.0f, 0);
				const ImVec2 text{ at.x + 6.0f, at.y + (height - GetTextLineHeight()) * 0.5f };
				if (r_query.empty())
				{
					static const std::string hint = TR("MML_SearchHint", "Search by name");
					ImDrawListManager::AddText(list, text, GetColorU32(ImGuiCol_TextDisabled), hint.c_str());
				}
				else
				{
					ImDrawListManager::AddText(list, text, GetColorU32(ImGuiCol_Text), r_query.c_str());
				}
			}
			Dummy(ImVec2{ room.x, height });
		}

		// The controller's on-screen keyboard: a tile per key, the key under the cursor filled in the text colour with its letter in
		// black or white, so it stands out in any theme. The last row is Space and Delete, half the width each.
		void DrawGrid(float a_height, float a_cellW, float a_cellH)
		{
			using namespace ImGuiMCP;
			const ImVec2 at = GetCursorScreenPos();
			const ImVec2 room = GetContentRegionAvail();
			auto* list = GetWindowDrawList();
			const auto& chars = r_list->grid;
			const int total = static_cast<int>(chars.size());
			const int letterRows = (total + kGridColumns - 1) / kGridColumns;
			const int rows = letterRows + 1;
			if (r_gridRow < r_gridTop) { r_gridTop = r_gridRow; }
			if (r_gridRow >= r_gridTop + kGridRowsShown) { r_gridTop = r_gridRow - kGridRowsShown + 1; }
			r_gridTop = std::clamp(r_gridTop, 0, (std::max)(rows - kGridRowsShown, 0));
			const float lineH = GetTextLineHeight();

			std::uint32_t markText = 0xFF000000;
			if (const auto* style = GetStyle())
			{
				const auto& text = style->Colors[ImGuiCol_Text];
				if (text.x * 0.3f + text.y * 0.6f + text.z * 0.1f < 0.5f) { markText = 0xFFFFFFFF; }
			}
			const auto drawKey = [&](float a_x, float a_y, float a_w, bool a_here, const char* a_text) {
				const ImVec2 to{ a_x + a_w - 2.0f, a_y + a_cellH - 2.0f };
				ImDrawListManager::AddRectFilled(list, ImVec2{ a_x, a_y }, to, GetColorU32(a_here ? ImGuiCol_Text : ImGuiCol_FrameBg), 0.0f, 0);
				const ImVec2 size = CalcTextSize(a_text);
				ImDrawListManager::AddText(list, ImVec2{ a_x + (a_w - 2.0f - size.x) * 0.5f, a_y + (a_cellH - 2.0f - lineH) * 0.5f }, a_here ? markText : GetColorU32(ImGuiCol_Text), a_text);
			};

			if (list)
			{
				for (int row = r_gridTop; row < rows && row < r_gridTop + kGridRowsShown; ++row)
				{
					const float y = at.y + static_cast<float>(row - r_gridTop) * a_cellH;
					if (row == letterRows)
					{
						const float half = a_cellW * static_cast<float>(kGridColumns) * 0.5f;
						const bool onRow = r_gridRow == letterRows;
						drawKey(at.x, y, half, onRow && r_gridCol < kGridColumns / 2, TR("MML_KeySpace", "Space").c_str());
						drawKey(at.x + half, y, half, onRow && r_gridCol >= kGridColumns / 2, TR("MML_KeyDelete", "Delete").c_str());
						continue;
					}
					for (int col = 0; col < kGridColumns; ++col)
					{
						const int i = row * kGridColumns + col;
						if (i >= total) { break; }
						drawKey(at.x + static_cast<float>(col) * a_cellW, y, a_cellW, row == r_gridRow && col == r_gridCol, chars[static_cast<std::size_t>(i)].c_str());
					}
				}
			}
			Dummy(ImVec2{ room.x, a_height });
		}

		enum class MenuAction { kCentre, kHide, kShow, kOnlyOne, kOnlyKind, kShowAll };

		// Changes what the map shows, then lets the map and the list catch up. Render thread.
		void RunMenuAction(MenuAction a_action)
		{
			const bool hiddenChanged = a_action == MenuAction::kHide || a_action == MenuAction::kShow;
			{
				std::scoped_lock lock(g_lock);
				switch (a_action)
				{
				case MenuAction::kHide: g_hidden.insert(r_menuKey); break;
				case MenuAction::kShow: g_hidden.erase(r_menuKey); break;
				case MenuAction::kOnlyOne:
					g_only = Only::kOne;
					g_onlyKey = r_menuKey;
					break;
				case MenuAction::kOnlyKind:
					g_only = Only::kKind;
					g_onlyFrame = r_menuFrame;
					break;
				case MenuAction::kShowAll: g_only = Only::kNone; break;
				default: return;
				}
				++g_queryGeneration;
			}
			logger::info("marker menu: action {} on \"{}\"", static_cast<int>(a_action), r_menuLabel);
			RunUI([hiddenChanged] {
				if (hiddenChanged) { SaveHidden(); }
				ApplyVisibility();
				ReadMarkers();
			});
		}

		void __stdcall Render()
		{
			using namespace ImGuiMCP;
			const auto current = static_cast<State>(state.load());
			if (current == State::kClosed)
			{
				if (window) { window->IsOpen = false; }
				return;
			}

			const Geometry g = ComputeGeometry();
			if (current == State::kDraining)
			{
				RenderDrain(g);
				return;
			}

			// A new opening starts from the top, and from nothing in the map movie (the clip left when the panel last closed).
			if (const auto opened = opens.load(); opened != r_opens)
			{
				r_opens = opened;
				r_lastLayout = Layout{};
				r_highlight = 0;
				r_top = 0;
				r_gridRow = r_gridCol = r_gridTop = 0;
				r_readCountdown = kReadEveryFrames;
				r_framesOpen = 0;
			}
			if (++r_framesOpen == 3) { RunUI(HoldTextInput); }

			// New data from the main thread: a read of the clips, or a change to the query.
			bool refilter = false;
			if (const auto generation = g_listGeneration.load(); generation != r_listGeneration)
			{
				r_listGeneration = generation;
				std::scoped_lock lock(g_lock);
				r_list = g_list;
				refilter = true;
			}
			{
				std::scoped_lock lock(g_lock);
				if (g_queryGeneration != r_queryGeneration)
				{
					r_queryGeneration = g_queryGeneration;
					r_query = g_query;
					r_only = g_only;
					r_onlyKey = g_onlyKey;
					r_onlyFrame = g_onlyFrame;
					refilter = true;
					r_highlight = 0;
					r_top = 0;
				}
			}
			if (refilter) { Refilter(); }

			if (g_loading && --r_readCountdown <= 0)
			{
				r_readCountdown = kReadEveryFrames;
				ReadSoon();
			}

			// The types view is the same list, of types: a new view starts from the top.
			const bool types = typesOpen.load() && r_list;
			if (types != r_typesShown)
			{
				r_typesShown = types;
				r_highlight = 0;
				r_top = 0;
			}
			const int count = static_cast<int>(types ? r_list->kinds.size() : r_filtered.size());
			const int move = pendingNavigate.exchange(0);
			r_highlight = count > 0 ? std::clamp(r_highlight + move, 0, count - 1) : 0;

			int chosen = -1;
			if (pendingSelect.exchange(false)) { chosen = r_highlight; }
			const bool cancel = pendingCancel.exchange(false);

			const bool showGrid = gridOpen.load() && r_list && !r_list->grid.empty();

			// The row menu is about the row that was highlighted when it opened; the list stands still while it is up.
			if (!menuOpen)
			{
				r_menuHasTarget = false;
				pendingMenuMove = 0;
				pendingMenuPress = false;
			}
			else if (!r_menuHasTarget)
			{
				if (count > 0 && r_list)
				{
					const Marker& marker = r_list->markers[static_cast<std::size_t>(r_filtered[static_cast<std::size_t>(r_highlight)])];
					r_menuHasTarget = true;
					r_menuRow = 0;
					r_menuKey = marker.key;
					r_menuLabel = marker.label;
					r_menuFrame = marker.frame;
					r_menuHidden = marker.hidden;
				}
				else
				{
					menuOpen = false;
				}
			}
			const bool showMenu = r_menuHasTarget;
			std::vector<std::pair<MenuAction, std::string>> menuItems;
			if (showMenu)
			{
				menuItems.emplace_back(MenuAction::kCentre, TR("MML_MenuCentre", "Centre the map on it"));
				if (r_menuHidden) { menuItems.emplace_back(MenuAction::kShow, TR("MML_MenuShow", "Show it again")); }
				else { menuItems.emplace_back(MenuAction::kHide, TR("MML_MenuHide", "Hide it")); }
				menuItems.emplace_back(MenuAction::kOnlyOne, TR("MML_MenuOnlyOne", "Show only this one"));
				menuItems.emplace_back(MenuAction::kOnlyKind, TR("MML_MenuOnlyKind", "Show only this kind"));
				if (r_only != Only::kNone) { menuItems.emplace_back(MenuAction::kShowAll, TR("MML_MenuShowAll", "Show all")); }
				r_menuRow = std::clamp(r_menuRow + pendingMenuMove.exchange(0), 0, static_cast<int>(menuItems.size()) - 1);
			}
			int menuPressed = -1;
			if (showMenu && pendingMenuPress.exchange(false)) { menuPressed = r_menuRow; }

			SetNextWindowPos(ImVec2{ g.x, g.y }, ImGuiCond_Always);
			SetNextWindowSize(ImVec2{ g.w, g.h }, ImGuiCond_Always);
			PanelStyle::Push();   // a style file recolours the panel; without one this pushes nothing

			Layout layout;
			layout.screenW = g.screenW;
			layout.screenH = g.screenH;
			layout.x0 = g.x;
			layout.y0 = g.y;
			layout.x1 = g.x + g.w;
			layout.y1 = g.y + g.h;
			if (const auto* style = GetStyle())
			{
				// The window has no background of its own: the map movie's fill is under the icons, in the window's colour.
				const auto& bg = style->Colors[ImGuiCol_WindowBg];
				layout.r = bg.x;
				layout.g = bg.y;
				layout.b = bg.z;
				layout.a = (std::max)(bg.w, 0.75f);   // a theme that paints its window with art has a clear colour here
			}

			if (Begin("##MMLPanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav))
			{
				SetWindowFontScale(static_cast<float>(settings::textScalePercent.load()) / 100.0f);

				const bool titlePushed = PanelStyle::PushRowText(true);
				if (types) { Text("%s", TR("MML_TypesTitle", "Marker types").c_str()); }
				else if (const auto title = TR("MML_PanelTitle", "Map Markers"); r_only != Only::kNone) { Text("%s (%d) - %s", title.c_str(), count, TR("MML_TitleFiltered", "filtered").c_str()); }
				else if (r_list && !r_list->markers.empty()) { Text("%s (%d)", title.c_str(), count); }
				else { Text("%s", title.c_str()); }
				PanelStyle::PopRowText(titlePushed);
				if (!PanelStyle::DrawSeparator()) { Separator(); }

				DrawSearchLine();
				if (!PanelStyle::DrawSeparator()) { Separator(); }

				const ImVec2 room = GetContentRegionAvail();
				const float lineH = GetTextLineHeight();
				const float rowH = GetTextLineHeightWithSpacing();
				const float iconColumn = rowH * 1.3f;

				const std::string hint = types ? TR("MML_HintTypes", "D-pad: move   A: hide or show the type   B: back to the list") :
				                         showMenu ? TR("MML_HintMenu", "D-pad: choose   A: do it   B: back to the list") :
				                         showGrid ? TR("MML_HintGrid", "D-pad: choose a letter   A: add it   Y: delete   B: back to the list") :
				                         (lastDeviceGamepad ? TR("MML_HintPad", "D-pad: move   A: centre the map   X: type   Y: options   RB: types   B: close") :
				                                              TR("MML_HintKeys", "Type to search   Arrows or wheel: move   Right arrow: options   Left arrow: types   Enter or click: centre the map   Esc: close"));
				const float hintH = CalcTextSize(hint.c_str(), nullptr, false, room.x).y + rowH;

				// The grid's size, from the width: the list gives up its height while the grid or the row menu is open.
				const float cellW = room.x / static_cast<float>(kGridColumns);
				const float cellH = lineH * 1.8f;
				float gridH = 0.0f;
				const float menuH = showMenu ? rowH * static_cast<float>(menuItems.size() + 1) : 0.0f;
				if (showGrid)
				{
					const int total = static_cast<int>(r_list->grid.size());
					const int letterRows = (total + kGridColumns - 1) / kGridColumns;
					const int rowsAll = letterRows + 1;
					gridH = static_cast<float>((std::min)(rowsAll, kGridRowsShown)) * cellH;

					r_gridRow = std::clamp(r_gridRow + pendingGridY.exchange(0), 0, rowsAll - 1);
					r_gridCol = std::clamp(r_gridCol + pendingGridX.exchange(0), 0, kGridColumns - 1);
					if (r_gridRow < letterRows && r_gridRow * kGridColumns + r_gridCol >= total) { r_gridCol = (total - 1) % kGridColumns; }
					if (pendingGridPress.exchange(false))
					{
						if (r_gridRow == letterRows)
						{
							if (r_gridCol < kGridColumns / 2) { EditQuery([](std::string& q) { if (q.size() < kMaxQueryBytes) { q += ' '; } }); }
							else { EditQuery([](std::string& q) { fold::DropLast(q); }); }
						}
						else
						{
							const std::string& picked = r_list->grid[static_cast<std::size_t>(r_gridRow * kGridColumns + r_gridCol)];
							EditQuery([&](std::string& q) { if (q.size() + picked.size() <= kMaxQueryBytes) { q += picked; } });
						}
					}
				}
				else
				{
					pendingGridX = pendingGridY = 0;
					pendingGridPress = false;
				}

				const float listH = (std::max)(room.y - hintH - gridH - menuH, rowH);
				const int visible = (std::max)(static_cast<int>(listH / rowH), 1);
				const ImVec2 listAt = GetCursorScreenPos();

				// The rows: only those on screen are touched, so a long list costs what a short one does.
				const auto* io = GetIO();
				if (io && io->MouseWheel != 0.0f && IsWindowHovered())
				{
					r_top -= static_cast<int>(io->MouseWheel) * 3;
				}
				else
				{
					if (r_highlight < r_top) { r_top = r_highlight; }
					if (r_highlight >= r_top + visible) { r_top = r_highlight - visible + 1; }
				}
				r_top = std::clamp(r_top, 0, (std::max)(count - visible, 0));
				const bool mouseMoved = io && (io->MouseDelta.x != 0.0f || io->MouseDelta.y != 0.0f);

				layout.iconX = listAt.x + rowH * 0.6f;
				layout.iconSize = lineH * 1.05f;
				const auto textX = (listAt.x - g.x) + iconColumn;

				if (r_list)
				{
					const auto& markers = r_list->markers;
					for (int row = r_top; row < count && row < r_top + visible; ++row)
					{
						std::string rowText;
						bool rowHidden = false;
						int rowFrame = 0;
						if (types)
						{
							const Kind& kind = r_list->kinds[static_cast<std::size_t>(row)];
							rowText = std::format("{} ({})", kind.name, kind.markers);
							rowHidden = kind.hidden;
							rowFrame = kind.frame;
						}
						else
						{
							const Marker& marker = markers[static_cast<std::size_t>(r_filtered[static_cast<std::size_t>(row)])];
							rowText = marker.label;
							rowHidden = marker.hidden;
							rowFrame = marker.frame;
						}
						const ImVec2 at = GetCursorScreenPos();
						const bool selected = row == r_highlight;
						if (selected && PanelStyle::IsActive())
						{
							const auto* rowStyle = GetStyle();
							const float halfGap = rowStyle ? rowStyle->ItemSpacing.y * 0.5f : 0.0f;
							PanelStyle::DrawRowHighlight(GetWindowDrawList(), at.x, at.y - halfGap, at.x + room.x, at.y + lineH + halfGap);
						}
						const auto id = std::format("##row{}", row);
						if (Selectable(id.c_str(), selected, 0, ImVec2{ 0.0f, lineH }))
						{
							r_highlight = row;
							chosen = row;
						}
						if (IsItemHovered() && mouseMoved) { r_highlight = row; }
						SameLine(textX);
						// A hidden marker is dimmed; the highlight still reads as the highlight.
						const auto* dimStyle = rowHidden && !selected ? GetStyle() : nullptr;
						if (dimStyle) { PushStyleColor(ImGuiCol_Text, dimStyle->Colors[ImGuiCol_TextDisabled]); }
						const bool textPushed = !dimStyle && PanelStyle::PushRowText(selected);
						TextUnformatted(rowText.c_str());
						PanelStyle::PopRowText(textPushed);
						if (dimStyle) { PopStyleColor(1); }
						layout.slots.push_back({ at.y + lineH * 0.5f, rowFrame });
					}
				}

				// What the list has to say when it has no rows.
				if (count == 0)
				{
					const char* key = "MML_NoMatches";
					const char* english = "No markers match.";
					if (!g_hasList && !g_loading)
					{
						key = "MML_NoList";
						english = "This map does not list its markers. SkyUI's map is needed.";
					}
					else if (g_loading)
					{
						key = "MML_Loading";
						english = "Reading the map's markers...";
					}
					TextWrapped("%s", TR(key, english).c_str());
				}

				// The scroll bar: where the window is in the list.
				if (count > visible)
				{
					if (auto* list = GetWindowDrawList())
					{
						const float barX1 = listAt.x + room.x;
						const float barH = (std::max)(listH * static_cast<float>(visible) / static_cast<float>(count), lineH);
						const float barY = listAt.y + (listH - barH) * static_cast<float>(r_top) / static_cast<float>(count - visible);
						ImDrawListManager::AddRectFilled(list, ImVec2{ barX1 - 3.0f, barY }, ImVec2{ barX1, barY + barH }, GetColorU32(ImGuiCol_ScrollbarGrab), 0.0f, 0);
					}
				}

				SetCursorScreenPos(ImVec2{ listAt.x, listAt.y + listH });
				if (showGrid) { DrawGrid(gridH, cellW, cellH); }
				if (showMenu)
				{
					if (const auto* style = GetStyle()) { PushStyleColor(ImGuiCol_Text, style->Colors[ImGuiCol_TextDisabled]); }
					TextUnformatted(r_menuLabel.c_str());
					if (GetStyle()) { PopStyleColor(1); }
					for (int row = 0; row < static_cast<int>(menuItems.size()); ++row)
					{
						const ImVec2 at = GetCursorScreenPos();
						const bool selected = row == r_menuRow;
						if (selected && PanelStyle::IsActive())
						{
							const auto* rowStyle = GetStyle();
							const float halfGap = rowStyle ? rowStyle->ItemSpacing.y * 0.5f : 0.0f;
							PanelStyle::DrawRowHighlight(GetWindowDrawList(), at.x, at.y - halfGap, at.x + room.x, at.y + lineH + halfGap);
						}
						if (Selectable(std::format("##menu{}", row).c_str(), selected, 0, ImVec2{ 0.0f, lineH })) { menuPressed = row; }
						SameLine(textX);
						const bool textPushed = PanelStyle::PushRowText(selected);
						TextUnformatted(menuItems[static_cast<std::size_t>(row)].second.c_str());
						PanelStyle::PopRowText(textPushed);
					}
				}

				if (!PanelStyle::DrawSeparator()) { Separator(); }
				if (const auto* style = GetStyle()) { PushStyleColor(ImGuiCol_Text, style->Colors[ImGuiCol_TextDisabled]); }
				TextWrapped("%s", hint.c_str());
				if (GetStyle()) { PopStyleColor(1); }

				lastTop = r_top;
				lastVisible = visible;
				lastGridChars = r_list ? static_cast<int>(r_list->grid.size()) : 0;
			}
			End();
			PanelStyle::Pop();

			// The style's own edge when it draws one; otherwise the active theme's frame around the panel.
			if (!PanelStyle::DrawPanelEdge(GetForegroundDrawList(), g.x, g.y, g.x + g.w, g.y + g.h))
			{
				static DrawThemeFrameFn drawThemeFrame = GetMenuFrameworkFunction<DrawThemeFrameFn>("AMF_DrawThemeFrame");
				if (drawThemeFrame) { drawThemeFrame(GetForegroundDrawList(), g.x, g.y, g.x + g.w, g.y + g.h); }
			}

			lastX = g.x;
			lastY = g.y;
			lastW = g.w;
			lastH = g.h;
			lastHighlight = r_highlight;
			lastFiltered = count;

			// The map movie's layer, only when something moved.
			if (layout != r_lastLayout)
			{
				r_lastLayout = layout;
				{
					std::scoped_lock lock(g_layoutLock);
					g_layout = layout;
				}
				if (!g_layoutPosted.exchange(true)) { RunUI(ApplyLayout); }
			}

			if (menuPressed >= 0 && menuPressed < static_cast<int>(menuItems.size()))
			{
				const MenuAction action = menuItems[static_cast<std::size_t>(menuPressed)].first;
				menuOpen = false;
				if (action == MenuAction::kCentre) { chosen = r_highlight; }
				else { RunMenuAction(action); }
			}

			if (types)
			{
				// A press hides or shows the type; the view stays up to do the next one.
				if (chosen >= 0 && chosen < count)
				{
					const Kind& kind = r_list->kinds[static_cast<std::size_t>(chosen)];
					{
						std::scoped_lock lock(g_lock);
						const auto key = KindKey(kind.frame);
						if (!g_hidden.erase(key)) { g_hidden.insert(key); }
					}
					logger::info("marker type \"{}\" {}", kind.name, kind.hidden ? "shown" : "hidden");
					RunUI([] {
						SaveHidden();
						ApplyVisibility();
						ReadMarkers();
					});
				}
				if (cancel) { BeginDrain("closed from the types view"); }
			}
			else if (chosen >= 0 && chosen < count && r_list)
			{
				const Marker& marker = r_list->markers[static_cast<std::size_t>(r_filtered[static_cast<std::size_t>(chosen)])];
				const int index = marker.index;
				std::string label = marker.label;
				++selections;
				BeginDrain("a marker was chosen");
				RunUI([index, label = std::move(label)] { ShowMarker(index, label); });
			}
			else if (chosen >= 0 || cancel)
			{
				BeginDrain("closed without a choice");
			}
		}

		std::string JsonEscape(std::string_view a_text)
		{
			std::string out;
			out.reserve(a_text.size());
			for (const char c : a_text)
			{
				if (c == '"' || c == '\\') { out += '\\'; }
				if (static_cast<unsigned char>(c) >= 0x20) { out += c; }
			}
			return out;
		}
	}

	bool Register()
	{
		if (window) { return true; }
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule())
		{
			logger::warn("no menu framework is loaded; this mod needs the Apocrypha Menu Framework and does nothing without it");
			return false;
		}
		if (!HasRequiredExports()) { return false; }
		LoadHidden();
		PanelStyle::Load();
		if (PanelStyle::IsActive()) { PanelStyle::HasRequiredExports(); }
		// BlockUserInput: while open, the framework takes the controller, keyboard and mouse from the game (the map stays still).
		window = SKSEMenuFramework::AddWindow(Render, true);
		static auto* inputEvent = SKSEMenuFramework::AddInputEvent(OnInputEvent);
		(void)inputEvent;
		if (!window)
		{
			logger::warn("the menu framework refused the panel's window");
			return false;
		}
		window->IsOpen = false;
		logger::info("registered the panel with the menu framework (width {}%, text {}%)", settings::widthPercent.load(), settings::textScalePercent.load());
		return true;
	}

	void OnMapMenuOpened()
	{
		RunUI([] {
			m_hiddenClips.clear();
			m_watchFrames = 0;
			WatchNewMap();
		});
	}

	void OnMapMenuClosed()
	{
		ReleaseTextInput();
		m_hiddenClips.clear();
		g_hiddenClipCount = 0;
		m_slotFrame.clear();
		m_appliedBack = Layout{};
		if (static_cast<State>(state.load()) == State::kClosed) { return; }
		state = static_cast<int>(State::kClosed);
		if (window) { window->IsOpen = false; }
		logger::debug("the world map closed; the panel closed with it");
	}

	void DriveOpen() { RunUI([] { TryOpen("DevBench"); }); }
	void DriveClose() { pendingCancel = true; }
	void DriveNav(int a_rows) { pendingNavigate += a_rows; }
	void DriveSelect() { pendingSelect = true; }
	void DriveType(const std::string& a_utf8)
	{
		EditQuery([&](std::string& q) { if (q.size() + a_utf8.size() <= kMaxQueryBytes) { q += a_utf8; } });
	}
	void DriveBackspace()
	{
		EditQuery([](std::string& q) { fold::DropLast(q); });
	}
	void DriveClear()
	{
		EditQuery([](std::string& q) { q.clear(); });
	}
	void DriveGridToggle() { gridOpen = !gridOpen; }
	void DriveGridMove(int a_dx, int a_dy)
	{
		pendingGridX += a_dx;
		pendingGridY += a_dy;
	}
	void DriveGridPress() { pendingGridPress = true; }
	void DriveMenuToggle() { menuOpen = !menuOpen; }
	void DriveMenuMove(int a_rows) { pendingMenuMove += a_rows; }
	void DriveMenuPress() { pendingMenuPress = true; }
	void DriveTypesToggle() { typesOpen = !typesOpen; }

	std::string StateJson(int a_names)
	{
		std::shared_ptr<const MarkerList> list;
		std::string query;
		std::size_t hiddenNames = 0;
		int only = 0;
		{
			std::scoped_lock lock(g_lock);
			list = g_list;
			query = g_query;
			hiddenNames = g_hidden.size();
			only = static_cast<int>(g_only);
		}
		std::string names = "[";
		if (a_names > 0 && list)
		{
			const auto wanted = fold::Lower(query);
			int written = 0;
			for (const auto& marker : list->markers)
			{
				if (written >= a_names) { break; }
				if (!wanted.empty() && marker.folded.find(wanted) == std::wstring::npos) { continue; }
				names += std::format("{}{{\"name\":\"{}\",\"index\":{},\"frame\":{}}}", written ? "," : "", JsonEscape(marker.label), marker.index, marker.frame);
				++written;
			}
		}
		names += "]";
		return std::format(
			R"({{"available":{},"open":{},"state":{},"hasMarkerList":{},"loading":{},"clipsMade":{},"typeRange":{},"listed":{},"query":"{}","filtered":{},"highlight":{},"top":{},"visibleRows":{},)"
			R"("textInputHeld":{},"gridOpen":{},"gridChars":{},"menuOpen":{},"typesOpen":{},"kinds":{},"hiddenNames":{},"hiddenClips":{},"only":{},"iconsOk":{},"iconSlots":{},"rect":[{:.0f},{:.0f},{:.0f},{:.0f}],"opens":{},"selections":{},"showOnMapCalls":{},"claimedHeld":{},"gamepad":{},"language":"{}","style":{},"names":{}}})",
			window != nullptr, state.load() != 0, state.load(), g_hasList.load(), g_loading.load(), g_clipCount.load(), g_typeRange.load(), list ? list->markers.size() : 0, JsonEscape(query), lastFiltered.load(),
			lastHighlight.load(), lastTop.load(), lastVisible.load(), g_textInputHeld, gridOpen.load(), lastGridChars.load(), menuOpen.load(), typesOpen.load(), list ? list->kinds.size() : 0, hiddenNames, g_hiddenClipCount.load(), only, g_iconsOk.load(), g_iconSlots.load(), lastX.load(), lastY.load(), lastW.load(), lastH.load(),
			opens.load(), selections.load(), showOnMapCalls.load(), claimedHeld.size(), lastDeviceGamepad.load(), mml::Strings::Language(), PanelStyle::StateJson(), names);
	}
}
