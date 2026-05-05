#include "bfc.h"

#include "io.h"

#include <cstring>

namespace nova3di::util::bfc {

	namespace {

		constexpr size_t BFC1_PREFIX_LEN = 10; // magic(4) + size(4) + zlib hdr(2)
		constexpr size_t BFC1_SUFFIX_LEN = 4;  // adler32

		//============================================================================
		// DEFLATE constants
		//============================================================================
		constexpr u16 LEN_BASE[] = {
			3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
			35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
		};

		constexpr u8 LEN_EXTRA[] = {
			0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
			3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
		};

		constexpr u16 DIST_BASE[] = {
			1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
			257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
		};

		constexpr u8 DIST_EXTRA[] = {
			0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
			7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
		};

		// Dynamic-Huffman code-length symbol order (RFC 1951 section 3.2.7).
		constexpr u8 CLEN_ORDER[] = {
			16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
		};

		//============================================================================
		// Huffman tables layout per RFC 1951
		//============================================================================
		struct HuffTable {
			u16 counts[16]{};
			u16 symbols[320]{};
		};

		// Build a canonical Huffman table from a code-length array.
		void build_table(HuffTable& t, const u16* lengths, int n) {

			for(int i = 0; i < n; ++i)
				t.counts[lengths[i]]++;

			t.counts[0] = 0;

			u16 offsets[16]{};

			for(int i = 1; i < 15; ++i)
				offsets[i + 1] = offsets[i] + t.counts[i];

			for(int i = 0; i < n; ++i) {

				if(lengths[i])
					t.symbols[offsets[lengths[i]]++] = (u16)i;

			}
		}

		HuffTable build_fixed_lit() {

			u16 lens[288];

			for(int i =   0; i <= 143; ++i) 
				lens[i] = 8;

			for(int i = 144; i <= 255; ++i) 
				lens[i] = 9;

			for(int i = 256; i <= 279; ++i) 
				lens[i] = 7;

			for(int i = 280; i <= 287; ++i) 
				lens[i] = 8;

			HuffTable t;
			build_table(t, lens, 288);

			return t;
		}

		HuffTable build_fixed_dist() {

			u16 lens[32];

			for(int i = 0; i < 32; ++i)
				lens[i] = 5;

			HuffTable t;
			build_table(t, lens, 32);

			return t;
		}

		//============================================================================
		// Bit stream + Huffman decoder
		//============================================================================
		struct BitStream {
			const u8* src;
			size_t    src_len;
			size_t    sp    = 0;
			u32       bits  = 0;
			int       nbits = 0;

			u32 need(int n) {

				while(nbits < n) {

					if(sp >= src_len)
						return 0;

					bits |= (u32)src[sp++] << nbits;
					nbits += 8;
				}

				u32 v = bits & ((1u << n) - 1);
				bits >>= n;
				nbits -= n;

				return v;
			}

			void align_byte() {
				bits  = 0;
				nbits = 0;
			}
		};

		int decode(BitStream& bs, const HuffTable& t) {

			int code  = 0;
			int first = 0;
			int idx   = 0;

			for(int len = 1; len <= 15; ++len) {

				code |= (int)bs.need(1);
				int count = t.counts[len];

				if(code < first + count)
					return t.symbols[idx + code - first];

				idx   += count;
				first  = (first + count) << 1;
				code <<= 1;
			}

			return -1;
		}

		//============================================================================
		// DEFLATE inflate RFC 1951
		//============================================================================
		bool inflate_raw(BitStream& bs, std::span<u8> dst, size_t& dst_written) {

			static const HuffTable fixed_lit  = build_fixed_lit();
			static const HuffTable fixed_dist = build_fixed_dist();

			size_t dp = 0;
			int bfinal;

			do {
				bfinal    = (int)bs.need(1);
				int btype = (int)bs.need(2);

				if(btype == 0) {

					// Stored block: byte-align, <len:2> <~len:2> <raw:len>.
					bs.align_byte();

					if(bs.sp + 4 > bs.src_len)
						return false;

					u16 len = bs.src[bs.sp] | (bs.src[bs.sp + 1] << 8);
					bs.sp += 4; // skip len + ~len

					if(bs.sp + len > bs.src_len || dp + len > dst.size())
						return false;

					memcpy(dst.data() + dp, bs.src + bs.sp, len);
					bs.sp += len;
					dp    += len;

				} else if(btype == 1 || btype == 2) {

					HuffTable lit_tab;
					HuffTable dist_tab;

					if(btype == 2) {

						int hlit  = (int)bs.need(5) + 257;
						int hdist = (int)bs.need(5) + 1;
						int hclen = (int)bs.need(4) + 4;

						u16 clen_lens[19] = {};

						for(int i = 0; i < hclen; ++i)
							clen_lens[CLEN_ORDER[i]] = (u16)bs.need(3);

						HuffTable clen_tab;
						build_table(clen_tab, clen_lens, 19);

						u16 all_lens[320] = {};
						int total = hlit + hdist;

						for(int i = 0; i < total; ) {

							int sym = decode(bs, clen_tab);

							if(sym < 16) {

								all_lens[i++] = (u16)sym;

							} else if(sym == 16) {

								int rep  = (int)bs.need(2) + 3;
								u16 prev = (i > 0) ? all_lens[i - 1] : 0;

								for(int r = 0; r < rep && i < total; ++r)
									all_lens[i++] = prev;

							} else if(sym == 17) {

								int rep = (int)bs.need(3) + 3;

								for(int r = 0; r < rep && i < total; ++r)
									all_lens[i++] = 0;

							} else {

								int rep = (int)bs.need(7) + 11;

								for(int r = 0; r < rep && i < total; ++r)
									all_lens[i++] = 0;

							}
						}

						build_table(lit_tab,  all_lens,         hlit);
						build_table(dist_tab, all_lens + hlit,  hdist);
					}

					const HuffTable& lit_table  = (btype == 1) ? fixed_lit  : lit_tab;
					const HuffTable& dist_table = (btype == 1) ? fixed_dist : dist_tab;

					for(;;) {

						int sym = decode(bs, lit_table);

						if(sym < 0)
							return false;

						if(sym < 256) {

							if(dp >= dst.size())
								return false;

							dst[dp++] = (u8)sym;

						} else if(sym == 256) {

							break;

						} else {

							int len_idx  = sym - 257;
							u32 length   = LEN_BASE[len_idx] + bs.need(LEN_EXTRA[len_idx]);
							int dist_idx = decode(bs, dist_table);

							if(dist_idx < 0)
								return false;

							u32 distance = DIST_BASE[dist_idx] + bs.need(DIST_EXTRA[dist_idx]);

							if(dp + length > dst.size() || distance > dp)
								return false;

							for(u32 j = 0; j < length; ++j) {
								dst[dp] = dst[dp - distance];
								++dp;
							}

						}

					}

				} else {
					return false;
				}

			} while(!bfinal);

			dst_written = dp;

			return true;
		}

	}  // namespace

	//============================================================================
	// "BFC1" + <uncompressed_size:4> <zlib_hdr:2> <deflate_stream:...> <adler32:4>
	//============================================================================
	std::vector<u8> decompress(std::span<const u8> data) {

		if(data.size() < BFC1_PREFIX_LEN + BFC1_SUFFIX_LEN)
			return {};

		if(memcmp(data.data(), "BFC1", 4) != 0)
			return {};

		u32 uncomp_size = io::read_u32(data.data() + 4);

		BitStream bs {
			.src     = data.data() + BFC1_PREFIX_LEN,
			.src_len = data.size() - BFC1_PREFIX_LEN - BFC1_SUFFIX_LEN,
		};

		std::vector<u8> out(uncomp_size);
		size_t written = 0;

		if(!inflate_raw(bs, out, written) || written != uncomp_size)
			return {};

		return out;
	}

}
