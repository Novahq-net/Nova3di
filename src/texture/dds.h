#pragma once

#include "texture.h"
#include "../util/types.h"

#include <span>
#include <string>

#include <vector>

namespace nova3di::texture::dds {

	bool to_tga(
		std::span<const u8> dds_bytes,
		const std::string& tga_path,
		const TextureSpec& spec = {}
	);

	bool decode(
		std::span<const u8> dds_bytes,
		std::vector<u8>&    out_bgra,
		int&                out_width,
		int&                out_height
	);

}
