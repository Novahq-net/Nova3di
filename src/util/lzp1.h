#pragma once

#include "types.h"

#include <span>
#include <vector>

namespace nova3di::util::lzp1 {

	struct Image {
		int             width  = 0;
		int             height = 0;
		std::vector<u8> palette; // 768 bytes: 256 RGB triples
		std::vector<u8> pixels;
	};

	Image decode(std::span<const u8> data);

}
