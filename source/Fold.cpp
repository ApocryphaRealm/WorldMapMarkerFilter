#include "PCH.h"

#include "Fold.h"

#include <algorithm>
#include <set>

#include <Windows.h>

namespace fold
{
	namespace
	{
		std::string ToUtf8(std::wstring_view a_wide)
		{
			if (a_wide.empty()) { return {}; }
			const int size = WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), out.data(), size, nullptr, nullptr);
			return out;
		}
	}

	std::wstring Lower(std::string_view a_utf8)
	{
		if (a_utf8.empty()) { return {}; }
		const int size = MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
		std::wstring wide(static_cast<std::size_t>(size), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), wide.data(), size);
		if (size > 0) { CharLowerBuffW(wide.data(), static_cast<DWORD>(size)); }
		return wide;
	}

	std::string FromCodePoint(std::uint32_t a_cp)
	{
		if (a_cp < 0x20 || a_cp == 0x7F || a_cp > 0x10FFFF || (a_cp >= 0xD800 && a_cp <= 0xDFFF)) { return {}; }
		std::string out;
		if (a_cp < 0x80) { out += static_cast<char>(a_cp); }
		else if (a_cp < 0x800)
		{
			out += static_cast<char>(0xC0 | (a_cp >> 6));
			out += static_cast<char>(0x80 | (a_cp & 0x3F));
		}
		else if (a_cp < 0x10000)
		{
			out += static_cast<char>(0xE0 | (a_cp >> 12));
			out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (a_cp & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (a_cp >> 18));
			out += static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (a_cp & 0x3F));
		}
		return out;
	}

	void DropLast(std::string& a_text)
	{
		while (!a_text.empty())
		{
			const auto last = static_cast<unsigned char>(a_text.back());
			a_text.pop_back();
			if ((last & 0xC0) != 0x80) { return; }  // not a continuation byte: the code point's first byte is gone
		}
	}

	std::vector<std::string> Alphabet(const std::vector<std::wstring>& a_names)
	{
		std::set<wchar_t> upper;
		std::set<wchar_t> seen;
		for (const auto& name : a_names)
		{
			for (const wchar_t c : name)
			{
				if (c > 0x20 && (c < 0xD800 || c > 0xDFFF) && seen.insert(c).second)
				{
					wchar_t up = c;
					CharUpperBuffW(&up, 1);
					upper.insert(up);
				}
			}
		}
		std::vector<std::string> out;
		out.reserve(upper.size());
		for (const wchar_t c : upper) { out.push_back(ToUtf8(std::wstring_view(&c, 1))); }
		return out;
	}
}
