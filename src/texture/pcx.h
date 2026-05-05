#pragma once

#include "texture.h"
#include "../util/types.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nova3di::texture::pcx {
	
	bool write_pixels(
		std::string_view path, 
		int w, 
		int h, 
		const u8* pixels, 
		const u8* palette_rgb
	);

	bool write_rle(
		std::string_view path, 
		int w, 
		int h, 
		const u8* rle_data, 
		size_t rle_size, 
		const u8* palette_rgb
	);

	bool load(
		std::string_view pcx_path,
		std::vector<u8>& pixels,
		u8 palette_rgb[768],
		int& w,
		int& h
	);

	bool to_tga(
		const std::string& src_pcx,
		const std::string& dst_tga,
		const TextureSpec& spec
	);

	bool to_tga(
		std::span<const u8> src_bytes,
		const std::string& dst_tga,
		const TextureSpec& spec
	);

	bool decode(
		std::span<const u8> src_bytes,
		std::vector<u8>&    out_pixels,    // 8bpp indices or 32bpp BGRA
		u8                  out_palette[768],
		int&                out_width,
		int&                out_height,
		bool&               out_is_paletted
	);

}
