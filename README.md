# Map Marker List

Version 1.0.0

A panel down the left of the world map that lists the map's markers top to bottom by name, each with its icon beside it, and
lets you search them by name. Choosing one centres the map on it.

## Using it

Open the world map, then press:

| Device | Default | INI key (`[Keys]`) |
| --- | --- | --- |
| Controller | **D-pad left** | `iTogglePad=0x0004` |
| Keyboard | **/** | `iToggleKey=0x35` |

Outside the world map both presses are left to the game.

- Controller: D-pad up / down or the left stick moves; **A** centres the map on the highlighted marker and closes the panel; **X**
  opens the on-screen keyboard; **Y** deletes a letter; **B** closes the keyboard, then the panel.
- On-screen keyboard: the D-pad walks the letters and **A** adds one. It holds the characters the listed names use, so it fits every
  language. The first key, shown as `_`, is the space.
- Keyboard and mouse: type to search; arrows or the wheel move; Enter or a click centres the map; Backspace deletes; Esc or Tab closes.

Only what the map shows is listed: a marker you have not found is not on the map, so it is not here, and a marker type another
mod has hidden (Legendary Map) is left out. A marker the map shows as undiscovered is listed with that same icon.

The icons are drawn by your own map movie, so icon replacers and CoMAP's icons appear as they do on the map. The panel wears the
Apocrypha Menu Framework's theme, or the same style file as World Map Selector for Controller's panel when one is installed.

## Requirements

SKSE64, Address Library for SKSE Plugins, the Apocrypha Menu Framework, and SkyUI's map (or a UI mod built on it, such as Norden UI).
Skyrim 1.5.97, 1.6 or 1.7: the installer picks the build.

## Building

`configure.bat` then `build.bat` for the SE and AE line, `configure17.bat` then `build17.bat` for the 1.7 line. `VCPKG_ROOT` must point
at a vcpkg checkout. Visual Studio, CMake and Ninja are found by `find-msvc.bat`.

## Testing

DevBench tool `mml.control`: `{}` for the state (add `"names":N` to list rows), and ops `open`, `close`, `nav`, `select`, `type`,
`backspace`, `clear`, `grid`, `gridmove`, `gridpress`.

## Licence

GPL-3.0-or-later. See `LICENSE`, `NOTICE.md` and `THIRD_PARTY_NOTICES.md`.
