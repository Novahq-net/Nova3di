#pragma once

#include "types.h"

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nova3di::util::io {

	//============================================================================
	// Inline LE Binary readers
	//============================================================================
	inline u16 read_u16(const u8* p) {
		u16 v;
		memcpy(&v, p, 2);
		return v;
	}

	inline u32 read_u32(const u8* p) {
		u32 v;
		memcpy(&v, p, 4);
		return v;
	}

	std::string sanitize_name(const char* raw, int maxlen);
	std::string strip_ext(std::string_view str);
	std::string to_lower(std::string_view str);
	std::string to_upper(std::string_view str);
	std::string trim(std::string_view str);
	bool iequals(std::string_view str1, std::string_view str2);
	bool has_ext_ci(std::string_view path, std::string_view ext);

	std::string file_prefix(std::string_view name);
	std::string dirname(std::string_view path);
	std::string stem(std::string_view path);
	std::string extension(std::string_view path);

	void make_dirs(const std::string& out_dir);
	std::optional<std::vector<u8>> read_file(const std::string& path);
	std::string resolve_filename(const std::string& dir, const std::string& file_name);

	std::string open_file_dialog();

}
