#pragma once

#include "../model/converter.h"
#include "../texture/texture.h"
#include "../util/types.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace nova3di::model {
	struct Model;
}

namespace nova3di::format::t3do {

	struct Texture {
		std::string name;
		u8          alpha        = 0;
		u8          r            = 0;
		u8          g            = 0;
		u8          b            = 0;
		u8          uvp_u        = 0;
		u8          uvp_v        = 0;
		u16         render_flags = 0;
		u8          frame_count  = 0; 
		u8          frame_index  = 0;
	};

	struct Vertex {
		i32   x = 0;
		i32   y = 0;
		i32   z = 0;
		float u = 0;
		float v = 0;
	};

	struct Normal {
		float nx = 0;
		float ny = 0;
		float nz = 0;
	};

	struct Triangle {
		u32 vi[3]   = {};
		u32 ni[3]   = {};
		u32 tex_idx = 0;
	};

	struct Parsed3do {
		std::string model_name;

		u32 tex_count    = 0;
		u32 vert_count   = 0;
		u32 tri_count    = 0;
		u32 normal_count = 0;
		u32 mesh_count   = 0;
		u32 off_tex      = 0;
		u32 off_fv       = 0;
		u32 off_poly     = 0;
		u32 off_normals  = 0;
		u32 off_meshes   = 0;
		u32 off_hardpoints      = 0;
		u32 byte_size    = 0;

		std::vector<Texture>           textures;
		std::vector<Vertex>            vertices;
		std::vector<Normal>            normals;
		std::vector<Triangle>          triangles;
		std::vector<std::array<i32,3>> mesh_entries;
		std::vector<std::array<i32,3>> attach_points;

		// Resolved at parse time
		std::vector<texture::TextureSpec> specs;
		std::vector<u8>                   ex_textures;
		std::vector<u8>                   ex_geometry;

		std::vector<texture::Image>       resolved_textures;
	};


	bool parse_3do(
		std::span<const u8>             bytes,
		std::span<const texture::Image> pool,
		const std::string&              input_dir,
		const model::ConvertOptions&    opts,
		Parsed3do&                      parsed
	);

	void build_model(
		const Parsed3do& pf,
		size_t lod_idx,
		const model::BuildContext& ctx,
		model::Model& model);

	class Converter : public model::Converter {
	public:
		bool convert(
			const std::string& input_file,
			const model::ConvertOptions& opts
		) override;

		std::string_view format_name() const override {
			return "3DO";
		}
	};

}
