#pragma once

#include "types.h"

#include <span>
#include <string>

namespace nova3di::util::scr1 {

	constexpr u32 KEY_DF1     = 0x04960552; // DF1 (no SCR1 header)
	constexpr u32 KEY_DF2     = 0x01234567; // DF2, LW, TFD, C4
	constexpr u32 KEY_BHD     = 0x2A5A8EAD; // BHD, BHDTS, DFX, DFX2, JOCA
	constexpr u32 KEY_FX      = 0xA55B1EED; // DFX, DFX2, JOCA (.fx files)
	constexpr u32 KEY_JO_DEMO = 0xABEEFACE; // JO Demo / JOE

	std::string decode(std::span<const u8> data, u32 key);

	int convert(const std::string& input_file, std::span<const u8> data, const std::string& out_dir);

}
