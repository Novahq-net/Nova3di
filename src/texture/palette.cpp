#include "palette.h"

#include <fstream>

namespace nova3di::texture::palette {

	//========================================================================
	// Read the trailing 0x0C + 768 RGB bytes of a .PAL or .PCX file
	//========================================================================
	bool load_palette(const std::string& path, u8 palette[768]) {

		std::ifstream file(path, std::ios::binary | std::ios::ate);

		if(!file)
			return false;

		std::streamsize len = file.tellg();

		if(len < 769)
			return false;

		file.seekg(len - 769, std::ios::beg);
		u8 marker = 0;

		if(!file.read(reinterpret_cast<char*>(&marker), 1) || marker != 0x0C)
			return false;

		return (bool)file.read(reinterpret_cast<char*>(palette), 768);
	}

	//========================================================================
	// Expand a 3-byte-stride RGB palette into a 4-byte-stride RGBA table with alpha=0xFF
	//========================================================================
	void expand_rgba(const u8 pal_rgb[768], u8 pal_rgba[1024]) {
		for(int i = 0; i < 256; ++i) {
			pal_rgba[i*4 + 0] = pal_rgb[i*3 + 0];
			pal_rgba[i*4 + 1] = pal_rgb[i*3 + 1];
			pal_rgba[i*4 + 2] = pal_rgb[i*3 + 2];
			pal_rgba[i*4 + 3] = 0xFF;
		}
	}

	//========================================================================
	// Compact a 4-byte-stride RGBA palette into a 3-byte-stride RGB table
	//========================================================================
	void compact_rgb(const u8 pal_rgba[1024], u8 pal_rgb[768]) {
		for(int i = 0; i < 256; ++i) {
			pal_rgb[i*3 + 0] = pal_rgba[i*4 + 0];
			pal_rgb[i*3 + 1] = pal_rgba[i*4 + 1];
			pal_rgb[i*3 + 2] = pal_rgba[i*4 + 2];
		}

	}

}
