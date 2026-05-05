#include "lzp1.h"

#include <array>
#include <cstring>

namespace nova3di::util::lzp1 {

	namespace {

		//============================================================================
		// LZP1 file layout constants
		//============================================================================
		constexpr size_t HEADER_SIZE   = 12;     // "LZP1" + width + height
		constexpr size_t PALETTE_SIZE  = 768;    // 256 * 3 RGB
		constexpr size_t CERP_MAGIC_SIZE = 4;
		constexpr size_t CERP_DATA_SIZE  = 1024; // 256 * 4-byte runtime metadata

		//============================================================================
		// LZW decoder constants (GIF like)
		//============================================================================
		constexpr u16    LZW_CLEAR     = 0x100;
		constexpr u16    LZW_END       = 0x101;
		constexpr u16    LZW_FIRST     = 0x102;
		constexpr int    LZW_WIDTH_MIN = 9;
		constexpr int    LZW_WIDTH_MAX = 13;
		constexpr size_t LZW_DICT_CAP  = 1 << LZW_WIDTH_MAX;  // 8192

		//============================================================================
		// LSB-first within each byte, little-endian byte order
		//============================================================================
		struct BitStream {
			const u8* src;
			size_t    src_len;
			size_t    bit_pos = 0;

			u32 read(int n) {

				size_t byte_off    = bit_pos >> 3;
				int    bit_in_byte = (int)(bit_pos & 7);

				if(byte_off + 4 > src_len)
					return UINT32_MAX;

				u32 word;
				memcpy(&word, src + byte_off, 4);
				bit_pos += n;

				return (word >> bit_in_byte) & ((1u << n) - 1);
			}
		};

		//============================================================================
		// prefix code + suffix byte, 3-byte stride
		//============================================================================
		struct DictEntry {
			u16 prefix;
			u8  suffix;
		};

		//============================================================================
		// Walk the dictionary chain back to a literal, pushing suffix bytes into a
		// reverse buffer, then write them in forward order. The final string may be
		// longer than the remaining buffer space. Writes are capped at out_cap 
		// Returns the first byte of the result string for use in the KwKwK case 
		// and the next dictionary chain.
		//============================================================================
		bool write_string(
			u16                                       code,
			const std::array<DictEntry, LZW_DICT_CAP>& dict,
			std::vector<u8>&                          out,
			size_t                                    out_cap,
			u8&                                       first_byte
		) {

			u8 stack[LZW_DICT_CAP];
			size_t depth = 0;

			while(code >= LZW_FIRST) {

				if(depth >= LZW_DICT_CAP || code >= LZW_DICT_CAP)
					return false;

				stack[depth++] = dict[code].suffix;
				code = dict[code].prefix;
			}

			if(code >= LZW_CLEAR)
				return false;

			first_byte = (u8)code;

			if(out.size() < out_cap)
				out.push_back(first_byte);

			while(depth > 0 && out.size() < out_cap)
				out.push_back(stack[--depth]);

			return true;
		}

		//============================================================================
		// Writes `pixel_count` palette indices into `out`.
		//============================================================================
		bool lzw_decode(BitStream& bs, std::vector<u8>& out, size_t pixel_count) {

			std::array<DictEntry, LZW_DICT_CAP> dict{};

			int    code_width = LZW_WIDTH_MIN;
			size_t next_code  = LZW_FIRST;
			u16    prev_code  = 0;
			u8     prev_first = 0;
			bool   have_prev  = false;

			out.reserve(pixel_count);

			while(out.size() < pixel_count) {

				u32 raw = bs.read(code_width);

				if(raw == UINT32_MAX)
					return false;

				u16 code = (u16)raw;

				if(code == LZW_END)
					break;

				if(code == LZW_CLEAR) {
					code_width = LZW_WIDTH_MIN;
					next_code  = LZW_FIRST;
					have_prev  = false;
					continue;
				}

				u8 first_byte;

				if(!have_prev) {

					if(code >= LZW_CLEAR)
						return false;

					out.push_back((u8)code);
					first_byte = (u8)code;

				} else if(code < next_code) {

					if(!write_string(code, dict, out, pixel_count, first_byte))
						return false;

				} else if(code == next_code) {

					if(!write_string(prev_code, dict, out, pixel_count, first_byte))
						return false;

					if(out.size() < pixel_count)
						out.push_back(first_byte);

				} else {
					return false;
				}

				if(have_prev && next_code < LZW_DICT_CAP) {

					dict[next_code] = { prev_code, first_byte };
					++next_code;

					if(next_code == (size_t)(1 << code_width) && code_width < LZW_WIDTH_MAX)
						++code_width;

				}

				prev_code  = code;
				prev_first = first_byte;
				have_prev  = true;
			}

			(void)prev_first;
			return out.size() == pixel_count;
		}

	}  // namespace

	//============================================================================
	// "LZP1" <width:4> <height:4> <palette:768> ["CerP" <ext:1024>] <lzw_stream:...>
	//
	// CerP block (when present) is runtime metadata. Its 1028 bytes are skipped 
	// to locate the LZW stream offset
	//============================================================================
	Image decode(std::span<const u8> data) {

		Image image;

		if(data.size() < HEADER_SIZE + PALETTE_SIZE)
			return image;

		if(memcmp(data.data(), "LZP1", 4) != 0)
			return image;

		u32 width;
		u32 height;
		memcpy(&width,  data.data() + 4, 4);
		memcpy(&height, data.data() + 8, 4);

		size_t pixel_count = (size_t)width * height;

		if(width == 0 || height == 0 || pixel_count > LZW_DICT_CAP * 64)
			return image;

		size_t lzw_offset = HEADER_SIZE + PALETTE_SIZE;

		if(data.size() >= lzw_offset + CERP_MAGIC_SIZE
		   && memcmp(data.data() + lzw_offset, "CerP", 4) == 0) {

			lzw_offset += CERP_MAGIC_SIZE + CERP_DATA_SIZE;
		}

		if(data.size() < lzw_offset)
			return image;

		BitStream bs {
			.src     = data.data() + lzw_offset,
			.src_len = data.size() - lzw_offset,
		};

		std::vector<u8> pixels;

		if(!lzw_decode(bs, pixels, pixel_count))
			return image;

		image.width   = (int)width;
		image.height  = (int)height;
		image.palette.assign(data.data() + HEADER_SIZE, data.data() + HEADER_SIZE + PALETTE_SIZE);
		image.pixels  = std::move(pixels);

		return image;
	}

}
