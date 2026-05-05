#include "io.h"

#include <filesystem>
#include <fstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

namespace nova3di::util::io {

	//============================================================================
	// String helpers
	//============================================================================
	std::string sanitize_name(const char* raw, int maxlen) {

		std::string str;

		for(int i = 0; i < maxlen && raw[i]; ++i) {

			char c = raw[i];

			if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
				str += c;
			else
				str += '_';
		}

		return str;
	}

	std::string strip_ext(std::string_view str) {
		size_t dot = str.find_last_of('.');
		return std::string(dot == std::string_view::npos ? str : str.substr(0, dot));
	}

	std::string to_lower(std::string_view str) {

		std::string result(str);

		for(auto& c : result)
			if(c >= 'A' && c <= 'Z')
				c += 32;

		return result;
	}

	std::string to_upper(std::string_view str) {

		std::string result(str);

		for(auto& c : result)
			if(c >= 'a' && c <= 'z')
				c -= 32;

		return result;
	}

	std::string trim(std::string_view str) {

		size_t start = 0;
		size_t end   = str.size();

		while(start < end && (str[start] == ' ' || str[start] == '\t' || str[start] == '\r' || str[start] == '\n'))
			++start;
		while(end > start && (str[end-1] == ' ' || str[end-1] == '\t' || str[end-1] == '\r' || str[end-1] == '\n'))
			--end;

		return std::string(str.substr(start, end - start));
	}

	bool iequals(std::string_view str1, std::string_view str2) {

		if(str1.size() != str2.size())
			return false;

		for(size_t i = 0; i < str1.size(); ++i) {

			char c1 = str1[i];
			char c2 = str2[i];

			if(c1 >= 'A' && c1 <= 'Z')
				c1 += 32;
			if(c2 >= 'A' && c2 <= 'Z')
				c2 += 32;

			if(c1 != c2)
				return false;
		}

		return true;
	}

	bool has_ext_ci(std::string_view path, std::string_view ext) {

		if(path.size() < ext.size())
			return false;

		return iequals(path.substr(path.size() - ext.size()), ext);
	}

	//============================================================================
	// Path utilities
	//============================================================================
	
	// Place an underscore before the first char in the name so the file
	// appears at the top in the directory listing.
	std::string file_prefix(std::string_view name) {

		size_t i = 0;

		while(i < name.size() && name[i] == '_')
			++i;

		return "_" + std::string(name.substr(i));
	}

	std::string dirname(std::string_view path) {

		size_t sep = path.find_last_of("/\\");

		return std::string(sep == std::string_view::npos ? "." : path.substr(0, sep));
	}

	// Filename without directory or extension.
	std::string stem(std::string_view path) {

		size_t sep = path.find_last_of("/\\");
		std::string_view fname = (sep == std::string_view::npos) ? path : path.substr(sep + 1);

		size_t dot = fname.find_last_of('.');

		return std::string(dot == std::string_view::npos ? fname : fname.substr(0, dot));
	}

	// Returns the lowercased file extension including the leading dot
	// (e.g. ".ai", ".ocf"), or "" if the path has no extension.
	std::string extension(std::string_view path) {

		size_t sep = path.find_last_of("/\\");
		std::string_view fname = (sep == std::string_view::npos) ? path : path.substr(sep + 1);

		size_t dot = fname.find_last_of('.');

		if(dot == std::string_view::npos)
			return {};

		return to_lower(fname.substr(dot));
	}

	//============================================================================
	// Filesystem wrappers
	//============================================================================
	void make_dirs(const std::string& out_dir) {

		std::error_code ec;
		std::filesystem::create_directories(out_dir, ec);
	}

	std::optional<std::vector<u8>> read_file(const std::string& path) {

		std::ifstream file(path, std::ios::binary | std::ios::ate);

		if(!file)
			return std::nullopt;

		std::streamsize size = file.tellg();
		file.seekg(0);

		std::vector<u8> buf((size_t)size);

		if(size > 0 && !file.read(reinterpret_cast<char*>(buf.data()), size))
			return std::nullopt;

		return buf;
	}

	std::string resolve_filename(const std::string& dir, const std::string& filename) {

		std::error_code ec;
		std::filesystem::path result = std::filesystem::canonical(std::filesystem::path(dir) / filename, ec);

		return ec ? "" : result.filename().string();
	}

	//============================================================================
	// Win32 file-open dialog
	//============================================================================
	std::string open_file_dialog() {

		char filename[MAX_PATH] = {};

		OPENFILENAMEA ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = "NovaLogic .3di, .3do, .pak, .ocf, .ai\0*.3di;*.3do;*.pak;*.ocf;*.ai\0All Files (*.*)\0*.*\0";
		ofn.lpstrFile   = filename;
		ofn.nMaxFile    = MAX_PATH;
		ofn.lpstrTitle  = "Select NovaLogic Model File";
		ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

		if(GetOpenFileNameA(&ofn))
			return std::string(filename);

		return "";
	}

}
