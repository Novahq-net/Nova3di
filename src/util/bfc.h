#pragma once

#include "types.h"

#include <span>
#include <vector>

namespace nova3di::util::bfc {

	std::vector<u8> decompress(std::span<const u8> data);

}
