#pragma once

#include "filters.h"
#include "../texture/texture.h"
#include "../util/types.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nova3di::model {

	enum class FileFormat {
		T3di,     // .3DI v2-v10      (DF1, DF2, LW, TFD, AF3 editor)
		T3di3,    // .3DI 3DI3        (JOP, DFX, DFX2)
		Gpm,      // .3DI GPM/GPS/GPP (BHD, C4)
		T3do,     // .3DO 3DO1        (AF3, C3G)
		Pak,      // .PAK 3DPK        (TTF, F16, M29, F22, L3)
		Ocf,      // .OCF CBIN        (TTF)
		Ai,       // .AI  text        (AF3, C3G)
		Unknown,
	};

	struct ConvertOptions {
		std::string out_dir;
		bool collision  = false; // --collision: write collision mesh to <name>_collision.obj
		bool hardpoints = false; // --hardpoints: write attachment points to <name>_hardpoints.txt
		bool extract    = false; // --extract: extract textures and models
		bool debug      = false; // --debug: per-format extra debug view
		bool effects    = false; // --effects: keep effect textures/faces
		bool lods       = false; // --lods: write one OBJ per LOD
		bool raw        = false; // --raw: no TGA modifications, MTL refs source files
		bool metadata   = false; // --metadata: per-format #nova: comments in MTL
		int  env        = 0;     // --env=N: environment-variant selector for per-env texture binding (currently GPM only; values 0-3)
		int  lod        = 0;     // --lod=N: LOD selector (GPM and 3DI; 0=highest detail, 3=lowest)
	};

	//========================================================================
	// Which textures to drop
	//========================================================================
	struct TextureExclusions {
		std::vector<u8> drop_texture; // non-zero = drop
	};

	//========================================================================
	// On-disk filename for each texture (parallel to pf.textures)
	// Empty string = texture dropped or missing
	//========================================================================
	struct TextureOutput {
		std::vector<std::string> filenames;
	};

	struct BuildContext {
		const ConvertOptions&                      opts;
		std::string_view                           input_basename;
		std::string_view                           input_dir;
		model::filters::Scope                      scope;
		const TextureExclusions&                   texture_exclusions;
		std::span<const u8>                        geometry_exclusions;
		const TextureOutput&                       textures;
		std::span<const texture::TextureSpec>      texture_specs;
	};

	class Converter {
	public:
		virtual ~Converter() = default;

		virtual bool convert(const std::string& input_file, const ConvertOptions& opts) = 0;

		virtual std::string_view format_name() const = 0;
	};
	
	FileFormat detect(const u8* bytes, size_t size, std::string_view filename = {});

}
