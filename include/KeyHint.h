#pragma once

// The hint in the world map's button bar: "Marker Filter" with the toggle's button or key, as SkyUI draws its own hints.
// bShowKeyHint in the INI turns it off. Needs SkyUI's map (or a UI built on it); on another map it stays out of the way.
namespace keyhint
{
	// Starts following the input device. Call at kInputLoaded.
	void Register();

	// Adds the hint if the bar does not have it. Safe to call again.
	void Show();
}
