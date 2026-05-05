#pragma once

#include "types.h"

#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace nova3di::util::cbin {

	struct Value {
		u32         flags;
		u32         raw;
		std::string str;

		float as_float() const {
			float f;
			memcpy(&f, &raw, 4);
			return f;
		}

		i32 as_int() const {
			return (i32)raw;
		}
	};

	struct Entry {
		std::string        key;
		std::vector<Value> values;
	};

	struct Section {
		std::string        name;
		std::vector<Entry> entries;

		const Entry* find     (const std::string& key, int occurrence = 0) const;
		std::string  get_str  (const std::string& key, int val_idx = 0, int occurrence = 0) const;
		float        get_float(const std::string& key, int val_idx = 0, int occurrence = 0) const;
		i32          get_int  (const std::string& key, int val_idx = 0, int occurrence = 0) const;
	};

	struct File {
		std::vector<Section> sections;

		const Section* find(const std::string& name) const;
	};

	bool parse(std::span<const u8> data, File& out);
}
