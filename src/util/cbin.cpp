#include "cbin.h"

#include "io.h"

namespace nova3di::util::cbin {

	//============================================================================
	// Header (20 bytes):
	//   "CBIN"(4) | str_table_off(4) | str_table_size(4) | str_count(4) | key_seed(4)
	//
	// Encrypted body starting at offset 20 (XOR with rotating key):
	//   section_count(4)
	//   [section headers:  name_idx(4)  + entry_count(4)] * section_count
	//   [entry headers:    key_idx(4)   + val_count(4)  ] * (entry_count + 1 per section)
	//   [values:           raw(4)       + flags(4)      ] * val_count
	//============================================================================
	namespace {

		constexpr size_t HEADER_SIZE      = 20;
		constexpr size_t OFF_STR_TABLE    = 4;
		constexpr size_t OFF_STR_TABLE_SZ = 8;
		constexpr size_t OFF_STR_COUNT    = 12;
		constexpr size_t OFF_KEY_SEED     = 16;

		constexpr u32 VALUE_FLAG_STRING = 0x04;

		void decrypt_body(std::span<u8> buf, u32 key_seed) {

			u32 k = key_seed;

			for(size_t i = HEADER_SIZE; i < buf.size(); ++i) {
				k = (k << 7) | (k >> 25);
				buf[i] ^= (u8)(k & 0xFF);
			}
		}

		// Read `str_count` strings from the decrypted body, starting at `str_table_off`.
		std::vector<std::string> load_string_table(
			std::span<const u8> buf, 
			size_t str_table_off, 
			u32 str_count
		) {

			std::vector<std::string> strings;
			strings.reserve(str_count);

			size_t pos = str_table_off;

			for(u32 i = 0; i < str_count; ++i) {

				size_t end = pos;

				while(end < buf.size() && buf[end] != 0)
					++end;

				strings.emplace_back((const char*)buf.data() + pos, end - pos);
				pos = end + 1;
			}

			return strings;
		}

	}  // namespace

	//============================================================================
	// Lookup Entry
	//============================================================================
	const Entry* Section::find(const std::string& key, int occurrence) const {

		int n = 0;

		for(const auto& entry : entries) {

			if(io::iequals(entry.key, key)) {

				if(n++ == occurrence)
					return &entry;

			}
		}

		return nullptr;
	}

	//============================================================================
	// Lookup Section
	//============================================================================
	const Section* File::find(const std::string& name) const {

		for(const auto& section : sections) {

			if(io::iequals(section.name, name))
				return &section;

		}

		return nullptr;
	}

	//============================================================================
	// Value helpers. Returns empty string / 0 on any failure
	//============================================================================
	std::string Section::get_str(const std::string& key, int val_idx, int occurrence) const {

		const Entry* entry = find(key, occurrence);

		if(!entry || val_idx >= (int)entry->values.size())
			return "";

		return entry->values[val_idx].str;
	}

	float Section::get_float(const std::string& key, int val_idx, int occurrence) const {

		const Entry* entry = find(key, occurrence);

		if(!entry || val_idx >= (int)entry->values.size())
			return 0.0f;

		return entry->values[val_idx].as_float();
	}

	i32 Section::get_int(const std::string& key, int val_idx, int occurrence) const {

		const Entry* entry = find(key, occurrence);

		if(!entry || val_idx >= (int)entry->values.size())
			return 0;

		return entry->values[val_idx].as_int();
	}

	//============================================================================
	// Parse CBIN
	//============================================================================
	bool parse(std::span<const u8> data, File& out) {

		if(data.size() < HEADER_SIZE || memcmp(data.data(), "CBIN", 4) != 0)
			return false;

		u32 str_table_off  = io::read_u32(data.data() + OFF_STR_TABLE);
		u32 str_table_size = io::read_u32(data.data() + OFF_STR_TABLE_SZ);
		u32 str_count      = io::read_u32(data.data() + OFF_STR_COUNT);
		u32 key_seed       = io::read_u32(data.data() + OFF_KEY_SEED);

		if(str_table_off + str_table_size > data.size())
			return false;

		std::vector<u8> buf(data.begin(), data.end());

		decrypt_body(buf, key_seed);

		std::vector<std::string> strings = load_string_table(buf, str_table_off, str_count);

		auto get_str = [&](u32 idx) -> std::string {

			if(idx == 0 || idx > strings.size())
				return "";

			return strings[idx - 1];
		};

		// Sections
		size_t pos = HEADER_SIZE;

		if(pos + 4 > buf.size())
			return false;

		u32 section_count = io::read_u32(buf.data() + pos);
		pos += 4;

		struct EntrySection {
			std::string name;
			u32         entry_count;
		};

		std::vector<EntrySection> sec_hdrs(section_count);

		for(u32 i = 0; i < section_count; ++i) {

			if(pos + 8 > buf.size())
				return false;

			u32 name_idx  = io::read_u32(buf.data() + pos);
			u32 entry_cnt = io::read_u32(buf.data() + pos + 4);

			sec_hdrs[i].name        = get_str(name_idx);
			sec_hdrs[i].entry_count = entry_cnt;

			pos += 8;
		}

		// Entry key headers
		struct EntryKeys {
			std::string key;
			u32         val_count;
		};

		std::vector<std::vector<EntryKeys>> all_entries(section_count);

		for(u32 si = 0; si < section_count; ++si) {

			u32 num = sec_hdrs[si].entry_count ? (sec_hdrs[si].entry_count + 1) : 0;
			all_entries[si].resize(num);

			for(u32 ei = 0; ei < num; ++ei) {

				if(pos + 8 > buf.size())
					return false;

				u32 key_idx = io::read_u32(buf.data() + pos);
				u32 val_cnt = io::read_u32(buf.data() + pos + 4);

				all_entries[si][ei].key       = get_str(key_idx);
				all_entries[si][ei].val_count = val_cnt;

				pos += 8;
			}
		}

		// Values
		out.sections.resize(section_count);

		for(u32 si = 0; si < section_count; ++si) {

			out.sections[si].name = sec_hdrs[si].name;

			for(u32 ei = 0; ei < all_entries[si].size(); ++ei) {

				Entry entry;
				entry.key = all_entries[si][ei].key;

				u32 vc = all_entries[si][ei].val_count;
				entry.values.resize(vc);

				for(u32 vi = 0; vi < vc; ++vi) {

					if(pos + 8 > buf.size())
						return false;

					u32 raw_val = io::read_u32(buf.data() + pos);
					u32 flags   = io::read_u32(buf.data() + pos + 4);

					entry.values[vi].raw   = raw_val;
					entry.values[vi].flags = flags;
					entry.values[vi].str   = (flags & VALUE_FLAG_STRING) ? get_str(raw_val) : std::to_string(raw_val);

					pos += 8;
				}

				if(!entry.key.empty())
					out.sections[si].entries.push_back(std::move(entry));
			}
		}

		return true;
	}

}
