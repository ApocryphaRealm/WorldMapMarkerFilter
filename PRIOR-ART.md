# Prior art for Map Marker List (searched 2026-11-04, rule 24)

Searched Nexus (skyrimspecialedition, GraphQL `nameStemmed`, op MATCHES): map marker search, searchable map, map marker list, map
search, world map search, find location map, map locations list, marker list, fast travel menu, fast travel list, quick travel,
location list, Legendary Map, Location Finder, map finder, map legend, map menu, marker search, search location, map overhaul,
Atlas search, world map panel, map index, travel list, SkyUI Map. Descriptions read through the same API for 192565, 194688, 157397.

Searched GitHub (`gh search repos`): skyrim map marker, location finder skyrim, SkyUI map search, map search controller skyrim,
Legendary Map skyrim, map menu redesigned. Nothing for this: the nearest are kassent/CustomMapMarker (placing a marker) and
Juan-MZ/CoMAP-MapMarkerFramework-1.7.99-Patch.

Nothing found does this on 1.5.97 with a controller. The nearest:

| Mod | Where | What it does | Why it is not this |
|---|---|---|---|
| SkyUI's Location Finder | SkyUI 5.2, map.swf, `Map.LocationFinder` | A small box listing the map's markers by name, with their icons, filtered by typing; a press centres the map through `skse.ShowOnMap` | The base this mod reads from. On a controller it opens with a right stick click, but its search box takes a keyboard, so a pad cannot filter |
| SkyUI Map Search Controller Support 0.1.0 | Nexus 192565 | Right stick click opens the Finder, D-pad moves through results, confirm and cancel work | Tested on 1.6.1170 and 1.7.104 only; no typing on a pad, so results cannot be filtered |
| Map Menu Redesigned | Nexus 194688 | Moves Find Location to a top panel, among many map changes (hold to travel, marker mode, custom markers) | A map overhaul; no left list, no pad typing shown |
| Legendary Map 1.0.2 | Nexus 157397 (ships its Papyrus source; the SWF has no public source) | A legend of marker TYPES, toggled, across the map: hides a type | Not a list of markers; hides, does not search or centre |
| Legendary Map - Toggle All, CoMAP Support | Nexus 169503, 174532 | Add-ons to the above | Same |
| Mark and Recall Fast Travel Menu | Nexus 22494 | Your own named teleports | Not the map's markers |

Read for the mechanism: SkyUI's map.swf (the running list's copy under Norden UI) for `_markerList`, `LocationListEntry`, `iconFrame`,
`TYPE_RANGE` and `skse.ShowOnMap`; Legendary Map's legend_inject.swf for hiding by `IconClip._visible`. No code or art is copied
from either: the icons are drawn by the player's own map movie.

Project precedent used: World Map Selector for Controller's panel (`source/PanelStyle.cpp` is its style code; the geometry, the
press-until-release claim and the drain follow its `MapPanel.cpp`).
