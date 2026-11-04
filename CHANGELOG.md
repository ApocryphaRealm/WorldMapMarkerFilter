# MapMarkerList - changelog

Rule 61: this mod's own history, kept beside the code it describes.

## 1.0.0 - 2026-11-04 - untested

### Added
- A panel down the left of the world map, opened and closed by D-pad left or /, listing the map's markers top to bottom by name with each marker's icon beside it. Lists what SkyUI's Location Finder lists (clips in _root.WorldMap._markerList of a type inside Map.LocationFinder.TYPE_RANGE), minus a marker whose IconClip another mod hid, so only what the map shows.
- Search by name: typing on a keyboard (the engine's character events, every key kept from the game and other mods while open), and on a controller an on-screen keyboard of the characters the listed names use. Case-insensitive in every language.
- Choosing a marker centres the map on it through skse.ShowOnMap, with the Location Finder's found-marker ring, and closes the panel.
- The icons are the map movie's own LocationListEntry symbol placed under the framework's window, so icon replacers and CoMAP's icons show as they do on the map.
- DevBench tool mml.control, eleven languages, INI keys for the two toggles, panel width and text size; reads World Map Selector for Controller's panel style file.
