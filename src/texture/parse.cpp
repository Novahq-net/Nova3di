#include "parse.h"

#include "dds.h"
#include "pcx.h"
#include "tga.h"
#include "../util/bfc.h"
#include "../util/io.h"
#include "../util/log.h"
#include "../util/lzp1.h"

#include <cstring>
#include <filesystem>
#include <span>

namespace nova3di::texture {

	// Fallback order: prefer .dds over .pcx so that BHD/C4 textures (which
	// often ship both - the .dds is the modern asset, the .pcx is a legacy
	// downscaled fallback) resolve to .dds when the header's stated extension
	// is absent. Older games ship no .dds files, so reordering is a no-op
	// for them. The header's stated extension is always tried first.
	static const char* TEXTURE_EXTS[] = {
		".dds", ".tga", ".pcx", ".bmp",
		nullptr
	};

	//============================================================================
	// LOD suffix convention:
	//   <stem>H  = High
	//   <stem>M  = Medium
	//   <stem>L  = Low
	//   <stem>   = full
	//============================================================================
	std::string resolve_lod(const std::string& input_dir, const std::string& filename) {

		size_t dot = filename.find_last_of('.');

		if(dot == std::string::npos || dot < 2)
			return filename;

		char last = filename[dot - 1];

		if(last != 'Q' && last != 'q')
			return filename;

		std::string stem = filename.substr(0, dot - 1);
		std::string ext  = filename.substr(dot);

		auto exists = [&](const std::string& fn) -> bool {
			return std::filesystem::exists(input_dir + "/" + fn);
		};

		std::string plain = stem + ext;

		if(exists(plain))
			return plain;

		char h_char = (last == 'Q') ? 'H' : 'h';
		std::string half = stem + h_char + ext;

		if(exists(half))
			return half;

		return filename;
	}

	namespace {

		//========================================================================
		// Decode bytes (already BFC/LZP1-unwrapped) into the Image struct.
		//========================================================================
		bool decode_pixels(Image& tex, std::span<const u8> bytes) {

			if(bytes.size() > 4 && memcmp(bytes.data(), "LZP1", 4) == 0) {

				auto img = util::lzp1::decode(bytes);

				if(img.pixels.empty() || img.palette.size() < 768)
					return false;

				tex.format = Format::Paletted;
				tex.width  = img.width;
				tex.height = img.height;
				tex.pixels = std::move(img.pixels);
				std::memcpy(tex.palette.data(), img.palette.data(), 768);
				return true;
			}

			if(bytes.size() > 128 && memcmp(bytes.data(), "DDS ", 4) == 0) {

				int w = 0, h = 0;
				std::vector<u8> bgra;

				if(!dds::decode(bytes, bgra, w, h))
					return false;

				tex.format = Format::Bgra;
				tex.width  = w;
				tex.height = h;
				tex.pixels = std::move(bgra);
				return true;
			}

			if(bytes.size() > 70 && bytes[0] == 0x0A) {

				int  w = 0, h = 0;
				bool is_paletted = false;
				std::vector<u8> pixels;
				u8 palette[768] = {};

				if(!pcx::decode(bytes, pixels, palette, w, h, is_paletted))
					return false;

				tex.format = is_paletted ? Format::Paletted : Format::Bgra;
				tex.width  = w;
				tex.height = h;
				tex.pixels = std::move(pixels);

				if(is_paletted)
					std::memcpy(tex.palette.data(), palette, 768);

				return true;
			}

			if(bytes.size() > 18) {

				std::vector<u8> normalized;
				const u8* tga_buf  = bytes.data();
				size_t    tga_size = bytes.size();

				if(tga::normalize(bytes.data(), bytes.size(), normalized)) {
					tga_buf  = normalized.data();
					tga_size = normalized.size();
				}

				if(tga_size <= 18 || tga_buf[2] != 2 || (tga_buf[16] != 24 && tga_buf[16] != 32))
					return false;

				u16 w = util::io::read_u16(tga_buf + 12);
				u16 h = util::io::read_u16(tga_buf + 14);
				u8  bpp = tga_buf[16];
				u8  desc = tga_buf[17];
				bool top_down = (desc & 0x20) != 0;
				size_t pixel_off = 18u + tga_buf[0];
				size_t npix = (size_t)w * h;

				if(pixel_off + npix * (bpp / 8) > tga_size)
					return false;

				const u8* px = tga_buf + pixel_off;
				std::vector<u8> bgra(npix * 4);

				for(int y = 0; y < (int)h; ++y) {
					int dst_y = top_down ? y : ((int)h - 1 - y);
					const u8* row_src = px + (size_t)y * w * (bpp / 8);
					u8*       row_dst = bgra.data() + (size_t)dst_y * w * 4;

					if(bpp == 32) {
						std::memcpy(row_dst, row_src, (size_t)w * 4);
					} else {
						for(int x = 0; x < (int)w; ++x) {
							row_dst[x*4 + 0] = row_src[x*3 + 0];
							row_dst[x*4 + 1] = row_src[x*3 + 1];
							row_dst[x*4 + 2] = row_src[x*3 + 2];
							row_dst[x*4 + 3] = 255;
						}
					}
				}

				tex.format = Format::Bgra;
				tex.width  = (int)w;
				tex.height = (int)h;
				tex.pixels = std::move(bgra);
				return true;
			}

			return false;
		}

	} // namespace

	//========================================================================
	// Parse an embedded texture from bytes
	//========================================================================
	bool parse(Image& image, std::span<const u8> embedded_bytes) {
		return decode_pixels(image, embedded_bytes);
	}

	//========================================================================
	// Parse an external texture from disk
	//========================================================================
	bool parse(Image& image, const std::string& input_dir, const std::string& filename) {

		std::string src_lod  = resolve_lod(input_dir, filename);
		std::string src_base = util::io::strip_ext(src_lod);
		std::string src_found;
		auto buf_opt = util::io::read_file(input_dir + "/" + src_lod);

		if(buf_opt) {
			src_found = src_lod;
		} else {
			for(const char** ext = TEXTURE_EXTS; *ext; ++ext) {
				std::string alt = src_base + *ext;
				buf_opt = util::io::read_file(input_dir + "/" + alt);

				if(buf_opt) {
					src_found = alt;
					break;
				}
			}
		}

		if(!buf_opt)
			return false;

		image.name = util::io::strip_ext(src_found);

		std::vector<u8>& buf = *buf_opt;
		std::span<const u8> bytes{buf};

		std::vector<u8> bfc_decomp;

		if(bytes.size() > 8 && memcmp(bytes.data(), "BFC1", 4) == 0) {
			bfc_decomp = util::bfc::decompress(bytes);

			if(bfc_decomp.empty())
				util::log::warn("BFC1 decompression failed for %s", src_found.c_str());
			else
				bytes = std::span<const u8>{bfc_decomp};
		}

		image.raw_bytes.assign(bytes.begin(), bytes.end());
		image.raw_ext = util::io::to_lower(util::io::extension(src_found));

		if(!image.raw_ext.empty() && image.raw_ext[0] == '.')
			image.raw_ext.erase(0, 1);

		return decode_pixels(image, bytes);
	}

}
