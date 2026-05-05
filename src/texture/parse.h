#pragma once

#include "texture.h"
#include "../util/types.h"

#include <span>
#include <string>

namespace nova3di::texture {

	bool parse(Image& image, std::span<const u8> embedded_bytes);
	bool parse(Image& image, const std::string& input_dir, const std::string& filename);

	std::string resolve_lod(const std::string& input_dir, const std::string& filename);

}
