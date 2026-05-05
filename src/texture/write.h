#pragma once

#include "texture.h"
#include "../util/types.h"

#include <span>
#include <string>
#include <vector>

namespace nova3di::texture {

	//============================================================================
	// v10 t3di matches loaded textures by (name1, name2, alpha-relevant flag
	// bits, off36 dword). Other formats default everything to empty/zero.
	// Since we can't write 2 files with the same name, this helps us 
	// number textures on output when the source doesn't have unique names 
	//============================================================================
	struct DedupKey {
		std::string name1_lc;
		std::string name2_lc;
		u8          flag_bits = 0;
		u32         off36     = 0;
		auto operator<=>(const DedupKey&) const = default;
	};

	bool write(const std::string& out_dir, const Image& image, bool raw);

	struct WriteResult {
		std::vector<std::string> filenames;
		int found   = 0;
		int missing = 0;
	};

	WriteResult write_all(
		const std::string& out_dir,
		std::span<const Image> images,
		bool raw,
		std::span<const DedupKey> keys = {}
	);

}
