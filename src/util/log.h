#pragma once

#include <string>
#include <string_view>

namespace nova3di::util::log {


	void enable(const std::string& path);
	void context(const std::string& name);
	
	void flush();

	void error(const char* fmt, ...);
	void warn(const char* fmt, ...);

	void info(const char* fmt, ...);
	void step(const char* fmt, ...);

	void announce(
		std::string_view basename,
		std::string_view format_label,
		std::string_view details = {}
	);

}
