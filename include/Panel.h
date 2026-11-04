#pragma once

#include <string>

// The panel: the world map's markers, top to bottom by name, each with its icon beside it, down the left of the map, on a
// toggle. Typing filters by name; choosing one centres the map on it; a row's menu hides it or shows only it or its kind.
namespace panel
{
	// Registers the window and the input callback with the menu framework. Call at kPostPostLoad.
	bool Register();

	// The world map opened: the hidden markers go back on its clips as it makes them.
	void OnMapMenuOpened();

	// The world map closed: the panel closes with it. Main thread.
	void OnMapMenuClosed();

	// DevBench driving (rule 64). A real test still presses the real button; these prove the panel, not the input path.
	void DriveOpen();
	void DriveClose();
	void DriveNav(int a_rows);
	void DriveSelect();
	void DriveType(const std::string& a_utf8);
	void DriveBackspace();
	void DriveClear();
	void DriveGridToggle();
	void DriveGridMove(int a_dx, int a_dy);
	void DriveGridPress();
	void DriveMenuToggle();
	void DriveMenuMove(int a_rows);
	void DriveMenuPress();

	// The panel's live state as a JSON object (rule 31); a_names lists that many of the rows the filter leaves.
	std::string StateJson(int a_names);
}
