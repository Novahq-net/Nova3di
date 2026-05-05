#include "dds.h"

#include "tga.h"
#include "../util/io.h"

#include <cstring>
#include <vector>

namespace nova3di::texture::dds {

	namespace {

		namespace io = nova3di::util::io;

		//============================================================================
		// DDS header offsets + fourcc constants
		//============================================================================
		constexpr size_t DDS_HEADER_SIZE = 128;
		constexpr size_t OFF_HEIGHT      = 12;
		constexpr size_t OFF_WIDTH       = 16;
		constexpr size_t OFF_PF_FLAGS    = 80;
		constexpr size_t OFF_FOURCC      = 84;

		constexpr u32 DDPF_FOURCC = 0x04;

		// FourCC in little-endian byte order ("DXT1" -> 'D' | 'X'<<8 | 'T'<<16 | '1'<<24).
		constexpr u32 FOURCC_DXT1 = 0x31545844;
		constexpr u32 FOURCC_DXT3 = 0x33545844;
		constexpr u32 FOURCC_DXT5 = 0x35545844;

		//============================================================================
		// DXT decompression
		//============================================================================
		void decode_rgb565(u16 c, u8* r, u8* g, u8* b) {
			*r = (u8)(((c >> 11) & 0x1F) * 255 / 31);
			*g = (u8)(((c >> 5)  & 0x3F) * 255 / 63);
			*b = (u8)(( c        & 0x1F) * 255 / 31);
		}

		//============================================================================
		// Decode one 8-byte DXT colour block into a 4x4 BGRA tile
		//============================================================================
		void decode_color_block(const u8* block, u8 pixels[16 * 4], bool dxt1) {

			u16 c0 = io::read_u16(block);
			u16 c1 = io::read_u16(block + 2);

			u8 pal[4][3];

			decode_rgb565(c0, &pal[0][0], &pal[0][1], &pal[0][2]);
			decode_rgb565(c1, &pal[1][0], &pal[1][1], &pal[1][2]);

			if(!dxt1 || c0 > c1) {

				for(int i = 0; i < 3; ++i) {
					pal[2][i] = (u8)((2 * pal[0][i] + pal[1][i]) / 3);
					pal[3][i] = (u8)((pal[0][i] + 2 * pal[1][i]) / 3);
				}

			} else {

				for(int i = 0; i < 3; ++i) {
					pal[2][i] = (u8)((pal[0][i] + pal[1][i]) / 2);
					pal[3][i] = 0;
				}
			}

			u32 bits = io::read_u32(block + 4);

			for(int i = 0; i < 16; ++i) {

				int idx = (bits >> (i * 2)) & 3;

				pixels[i * 4 + 0] = pal[idx][2];
				pixels[i * 4 + 1] = pal[idx][1];
				pixels[i * 4 + 2] = pal[idx][0];
				pixels[i * 4 + 3] = 255;
			}
		}

		//============================================================================
		// Copy one decoded 4x4 DXT block into the full-image pixel buffer
		//============================================================================
		void blit_block(
			u8* pixels, 
			u32 width, 
			u32 height,
		    const u8 block[16 * 4], 
			u32 bx, 
			u32 by
		) {

			for(int py = 0; py < 4; ++py) {

				u32 dy = by * 4 + py;

				if(dy >= height)
					break;

				for(int px = 0; px < 4; ++px) {

					u32 dx = bx * 4 + px;

					if(dx >= width)
						break;

					memcpy(&pixels[(dy * width + dx) * 4], &block[(py * 4 + px) * 4], 4);
				}
			}
		}

		//============================================================================
		// DXT3 alpha block: 16 nibbles, one per pixel in row-major order
		// Each nibble is expanded to 8 bits via *17 (255/15)
		//============================================================================
		void decode_alpha_dxt3(const u8* block, u8 alphas[16]) {

			for(int i = 0; i < 16; ++i) {
				u8 nib = (block[i / 2] >> ((i & 1) * 4)) & 0x0F;
				alphas[i] = (u8)(nib * 17);
			}
		}

		//============================================================================
		// DXT5 alpha block: 2 endpoint bytes (a0, a1) build an 8-stop palette,
		// then 16 * 3-bit indices (48 bits in bytes 2..7) select per-pixel alpha
		// a0 > a1 = 6 interpolated stops; a0 <= a1 = 4 interpolated + {0, 255}
		//============================================================================
		void decode_alpha_dxt5(const u8* block, u8 alphas[16]) {

			u8 a0 = block[0];
			u8 a1 = block[1];

			u8 pal[8];
			pal[0] = a0;
			pal[1] = a1;

			if(a0 > a1) {

				for(int k = 2; k < 8; ++k)
					pal[k] = (u8)(((8 - k) * a0 + (k - 1) * a1) / 7);

			} else {

				for(int k = 2; k < 6; ++k)
					pal[k] = (u8)(((6 - k) * a0 + (k - 1) * a1) / 5);

				pal[6] = 0;
				pal[7] = 255;
			}

			u64 bits = 0;

			for(int i = 0; i < 6; ++i)
				bits |= ((u64)block[2 + i]) << (i * 8);

			for(int i = 0; i < 16; ++i) {
				int idx = (int)((bits >> (i * 3)) & 7);
				alphas[i] = pal[idx];
			}
		}

	}  // namespace

	//============================================================================
	// DXT1/3/5 > BGRA pixels
	//============================================================================
	bool decode(
		std::span<const u8> dds_bytes,
		std::vector<u8>&    out_bgra,
		int&                out_width,
		int&                out_height
	) {

		if(dds_bytes.size() < DDS_HEADER_SIZE || memcmp(dds_bytes.data(), "DDS ", 4) != 0)
			return false;

		const u8* dds = dds_bytes.data();

		u32 height   = io::read_u32(dds + OFF_HEIGHT);
		u32 width    = io::read_u32(dds + OFF_WIDTH);
		u32 pf_flags = io::read_u32(dds + OFF_PF_FLAGS);
		u32 fourcc   = io::read_u32(dds + OFF_FOURCC);

		if(!(pf_flags & DDPF_FOURCC))
			return false;

		int  block_size = 16;
		bool is_dxt1    = false;
		int  alpha_kind = 0; // 0 = none, 3 = DXT3 explicit, 5 = DXT5 interpolated

		switch(fourcc) {
			case FOURCC_DXT1: block_size = 8;  is_dxt1 = true; break;
			case FOURCC_DXT3: block_size = 16; alpha_kind = 3; break;
			case FOURCC_DXT5: block_size = 16; alpha_kind = 5; break;
			default: return false;
		}

		u32 bw = (width  + 3) / 4;
		u32 bh = (height + 3) / 4;

		size_t expected = DDS_HEADER_SIZE + (size_t)bw * bh * block_size;

		if(dds_bytes.size() < expected)
			return false;

		out_bgra.assign((size_t)width * height * 4, 0);
		const u8* src = dds + DDS_HEADER_SIZE;

		for(u32 by = 0; by < bh; ++by) {
			for(u32 bx = 0; bx < bw; ++bx) {

				u8 block_pixels[16 * 4];
				const u8* color_block = is_dxt1 ? src : src + 8;
				decode_color_block(color_block, block_pixels, is_dxt1);

				if(alpha_kind != 0) {
					u8 alphas[16];

					if(alpha_kind == 3)
						decode_alpha_dxt3(src, alphas);
					else
						decode_alpha_dxt5(src, alphas);

					for(int i = 0; i < 16; ++i)
						block_pixels[i * 4 + 3] = alphas[i];
				}

				src += block_size;

				blit_block(out_bgra.data(), width, height, block_pixels, bx, by);
			}
		}

		out_width  = (int)width;
		out_height = (int)height;
		return true;
	}

	//============================================================================
	// DXT1/3/5 > BGRA TGA, stripping alpha if necessary
	//============================================================================
	bool to_tga(
		std::span<const u8> dds_bytes,
		const std::string& tga_path,
		const TextureSpec& spec
	) {

		std::vector<u8> pixels;
		int width = 0, height = 0;

		if(!decode(dds_bytes, pixels, width, height))
			return false;

		// AlphaMode::Opaque -> 24bpp, no alpha, no bleed needed
		if(spec.mode == AlphaMode::Opaque)
			return tga::write_bgr24(tga_path, width, height, pixels.data());

		// AlphaMode::C > 0 -> binarize at threshold, then bleed RGB from the
		// surviving alpha=255 pixels into the zeroed neighbours
		if(spec.alpha_cutoff > 0) {

			tga::cutoff_alpha(
				pixels.data(), width, height,
				spec.alpha_cutoff, spec.alpha_cutoff_invert
			);

			return tga::write_bgra32(tga_path, width, height, pixels.data());
		}

		// bleed from any-alpha seeds into alpha=0 ixels so bilinear filtering 
		// doesn't pull stale RGB through transparent areas as a chroma fringe
		size_t npix = (size_t)width * height;
		std::vector<u8> filled(npix, 0);
		std::vector<i32> queue;
		queue.reserve(npix);

		for(size_t i = 0; i < npix; ++i) {
			if(pixels[i*4 + 3] > 0) {
				filled[i] = 1;
				queue.push_back((i32)i);
			}
		}

		tga::bleed_rgb(pixels.data(), filled, queue, width, height);

		return tga::write_bgra32(tga_path, width, height, pixels.data());
	}

}
