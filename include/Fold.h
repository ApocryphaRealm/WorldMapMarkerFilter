#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Text helpers for the search, in the player's language: marker names are UTF-8 and may be Cyrillic, Greek or CJK.
namespace fold
{
	// Lower case in UTF-16, for a case-insensitive sort and search.
	std::wstring Lower(std::string_view a_utf8);

	// One code point as UTF-8; empty for a control character.
	std::string FromCodePoint(std::uint32_t a_codePoint);

	// a_text without its last code point.
	void DropLast(std::string& a_text);

	// The characters used by these (lower-cased) names, upper case, as UTF-8, in code point order, without the space.
	std::vector<std::string> Alphabet(const std::vector<std::wstring>& a_names);
}
