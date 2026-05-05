#pragma once

#include "texture.h"
#include "../util/types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace nova3di::texture::tga {

	//============================================================================
	// Normalize uncommon TGA (type 1 paletted, type 3 grayscale, type 9 RLE,
	// type 10 RLE truecolor, type 11 RLE grayscale) into a type-2 24/32bpp
	//============================================================================
	bool normalize(const u8* src, size_t src_len, std::vector<u8>& out);

	bool write_tga(
		int w,
		int h,
		const u8* indices,
		const u8* palette_rgba,
		const std::string& out_tga,
		const TextureSpec& spec
	);

	bool write_bgra32(
		const std::string& path,
		int w,
		int h,
		const u8* bgra
	);

	bool write_bgr24(
		const std::string& path,
		int w,
		int h,
		const u8* bgra
	);

	void bleed_rgb(
		u8* bgra,
		std::vector<u8>& filled,
		std::vector<i32>& queue,
		int w,
		int h
	);

	//============================================================================
	// 32bpp BGRA alpha cutoff: binarize alpha to 0/255 at the threshold (or
	// below it when invert is set), then bleed RGB from the surviving 255
	// pixels into the zeroed neighbours so bilinear filtering doesn't fringe.
	//============================================================================
	void cutoff_alpha(
		u8* bgra,
		int w,
		int h,
		u8 cutoff,
		bool invert
	);

}
