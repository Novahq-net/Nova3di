#pragma once

#include "../util/types.h"

#include <array>
#include <string>

namespace nova3di::texture::palette {

	inline constexpr auto grayscale = []() {

		std::array<u8, 768> palette{};

		for(int i = 0; i < 256; ++i) {
			palette[i*3]     = (u8)i;
			palette[i*3 + 1] = (u8)i;
			palette[i*3 + 2] = (u8)i;
		}

		return palette;
	}();

	bool load_palette(const std::string& path, u8 palette[768]);

	void expand_rgba(const u8 pal_rgb[768], u8 pal_rgba[1024]);

	void compact_rgb( const u8 pal_rgba[1024], u8 pal_rgb[768]);

}
