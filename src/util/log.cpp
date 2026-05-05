#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace nova3di::util::log {

	namespace {
		bool                     enabled = false;
		std::string              path;
		std::string              current_context;
		std::vector<std::string> entries;

		std::string vformat(const char* fmt, va_list args) {
			char buf[1024];
			
			vsnprintf(buf, sizeof(buf), fmt, args);
			return std::string(buf);
		}

		void write_stderr(const std::string& line) {
			fputs(line.c_str(), stderr);

			if(enabled)
				entries.push_back(line);

		}
	}

	void enable(const std::string& log_path) {
		enabled = true;
		path    = log_path;
	}

	void context(const std::string& name) {
		current_context = name;
	}

	void flush() {

		if(!enabled || entries.empty())
			return;

		std::ofstream file(path, std::ios::app);

		if(!file)
			return;

		if(!current_context.empty())
			file << std::format("[{}]\n", current_context);

		for(const auto& entry : entries)
			file << std::format("  {}", entry);

		file << '\n';

		entries.clear();
	}

	//============================================================================
	// "Error: <fmt>\n" to stderr + logfile.
	//============================================================================
	void error(const char* fmt, ...) {
		va_list args;
		va_start(args, fmt);
		std::string body = vformat(fmt, args);
		va_end(args);
		write_stderr("Error: " + body + "\n");
	}

	//============================================================================
	// "Warning: <fmt>\n" to stderr + logfile.
	//============================================================================
	void warn(const char* fmt, ...) {
		va_list args;
		va_start(args, fmt);
		std::string body = vformat(fmt, args);
		va_end(args);
		write_stderr("Warning: " + body + "\n");
	}

	//============================================================================
	// Progress: "<fmt>" to stdout.
	//============================================================================
	void info(const char* fmt, ...) {
		va_list args;
		va_start(args, fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);
	}

	//============================================================================
	// Progress-Step: "  <fmt>" to stdout (2-space indent).
	//============================================================================
	void step(const char* fmt, ...) {
		fputs("  ", stdout);
		va_list args;
		va_start(args, fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);
	}

	//============================================================================
	// Banner
	//============================================================================
	void announce(std::string_view basename, std::string_view format_label, std::string_view details) {
		static const std::string bar(60, '=');

		info("%s\n", bar.c_str());

		if(details.empty()) {

			info("%.*s (%.*s)\n",
				(int)basename.size(), basename.data(),
				(int)format_label.size(), format_label.data());

		} else {

			info("%.*s (%.*s) %.*s\n",
				(int)basename.size(), basename.data(),
				(int)format_label.size(), format_label.data(),
				(int)details.size(), details.data());

		}

		info("%s\n", bar.c_str());
	}

}
