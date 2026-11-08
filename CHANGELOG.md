# WorldMapMarkerFilter - changelog

Rule 61: this mod's own history, kept beside the code it describes.

## 1.1.0 - 2026-11-08 - untested

### Added
- The marker menu has Hide this kind (and Show this kind again on a hidden kind), beside Show only this kind.

## 1.0.0 - 2026-11-04 - untested

### Changed
- Named World Map Marker Filter (it was Map Marker List before it was posted): DLL, INI, log, language files and the DevBench tool wmmf.control. A MapMarkerList_Hidden.txt is renamed to WorldMapMarkerFilter_Hidden.txt, or read as it is if the rename fails.

### Added
- A hint in the world map's button bar, Marker Filter with the toggle's controller button or keyboard key, beside any other mod's hint; bShowKeyHint in the INI turns it off. Copied from World Map Selector for Controller's hint; not yet run in game.
- RB shows the marker types, like a legend: A hides or shows every marker of a type, kept across games.
- Y on a marker opens a menu: centre the map on it, hide it or show it again, show only this one, show only this kind, show all. The on-screen keyboard is QWERTY with Space and Delete keys and a filled cursor.
- A panel down the left of the world map, opened and closed by D-pad left or /, listing the map's markers top to bottom by name with each marker's icon beside it. Lists what SkyUI's Location Finder lists (clips in _root.WorldMap._markerList of a type inside Map.LocationFinder.TYPE_RANGE), minus a marker whose IconClip another mod hid, so only what the map shows.
- Search by name: typing on a keyboard (the engine's character events, every key kept from the game and other mods while open), and on a controller an on-screen keyboard of the characters the listed names use. Case-insensitive in every language.
- Choosing a marker centres the map on it through skse.ShowOnMap, with the Location Finder's found-marker ring, and closes the panel.
- The icons are the map movie's own LocationListEntry symbol placed under the framework's window, so icon replacers and CoMAP's icons show as they do on the map.
- DevBench tool wmmf.control, eleven languages, INI keys for the two toggles, panel width and text size; reads World Map Selector for Controller's panel style file.

### Fixed
- Opening the panel with the keyboard key no longer types that key into the search (it opened as '/' with no markers listed). The opening key's character is left out, and the search is cleared when that happens.
- This hint and World Map Selector for Controller's no longer overlap on the world map's button bar, in either load order. Each attaches a new clip as the map opens; a new clip reports the width of its placeholder until it loads the next frame, and the panel placed the next hint by that width. The panel now lays itself out again when a clip loads (its onButtonLoad call, which SkyUI's map leaves empty). World Map Selector is unchanged.