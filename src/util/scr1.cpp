#include "scr1.h"

#include "io.h"
#include "log.h"

#include <fstream>
#include <vector>

namespace nova3di::util::scr1 {

	namespace {

		namespace nlog = nova3di::util::log;
		namespace io   = nova3di::util::io;

		inline u32 rol32(u32 v, int n) {
			return (v << n) | (v >> (32 - n));
		}

		void decrypt(std::span<u8> data, u32 key) {

			for(size_t i = 0; i < data.size(); ++i) {

				key = rol32(key + rol32(key, 11), 4) ^ 1;
				data[i] ^= (u8)(key & 0xFF);
			}
		}

	}

	//============================================================================
	// SCR1:
	//   [optional "SCR1\x01" magic] <encrypted + byte-reversed payload>
	//
	// The decoder byte-reverses the payload first, then runs the stream cipher
	// with the supplied key. DF1 does not have SCR1 but it doesn't matter here.
	//============================================================================
	std::string decode(std::span<const u8> data, u32 key) {

		if(data.size() < 4)
			return "";

		size_t payload_off = 0;

		if(data[0] == 'S' && data[1] == 'C' && data[2] == 'R' && data[3] == 0x01)
			payload_off = 4;

		std::vector<u8> buf(data.begin() + payload_off, data.end());

		for(size_t i = 0, j = buf.size() - 1; i < j; ++i, --j)
			std::swap(buf[i], buf[j]);

		decrypt(buf, key);

		return std::string(buf.begin(), buf.end());
	}

	//============================================================================
	// SCR1 convert to txt
	//============================================================================
	int convert(const std::string& input_file, std::span<const u8> data, const std::string& out_dir) {

		static const struct {
			u32 key;
			const char* name;
		} keys[] = {
			{ KEY_BHD,     "BHD/DFX/JOP" },
			{ KEY_DF2,     "DF2/LW/TFD/C4" },
			{ KEY_FX,      "DFX/DFX2/JOP (.fx)" },
			{ KEY_JO_DEMO, "JOP Demo" },
			{ KEY_DF1,     "DF1" },
		};

		for(const auto& k : keys) {

			std::string text = decode(data, k.key);

			if(text.empty())
				continue;

			bool valid = true;
			int printable = 0;

			for(size_t i = 0; i < text.size() && i < 256; ++i) {
				u8 c = (u8)text[i];

				if(c == '\r' || c == '\n' || c == '\t' || (c >= 0x20 && c < 0x7F)) {
					++printable;
				} else if(c == 0) {
					break;
				} else {
					valid = false;
					break;
				}
			}

			if(!valid || printable < 10)
				continue;

			io::make_dirs(out_dir);
			std::string out_file = out_dir + "\\" + io::stem(input_file) + ".txt";
			std::ofstream fout(out_file, std::ios::binary);

			if(!fout) {
				nlog::error("cannot create '%s'", out_file.c_str());
				return 1;
			}

			fout.write(text.data(), text.size());

			std::string input_basename = input_file.substr(input_file.find_last_of("/\\") + 1);
			nlog::info("SCR1 %s: %s > %s\n", k.name, input_basename.c_str(), out_file.c_str());

			return 0;
		}

		nlog::error("SCR1 decryption failed (exhausted known keys)");

		return 1;
	}

}
