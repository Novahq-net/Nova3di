//============================================================================
//
//         ███╗   ██╗ ██████╗ ██╗   ██╗ █████╗ ██╗  ██╗ ██████╗
//         ████╗  ██║██╔═══██╗██║   ██║██╔══██╗██║  ██║██╔═══██╗
//         ██╔██╗ ██║██║   ██║██║   ██║███████║███████║██║   ██║
//         ██║╚██╗██║██║   ██║╚██╗ ██╔╝██╔══██║██╔══██║██║▄▄ ██║
//         ██║ ╚████║╚██████╔╝ ╚████╔╝ ██║  ██║██║  ██║╚██████╔╝
//         ╚═╝  ╚═══╝ ╚═════╝   ╚═══╝  ╚═╝  ╚═╝╚═╝  ╚═╝ ╚══▀▀═╝
//                  NovaHQ's 3DI/3DO to OBJ Converter
//                         https://novahq.net
//============================================================================
// Supported formats:
//   .3DI (v2-v10) - Delta Force, Delta Force 2, Land Warrior, Task Force Dagger,
//                   Armored Fist 3 (editor previews)
//   .3DI (3DI3)   - Joint Operations, Delta Force: Xtreme, Delta Force: Xtreme 2
//   .3DI (GPM)    - Black Hawk Down, Comanche 4
//   .3DO (3DO1)   - Armored Fist 3, Comanche 3 Gold,
//                   Tachyon: The Fringe, F-16 Multirole Fighter, MiG-29 Fulcrum,
//                   F-22 Lightning 3, F-22 Raptor, L3
//   .PAK (3DPK)   - Tachyon: The Fringe, F-16 Multirole Fighter, MiG-29 Fulcrum,
//                   F-22 Lightning 3, F-22 Raptor, L3
//   .OCF (3DO1)   - Tachyon: The Fringe (Object Config files)
//   .AI  (3DO1)   - Armored Fist 3 (Asset Index files)
//
// Usage:
//   Nova3di <file>                  Convert a single file
//   Nova3di <file> <out_dir>        Convert to specific output directory
//   Nova3di <directory>             Batch convert all models in a directory
//   Nova3di <directory> <out_dir>   Batch convert with output directory
//
// Options:
//   --collision     Export collision mesh as separate OBJ (GPM/3DI3)
//   --hardpoints    Export attachment points as text file (3DI/3DI3)
//   --extract       Extract embedded 3DOs + textures from a PAK (or OCF passthrough)
//   --effects       Include animated effect materials (shadow, glow, lights)
//   --lods          Include all LODs instead of just the highest-quality
//   --metadata      Write #nova: round-trip metadata comments in MTL
//   --env=N         Environment-variant selector for per-env texture binding (GPM only; 0-3, default 0)
//   --lod=N         LOD selector (GPM and 3DI; 0=highest detail, 3=lowest, default 0)
//   --3di           Batch: convert .3di files only
//   --3do           Batch: convert .3do files only
//   --pak           Batch: convert .pak files only
//   --ocf           Batch: convert .ocf files only
//   --ai            Batch: convert .ai files only
//   --log           Write warnings/errors to Nova3di.log
//
// Outputs:
//   <name>.obj          - Geometry (vertices, normals, UVs, faces)
//   <name>.mtl          - Materials referencing textures
//   *.pcx, *.tga, *.dds - Extracted/copied textures
//   *_collision.obj     - Collision mesh (--collision)
//   *_hardpoints.txt    - Attachment points (--hardpoints)
//============================================================================
#include "util/io.h"
#include "util/types.h"
#include "util/log.h"
#include "util/scr1.h"
#include "format/gpm.h"
#include "format/t3do.h"
#include "format/t3di3.h"
#include "format/t3di.h"
#include "format/pak.h"
#include "composite/ocf.h"
#include "composite/ai.h"
#include "model/converter.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nlog = nova3di::util::log;
namespace io = nova3di::util::io;
namespace scr1 = nova3di::util::scr1;

constexpr const char* APP_TITLE = "NovaHQ Nova3di";

//============================================================================
// filter_exts is empty when no --<format> flag was passed
//============================================================================
struct CliArgs {
	bool help       = false;
	bool log        = false;
	bool collision  = false;
	bool hardpoints = false;
	bool extract    = false;
	bool debug      = false;
	bool effects    = false;
	bool metadata   = false;
	bool lods       = false;
	bool raw        = false;
	int  env        = 0;
	int  lod        = 0;

	std::vector<std::string> filter_exts;
	std::vector<std::string> pos_args;
	std::string              out_dir;
};

//============================================================================
// Format-filter table
//============================================================================
struct FormatFilter {
	const char*                        cli_flag;
	std::initializer_list<const char*> extensions;
};

static const FormatFilter FORMAT_FILTERS[] = {
	{ "--3di", { ".3di" } },
	{ "--3do", { ".3do" } },
	{ "--pak", { ".pak" } },
	{ "--ocf", { ".ocf" } },
	{ "--ai",  { ".ai" } },
};

//============================================================================
// Parse argv into CliArgs. Unknown tokens go into pos_args.
//============================================================================
static CliArgs parse_args(int argc, char** argv) {

	CliArgs args;

	for(int i = 1; i < argc; ++i) {
		std::string_view arg = argv[i];

		bool matched_filter = false;

		for(const auto& f : FORMAT_FILTERS) {
			if(arg == f.cli_flag) {
				
				for(const char* ext : f.extensions)
					args.filter_exts.push_back(ext);

				matched_filter = true;
				break;
			}
		}

		if(matched_filter) 
			continue;

		if     (arg == "--help")       args.help = true;
		else if(arg == "--log")        args.log = true;
		else if(arg == "--collision")  args.collision  = true;
		else if(arg == "--hardpoints") args.hardpoints = true;
		else if(arg == "--extract")    args.extract    = true;
		else if(arg == "--debug")      args.debug      = true;
		else if(arg == "--effects")    args.effects    = true;
		else if(arg == "--metadata")   args.metadata   = true;
		else if(arg == "--lods")       args.lods       = true;
		else if(arg == "--raw")        args.raw        = true;
		else if(arg.rfind("--out=", 0) == 0)  args.out_dir = std::string(arg.substr(6));
		else if(arg.rfind("--env=", 0) == 0) {
			int env = std::atoi(std::string(arg.substr(6)).c_str());
			if(env < 0 || env > 3) {
				nlog::warn("--env=%d out of range (0-3)", env);
				env = 0;
			}
			args.env = env;
		}
		else if(arg.rfind("--lod=", 0) == 0) {
			int lod = std::atoi(std::string(arg.substr(6)).c_str());
			if(lod < 0 || lod > 3) {
				nlog::warn("--lod=%d out of range (0-3)", lod);
				lod = 0;
			}
			args.lod = lod;
		}
		else args.pos_args.push_back(argv[i]);
	}

	return args;
}

//============================================================================
// True when this process owns its console
//============================================================================
static bool standalone_console() {
	DWORD procs[2];
	return GetConsoleProcessList(procs, 2) <= 1;
}

//============================================================================
// Print help
//============================================================================
static void print_usage() {
	nlog::info("Usage: Nova3di [file_or_dir] [options]\n"
		   "  file_or_dir   Single model file (.3di, .3do, .pak, .ocf) or directory\n"
		   "  --out=<dir>   Output directory (default: exe directory)\n"
		   "  --collision   Export collision mesh (GPM/3DI3)\n"
		   "  --hardpoints  Export attachment points (3DI/3DI3)\n"
		   "  --extract     Extract embedded 3DOs + textures from PAK files (also via OCF)\n"
		   "  --effects     Include animated effect materials (shadows, glow, lights)\n"
		   "  --lods        Write one OBJ per LOD level (default: highest LOD only)\n"
		   "  --raw         Skip TGA generation; MTL references source texture files\n"
		   "  --debug       Write per-format debug visualization\n"
		   "  --env=N       Environment-variant texture binding (GPM only 0-3, default 0)\n"
		   "  --lod=N       LOD selector (GPM and 3DI; 0=highest, 3=lowest, default 0)\n"
		   "  --3di         Batch: convert .3di files only (no path = current dir)\n"
		   "  --3do         Batch: convert .3do files only\n"
		   "  --pak         Batch: convert .pak files only\n"
		   "  --ocf         Batch: convert .ocf files only\n"
		   "  --ai          Batch: convert .ai composition files only\n"
		   "  --log         Write warnings to Nova3di.log\n"
		   "  --help        Show this help and exit\n");
}

//============================================================================
// Resolve every file under `dir` whose extension matches the active filter 
// set in CliArgs. No filters = all supported model formats.
//============================================================================
static std::vector<std::string> resolve_models(const std::string& dir, const CliArgs& args) {

	// Empty filter list = all formats
	std::vector<std::string> extensions = args.filter_exts;

	if(extensions.empty()) {
		for(const auto& f : FORMAT_FILTERS) {

			for(const char* ext : f.extensions)
				extensions.push_back(ext);

		}
	}

	std::vector<std::string> files;
	std::error_code ec;

	for(const auto& incl : extensions) {
		for(const auto& entry : std::filesystem::directory_iterator(dir, ec)) {

			if(!entry.is_regular_file())
				continue;

			std::string ext = nova3di::util::io::to_lower(entry.path().extension().string());

			if(ext == incl)
				files.push_back(entry.path().string());

		}
	}

	return files;
}

//============================================================================
// Resolve the input-files list from CLI args
//   - files in the positional directory
//   - a single positional file
//   - files in "." when filters set but no path given
//   - a file dialog pick when running standalone and no args
//============================================================================
static std::vector<std::string> resolve_input_files(const CliArgs& args) {

	std::vector<std::string> input_files;

	if(args.pos_args.empty() && !args.filter_exts.empty()) {

		input_files = resolve_models(".", args);

	} else if(args.pos_args.empty()) {
		if(standalone_console()) {

			std::string picked = io::open_file_dialog();

			if(!picked.empty())
				input_files.push_back(picked);

		} else {
			print_usage();
		}

	} else {
		std::error_code ec;

		if(std::filesystem::is_directory(args.pos_args[0], ec))
			input_files = resolve_models(args.pos_args[0], args);

		else
			input_files.push_back(args.pos_args[0]);
	}

	return input_files;
}

//============================================================================
// Read, determine format, dispatch to the matching Converter
//============================================================================
static int convert_file(const std::string& input_file, const std::string& out_base, const CliArgs& args) {

	std::string out_dir = out_base + "\\" + io::stem(input_file);

	nlog::context(input_file.substr(input_file.find_last_of("/\\") + 1));

	auto file = io::read_file(input_file);

	if(!file) {
		nlog::error("cannot open '%s'", input_file.c_str());
		return 1;
	}

	std::vector<u8>& file_bytes = *file;
	size_t file_size = file_bytes.size();
	const u8* data = file_bytes.data();

	if(file_size < 8) {
		nlog::error("file too small");
		return 1;
	}

	// SCR1
	if(data[0] == 'S' && data[1] == 'C' && data[2] == 'R' && data[3] == 0x01)
		return scr1::convert(input_file, std::span<const u8>{data, file_size}, out_dir);

	nova3di::model::ConvertOptions opts;
	opts.out_dir    = out_dir;
	opts.collision  = args.collision;
	opts.hardpoints = args.hardpoints;
	opts.extract    = args.extract;
	opts.debug      = args.debug;
	opts.effects    = args.effects;
	opts.metadata   = args.metadata;
	opts.lods       = args.lods;
	opts.raw        = args.raw;
	opts.env        = args.env;
	opts.lod        = args.lod;

	switch(nova3di::model::detect(data, file_size, input_file)) {

		case nova3di::model::FileFormat::Ai:
			return nova3di::composite::ai::Converter{}.convert(input_file, opts) ? 0 : 1;

		case nova3di::model::FileFormat::Ocf:
			return nova3di::composite::ocf::Converter{}.convert(input_file, opts) ? 0 : 1;

		case nova3di::model::FileFormat::Gpm:
			return nova3di::format::gpm::Converter{}.convert(input_file, opts) ? 0 : 1;

		case nova3di::model::FileFormat::Pak:
			return nova3di::format::pak::Converter{}.convert(input_file, opts) ? 0 : 1;

		case nova3di::model::FileFormat::T3di3:
			return nova3di::format::t3di3::Converter{}.convert(input_file, opts) ? 0 : 1;

		case nova3di::model::FileFormat::T3do:
			return nova3di::format::t3do::Converter{}.convert(input_file, opts) ? 0 : 1;

		case nova3di::model::FileFormat::T3di:
			return nova3di::format::t3di::Converter{}.convert(input_file, opts) ? 0 : 1;

		case nova3di::model::FileFormat::Unknown:
			nlog::error("unsupported format (magic='%.4s')", (const char*)data);
			return 1;
	}

	return 1;
}

//============================================================================
// main
//============================================================================
int main(int argc, char** argv) {

	SetConsoleTitleA(APP_TITLE);

	CliArgs args = parse_args(argc, argv);

	if(args.help) {
		print_usage();
		return 0;
	}

	if(args.log)
		nlog::enable(io::dirname(std::string(argv[0])) + "\\" + io::stem(std::string(argv[0])) + ".log");

	std::vector<std::string> input_files = resolve_input_files(args);

	if(input_files.empty()) {
		nlog::info("No model files found\n");
		return 1;
	}

	// --out=, second positional arg, or exe directory as default
	std::string out_base;
	if(!args.out_dir.empty())
		out_base = args.out_dir;
	else if(args.pos_args.size() >= 2)
		out_base = args.pos_args[1];
	else
		out_base = io::dirname(std::string(argv[0]));

	int success = 0, fail = 0;
	for(const auto& f : input_files) {
		int result = convert_file(f, out_base, args);

		if(result == 0)
			++success;
		else
			++fail;

		nlog::flush();
	}

	if(input_files.size() > 1)
		nlog::info("Batch complete: %d converted, %d failed\n", success, fail);

	// Pause on error when run without console
	if(standalone_console() && fail > 0) {
		nlog::info("\nPress Enter to exit...");
		getchar();
	}

	return (fail > 0) ? 1 : 0;
}
