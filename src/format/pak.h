#pragma once

#include "t3do.h"
#include "../model/converter.h"
#include "../texture/texture.h"
#include "../util/types.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace nova3di::format::pak {

	struct Part {
		std::string        name;
		t3do::Parsed3do    data;
		std::array<i32, 3> offset      = {0, 0, 0};
		size_t             byte_offset = 0;
	};

	struct ParsedPak {
		std::vector<u8>             bytes;
		std::string                 pak_name;
		std::string                 model_name;
		u16                         version = 0;
		u32                         lod_count = 0;
		u32                         off_tex_data = 0;
		std::vector<Part>           parts;
		std::array<u8, 768>         palette = {};
		bool                        has_palette = false;
		std::vector<texture::Image> textures;
	};

	bool parse_pak(
		std::vector<u8>&&             bytes,
		const std::string&            input_dir,
		std::string_view              pal_filename,
		std::string_view              pak_filename,
		const model::ConvertOptions&  opts,
		ParsedPak&                    pf
	);

	class Converter : public model::Converter {
	public:
		bool convert(
			const std::string& input_file,
			const model::ConvertOptions& opts
		) override;

		std::string_view format_name() const override {
			return "PAK";
		}
	};

}
