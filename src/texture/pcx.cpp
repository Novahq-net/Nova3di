#include "pcx.h"

#include "palette.h"
#include "tga.h"
#include "../util/io.h"

#include <array>
#include <cstring>
#include <fstream>
#include <string>

namespace nova3di::texture::pcx {

	namespace {

		//============================================================================
		// 128-byte header for 8bpp PCX
		//============================================================================
		std::array<u8, 128> build_header(int width, int height) {

			std::array<u8, 128> hdr{};

			hdr[0] = 0x0A; // manuf
			hdr[1] = 5;    // version
			hdr[2] = 1;    // RLE-compressed
			hdr[3] = 8;    // bits per pixel per plane

			u16 xmax = (u16)(width - 1), ymax = (u16)(height - 1);

			memcpy(hdr.data() + 8,  &xmax, 2);
			memcpy(hdr.data() + 10, &ymax, 2);

			u16 hdpi = (u16)width, vdpi = (u16)height;

			memcpy(hdr.data() + 12, &hdpi, 2);
			memcpy(hdr.data() + 14, &vdpi, 2);

			hdr[65] = 1; // planes
			u16 bpl = (u16)((width + 1) & ~1); // bytes per line

			memcpy(hdr.data() + 66, &bpl, 2);

			hdr[68] = 1; // color

			return hdr;
		}

		//============================================================================
		// Decode RLE-compressed pixel data
		//============================================================================
		bool decode(
			const u8* src, 
			size_t src_len, 
			std::vector<u8>& pixels, 
			int& width, 
			int& height, 
			u8 palette_rgb[768]
		) {

			if(src_len < 128 + 769 || src[0] != 0x0A)
				return false;

			if(src[3] != 8 || src[65] != 1)
				return false;

			u16 xmin;
			u16 ymin; 
			u16 xmax;
			u16 ymax;
			u16 bpl;

			memcpy(&xmin, src + 4, 2);
			memcpy(&ymin, src + 6, 2);
			memcpy(&xmax, src + 8, 2);
			memcpy(&ymax, src + 10, 2);
			memcpy(&bpl, src + 66, 2);

			width = (int)xmax - (int)xmin + 1;
			height = (int)ymax - (int)ymin + 1;

			if(width <= 0 || height <= 0 || bpl < width)
				return false;

			pixels.assign((size_t)width * height, 0);
			size_t p = 128;
			size_t body_end = src_len - 769;

			for(int y = 0; y < height; ++y) {

				int col = 0;
				while(col < bpl) {

					if(p >= body_end) 
						return false;

					u8 b = src[p++];
					int count = 1;
					u8 val = b;

					if((b & 0xC0) == 0xC0) {
						count = b & 0x3F;

						if(p >= body_end) 
							return false;

						val = src[p++];
					}
					for(int i = 0; i < count && col < bpl; ++i, ++col) {
						if(col < width)
							pixels[(size_t)y * width + col] = val;
					}
				}
			}
			const u8* pal_marker = src + src_len - 769;

			if(*pal_marker != 0x0C)
				return false;

			memcpy(palette_rgb, pal_marker + 1, 768);
			return true;
		}

		//============================================================================
		// Decode RLE-compressed 24bpp 3-plane PCX (R-plane, G-plane, B-plane per
		// row, no trailing palette) into a BGRA pixel buffer with alpha = 255
		//============================================================================
		bool decode_truecolor(
			const u8* src,
			size_t src_len,
			std::vector<u8>& bgra,
			int& width,
			int& height
		) {

			if(src_len < 128 || src[0] != 0x0A)
				return false;

			if(src[3] != 8 || src[65] != 3)
				return false;

			u16 xmin;
			u16 ymin;
			u16 xmax;
			u16 ymax;
			u16 bpl;

			memcpy(&xmin, src + 4, 2);
			memcpy(&ymin, src + 6, 2);
			memcpy(&xmax, src + 8, 2);
			memcpy(&ymax, src + 10, 2);
			memcpy(&bpl, src + 66, 2);

			width  = (int)xmax - (int)xmin + 1;
			height = (int)ymax - (int)ymin + 1;

			if(width <= 0 || height <= 0 || bpl < width)
				return false;

			bgra.assign((size_t)width * height * 4, 255);

			std::vector<u8> row_buf((size_t)bpl * 3);
			size_t p = 128;

			for(int y = 0; y < height; ++y) {

				size_t col = 0;

				while(col < row_buf.size()) {

					if(p >= src_len)
						return false;

					u8 b = src[p++];
					int count = 1;
					u8 val = b;

					if((b & 0xC0) == 0xC0) {
						count = b & 0x3F;

						if(p >= src_len)
							return false;

						val = src[p++];
					}

					for(int i = 0; i < count && col < row_buf.size(); ++i, ++col)
						row_buf[col] = val;
				}

				u8* dst = bgra.data() + (size_t)y * width * 4;

				for(int x = 0; x < width; ++x) {
					dst[x*4 + 0] = row_buf[2 * bpl + x];  // B
					dst[x*4 + 1] = row_buf[bpl + x];      // G
					dst[x*4 + 2] = row_buf[x];            // R
				}
			}

			return true;
		}

	}  // namespace

	//============================================================================
	// Write non-RLE pixel data (width*height bytes) to a PCX
	//============================================================================
	bool write_pixels(
		std::string_view path, 
		int width, 
		int height, 
		const u8* pixels, 
		const u8* palette_rgb
	) {

		std::ofstream file(std::string{path}, std::ios::binary);

		if(!file)
			return false;

		auto hdr = build_header(width, height);
		file.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
		u16 bpl = (u16)((width + 1) & ~1);

		for(int y = 0; y < height; ++y) {
			const u8* row = pixels + y * width;
			int x = 0;

			while(x < bpl) {
				u8 val = (x < width) ? row[x] : 0;
				int run = 1;

				while(x + run < bpl && run < 63) {
					u8 next = (x + run < width) ? row[x + run] : 0;

					if(next != val)
						break;

					++run;
				}

				if(run > 1 || (val & 0xC0) == 0xC0)
					file.put((char)(0xC0 | run));

				file.put((char)val);
				x += run;
			}
		}

		file.put((char)0x0C);  // palette marker
		file.write(reinterpret_cast<const char*>(palette_rgb), 768);

		return true;
	}

	//============================================================================
	// Write RLE-compressed pixel data (width*height bytes) to a PCX
	//============================================================================
	bool write_rle(
		std::string_view path, 
		int width, int height, 
		const u8* rle_data, 
		size_t rle_size, 
		const u8* palette_rgb
	) {

		std::ofstream file(std::string{path}, std::ios::binary);

		if(!file)
			return false;

		auto hdr = build_header(width, height);
		
		file.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
		file.write(reinterpret_cast<const char*>(rle_data), rle_size);
		file.put((char)0x0C);  // palette marker
		file.write(reinterpret_cast<const char*>(palette_rgb), 768);

		return true;
	}

	//============================================================================
	// Load PCX
	//============================================================================
	bool load(
		std::string_view pcx_path, 
		std::vector<u8>& pixels, 
		u8 palette_rgb[768], 
		int& width, 
		int& height
	) {
		
		auto buf = util::io::read_file(std::string{pcx_path});

		if(!buf)
			return false;

		return decode(buf->data(), buf->size(), pixels, width, height, palette_rgb);
	}

	//============================================================================
	// PCX to TGA. 1-plane = 8bpp indexed | 3-plane = 24bpp RGB truecolor
	//============================================================================
	bool to_tga(
		std::span<const u8> src_bytes,
		const std::string& dst_tga,
		const TextureSpec& spec
	) {

		if(src_bytes.size() < 70 || src_bytes[0] != 0x0A)
			return false;

		if(src_bytes[65] == 3) {

			std::vector<u8> bgra;
			int width  = 0;
			int height = 0;

			if(!decode_truecolor(src_bytes.data(), src_bytes.size(), bgra, width, height))
				return false;

			return tga::write_bgr24(dst_tga, width, height, bgra.data());
		}

		std::vector<u8> pixels;
		u8  pal[768];
		int width  = 0;
		int height = 0;

		if(!decode(src_bytes.data(), src_bytes.size(), pixels, width, height, pal))
			return false;

		u8 pal_rgba[1024];
		palette::expand_rgba(pal, pal_rgba);

		return tga::write_tga(width, height, pixels.data(), pal_rgba, dst_tga, spec);
	}

	bool to_tga(
		const std::string& src_pcx,
		const std::string& dst_tga,
		const TextureSpec& spec
	) {

		auto buf = util::io::read_file(src_pcx);
		if(!buf)
			return false;

		return to_tga(std::span<const u8>{buf->data(), buf->size()}, dst_tga, spec);
	}

	//============================================================================
	// Decode PCX bytes into pixels + palette + dimensions
	//============================================================================
	bool decode(
		std::span<const u8> src_bytes,
		std::vector<u8>&    out_pixels,
		u8                  out_palette[768],
		int&                out_width,
		int&                out_height,
		bool&               out_is_paletted
	) {

		if(src_bytes.size() < 70 || src_bytes[0] != 0x0A)
			return false;

		if(src_bytes[65] == 3) {

			std::vector<u8> bgra;
			if(!decode_truecolor(src_bytes.data(), src_bytes.size(), bgra, out_width, out_height))
				return false;

			out_pixels      = std::move(bgra);
			out_is_paletted = false;
			std::memset(out_palette, 0, 768);
			return true;
		}

		std::vector<u8> indexed;
		if(!decode(src_bytes.data(), src_bytes.size(), indexed, out_width, out_height, out_palette))
			return false;

		out_pixels      = std::move(indexed);
		out_is_paletted = true;
		return true;
	}

}
