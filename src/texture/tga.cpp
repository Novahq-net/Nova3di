#include "tga.h"

#include "../util/io.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace nova3di::texture::tga {

	//============================================================================
	// Normalize uncommon TGA (type 1 paletted, type 3 grayscale, type 9 RLE,
	// type 10 RLE truecolor, type 11 RLE grayscale) into a type-2 24/32bpp
	//============================================================================
	bool normalize(const u8* src, size_t src_len, std::vector<u8>& out) {

		if(src_len < 18 || memcmp(src, "DDS ", 4) == 0)
			return false;

		u8  id_len     = src[0];
		u8  cmap_type  = src[1];
		u8  img_type   = src[2];
		u16 cmap_first = util::io::read_u16(src + 3);
		u16 cmap_len   = util::io::read_u16(src + 5);
		u8  cmap_bits  = src[7];
		u16 w          = util::io::read_u16(src + 12);
		u16 h          = util::io::read_u16(src + 14);
		u8  depth      = src[16];
		u8  desc       = src[17];

		bool normalized = (img_type == 2 && (depth == 24 || depth == 32));

		bool supported = (img_type == 1  && depth == 8) ||
		                 (img_type == 3  && depth == 8) ||
		                 (img_type == 9  && depth == 8) ||
		                 (img_type == 10 && (depth == 16 || depth == 24 || depth == 32)) ||
		                 (img_type == 11 && depth == 8);

		if(normalized || !supported || w == 0 || h == 0)
			return false;

		size_t header_size = 18 + id_len;
		u8 cmap_bpp = (u8)(cmap_bits / 8);
		size_t cmap_size = (cmap_type == 1) ? (size_t)cmap_len * cmap_bpp : 0;
		const u8* cmap_data = (cmap_size > 0) ? (src + header_size) : nullptr;
		const u8* pixel_data = src + header_size + cmap_size;
		const u8* pixel_end  = src + src_len;
		size_t pixel_count = (size_t)w * h;

		u8 out_depth = 24;

		if((cmap_type == 1 && cmap_bits == 32) || depth == 32)
			out_depth = 32;

		size_t out_bpp = out_depth / 8;

		out.resize(18 + pixel_count * out_bpp);
		std::fill(out.begin(), out.begin() + 18, (u8)0);

		out[2]  = 2;
		out[12] = (u8)(w & 0xFF);
		out[13] = (u8)(w >> 8);
		out[14] = (u8)(h & 0xFF);
		out[15] = (u8)(h >> 8);
		out[16] = out_depth;
		out[17] = (u8)((desc & 0x20) | (out_depth == 32 ? 0x08 : 0x00));

		u8* dst = out.data() + 18;

		auto write_bgr = [&](size_t k, u8 b, u8 g, u8 r, u8 a) {
			u8* d = dst + k * out_bpp;
			d[0] = b; d[1] = g; d[2] = r;

			if(out_bpp == 4)
				d[3] = a;

		};

		auto decode_palette = [&](u8 idx, u8& b, u8& g, u8& r, u8& a) {
			a = 255;

			if(!cmap_data || idx < cmap_first || (idx - cmap_first) >= cmap_len) {
				b = g = r = 0;
				return;
			}

			const u8* e = cmap_data + (idx - cmap_first) * cmap_bpp;

			if(cmap_bpp == 2) {
				u16 c = (u16)e[0] | ((u16)e[1] << 8);
				b = (u8)(((c      ) & 0x1F) * 255 / 31);
				g = (u8)(((c >> 5 ) & 0x1F) * 255 / 31);
				r = (u8)(((c >> 10) & 0x1F) * 255 / 31);
			} else {
				b = e[0]; g = e[1]; r = e[2];
				if(cmap_bpp == 4)
					a = e[3];
			}
		};

		auto decode_truecolor = [&](const u8* e, int bpp, u8& b, u8& g, u8& r, u8& a) {
			a = 255;

			if(bpp == 2) {
				u16 c = (u16)e[0] | ((u16)e[1] << 8);
				b = (u8)(((c      ) & 0x1F) * 255 / 31);
				g = (u8)(((c >> 5 ) & 0x1F) * 255 / 31);
				r = (u8)(((c >> 10) & 0x1F) * 255 / 31);
			} else {
				b = e[0]; g = e[1]; r = e[2];
				if(bpp == 4)
					a = e[3];
			}
		};

		bool ok = true;

		if(img_type == 1) {
			if(pixel_data + pixel_count > pixel_end)
				ok = false;

			for(size_t i = 0; ok && i < pixel_count; ++i) {
				u8 b, g, r, a;
				decode_palette(pixel_data[i], b, g, r, a);
				write_bgr(i, b, g, r, a);
			}
		} else if(img_type == 3) {
			if(pixel_data + pixel_count > pixel_end)
				ok = false;

			for(size_t i = 0; ok && i < pixel_count; ++i) {
				u8 v = pixel_data[i];
				write_bgr(i, v, v, v, 255);
			}
		} else {
			int src_bpp = depth / 8;
			const u8* sp = pixel_data;
			size_t pi = 0;

			while(ok && pi < pixel_count) {
				if(sp >= pixel_end) {
					ok = false;
					break;
				}

				u8 ph = *sp++;
				int count = (ph & 0x7F) + 1;
				bool rle = (ph & 0x80) != 0;
				int consume = rle ? 1 : count;

				if(sp + (size_t)consume * src_bpp > pixel_end) {
					ok = false;
					break;
				}

				for(int k = 0; k < count && pi < pixel_count; ++k, ++pi) {
					const u8* e = sp + (rle ? 0 : k) * src_bpp;
					u8 b, g, r, a;

					if(img_type == 9) {
						decode_palette(e[0], b, g, r, a);
					} else if(img_type == 11) {
						b = g = r = e[0];
						a = 255;
					} else {
						decode_truecolor(e, src_bpp, b, g, r, a);
					}

					write_bgr(pi, b, g, r, a);
				}

				sp += (size_t)consume * src_bpp;
			}
		}

		if(!ok) {
			out.clear();
			return false;
		}

		return true;
	}

	//============================================================================
	// Write 24bpp BGR TGA
	//============================================================================
	bool write_bgr24(
		const std::string& path,
		int width,
		int height,
		const u8* bgra
	) {

		std::ofstream file(path, std::ios::binary);

		if(!file)
			return false;

		u8 hdr[18] = {0};

		hdr[2]  = 2;
		hdr[12] = (u8)(width & 0xFF);
		hdr[13] = (u8)((width >> 8) & 0xFF);
		hdr[14] = (u8)(height & 0xFF);
		hdr[15] = (u8)((height >> 8) & 0xFF);
		hdr[16] = 24;   // bits per pixel
		hdr[17] = 0x20; // top-left origin, no alpha bits

		file.write(reinterpret_cast<const char*>(hdr), 18);

		size_t npix = (size_t)width * height;

		for(size_t i = 0; i < npix; ++i)
			file.write(reinterpret_cast<const char*>(bgra + i*4), 3);

		return true;
	}

	//============================================================================
	// Write 32bpp BGRA TGA
	//============================================================================
	bool write_bgra32(
		const std::string& path,
		int width,
		int height,
		const u8* bgra
	) {

		std::ofstream file(path, std::ios::binary);

		if(!file)
			return false;

		u8 hdr[18] = {0};

		hdr[2]  = 2;
		hdr[12] = (u8)(width & 0xFF);
		hdr[13] = (u8)((width >> 8) & 0xFF);
		hdr[14] = (u8)(height & 0xFF);
		hdr[15] = (u8)((height >> 8) & 0xFF);
		hdr[16] = 32;   // bits per pixel
		hdr[17] = 0x28; // 8 alpha bits + top-left origin

		file.write(reinterpret_cast<const char*>(hdr), 18);
		file.write(reinterpret_cast<const char*>(bgra), (size_t)width * height * 4);

		return true;
	}

	//============================================================================
	// Bleed RGB from a 32bpp BGRA pixel into its zero-alpha neighbours
	//============================================================================
	void bleed_rgb(
		u8* bgra, std::vector<u8>& filled,
		std::vector<i32>& queue,
		int width,
		int height
	) {

		static const int dx[4] = { 1, -1, 0, 0 };
		static const int dy[4] = { 0, 0, 1, -1 };

		for(size_t head = 0; head < queue.size(); ++head) {

			i32 ci = queue[head];
			int cx = ci % width;
			int cy = ci / width;
			u8 r = bgra[ci*4+2];
			u8 g = bgra[ci*4+1];
			u8 b = bgra[ci*4+0];

			for(int k = 0; k < 4; ++k) {
				
				int nx = cx + dx[k], ny = cy + dy[k];

				if(nx < 0 || nx >= width || ny < 0 || ny >= height) 
					continue;

				size_t ni = (size_t)ny * width + nx;

				if(filled[ni]) 
					continue;

				bgra[ni*4+0] = b;
				bgra[ni*4+1] = g;
				bgra[ni*4+2] = r;
				filled[ni] = 1;
				
				queue.push_back((i32)ni);
			}
		}
	}

	//========================================================================
	// Binarize 32bpp BGRA alpha at cutoff, then bleed RGB from the surviving
	// alpha=255 pixels into the zeroed neighbours.
	//========================================================================
	void cutoff_alpha(u8* bgra, int w, int h, u8 cutoff, bool invert) {

		size_t npix = (size_t)w * h;

		for(size_t i = 0; i < npix; ++i) {
			u8 a = bgra[i*4 + 3];
			bool keep = invert ? (a <= cutoff) : (a > cutoff);
			bgra[i*4 + 3] = keep ? 255 : 0;
		}

		std::vector<u8> filled(npix, 0);
		std::vector<i32> queue;
		queue.reserve(npix);

		for(size_t i = 0; i < npix; ++i) {
			if(bgra[i*4 + 3] != 0) {
				filled[i] = 1;
				queue.push_back((i32)i);
			}
		}

		bleed_rgb(bgra, filled, queue, w, h);
	}

	//========================================================================
	// TGA writer
	//========================================================================
	bool write_tga(
		int width, 
		int height, 
		const u8* indices, 
		const u8* palette_rgba, 
		const std::string& out_tga, 
		const TextureSpec& spec
	) {
		
		if(width <= 0 || height <= 0) 
			return false;
			
		size_t npix = (size_t)width * height;

		// Pre-compute palette brightness when needed.
		u8 brightness[256] = {};
		if(spec.mode == AlphaMode::Luminance) {

			for(int i = 0; i < 256; ++i) {
				u32 sum = (u32)palette_rgba[i*4+0] + palette_rgba[i*4+1] + palette_rgba[i*4+2];
				brightness[i] = (u8)((85u * sum) >> 8);
			}

		}

		// For Shade, scan once to find the farthest-from-128 index.
		int shade_delta = 1;
		if(spec.mode == AlphaMode::Shade) {
			int m = 0;

			for(size_t i = 0; i < npix; ++i) {
				int d = std::abs((int)indices[i] - 128);

				if(d > m)
					m = d;

			}

			shade_delta = (m > 0) ? m : 1;
		}

		int mask_w = 0, mask_h = 0;
		const u8* mask_brightness_buf = nullptr;

		if(spec.mode == AlphaMode::Mask) {
			if(!spec.mask_brightness)
				return false;
			mask_w = spec.mask_brightness->width;
			mask_h = spec.mask_brightness->height;
			mask_brightness_buf = spec.mask_brightness->pixels.data();
		}

		std::vector<u8> bgra(npix * 4);
		std::vector<u8> filled(npix, 0);
		std::vector<i32> queue;

		queue.reserve(npix);

		for(size_t i = 0; i < npix; ++i) {
			u8  idx = indices[i];
			u8  pr  = palette_rgba[idx*4+0];
			u8  pg  = palette_rgba[idx*4+1];
			u8  pb  = palette_rgba[idx*4+2];

			// RGB source
			u8 r = pr;
			u8 g = pg;
			u8 b = pb;

			if(spec.rgb == RgbSource::Black) {
				r = 0;
				g = 0;
				b = 0;
			} else if(spec.rgb == RgbSource::Tint) {
				r = spec.tint_r; 
				g = spec.tint_g; 
				b = spec.tint_b;
			}
	
			// Palette[0] gets fixed RGB, opaque, bypassing the keying switch.
			// AF3 0x0900 textured-slot rule (HIND canopy isn't tinted like others).
			if(spec.idx0_rgb && idx == 0) {
				bgra[i*4+0] = (*spec.idx0_rgb)[2];
				bgra[i*4+1] = (*spec.idx0_rgb)[1];
				bgra[i*4+2] = (*spec.idx0_rgb)[0];
				bgra[i*4+3] = 255;
				filled[i] = 1;
				queue.push_back((i32)i);
				
				continue;
			}

			// Alpha keying
			u8 alpha = 255;
			bool transparent = false;

			switch(spec.mode) {
				case AlphaMode::Opaque:
					alpha = 255;
					break;
				case AlphaMode::Preserve:
					alpha = 255;
					break;
				case AlphaMode::IndexZero:
					transparent = (idx == 0);
					alpha = transparent ? 0 : 255;
					break;
				case AlphaMode::Luminance:
					alpha = brightness[idx];
					transparent = (alpha == 0);
					break;
				case AlphaMode::IndexAlpha:
					alpha = idx;
					transparent = false;
					break;
				case AlphaMode::Shade: {
					int d = std::abs((int)idx - 128);
					int a = d * 255 / shade_delta;
					alpha = (u8)a;
					transparent = (alpha == 0);
					r = g = b = idx;
					break;
				}

				case AlphaMode::Mask: {

					int x = (int)(i % (size_t)width);
					int y = (int)(i / (size_t)width);
					int mx = (mask_w == width) ? x : (int)((i64)x * mask_w / width);
					int my = (mask_h == height) ? y : (int)((i64)y * mask_h / height);

					if(mx >= mask_w) mx = mask_w - 1;
					if(my >= mask_h) my = mask_h - 1;

					alpha = mask_brightness_buf[(size_t)my * mask_w + mx];
					transparent = (alpha == 0);
					break;
				}

				case AlphaMode::Explicit:
					alpha = 255;
					break;

			}

			if(transparent && spec.rgb == RgbSource::Palette) {

				bgra[i*4+0] = bgra[i*4+1] = bgra[i*4+2] = bgra[i*4+3] = 0;

			} else {
				bgra[i*4+0] = b;
				bgra[i*4+1] = g;
				bgra[i*4+2] = r;
				bgra[i*4+3] = alpha;

				if(!transparent) {
					filled[i] = 1;
					queue.push_back((i32)i);
				}
			}
		}

		if(spec.bleed && spec.mode != AlphaMode::Opaque && spec.rgb == RgbSource::Palette)
			bleed_rgb(bgra.data(), filled, queue, width, height);

		bool any_alpha = false;

		if(spec.mode != AlphaMode::Opaque) {
			for(size_t i = 0; i < npix; ++i) {
				if(bgra[i*4+3] != 255) {
					any_alpha = true;
					break;
				}
			}
		}

		if(any_alpha)
			return write_bgra32(out_tga, width, height, bgra.data());

		return write_bgr24(out_tga, width, height, bgra.data());
	}

}
