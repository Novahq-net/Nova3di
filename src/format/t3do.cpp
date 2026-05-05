#include "t3do.h"

#include "../model/model.h"
#include "../texture/parse.h"
#include "../texture/write.h"
#include "../util/io.h"
#include "../util/log.h"
#include "../util/types.h"
#include "../writer/debug.h"
#include "../writer/export.h"
#include "../writer/extras.h"

#include <cstring>
#include <format>
#include <map>
#include <span>
#include <vector>

namespace nova3di::format::t3do {

	namespace {
		namespace nlog    = nova3di::util::log;
		namespace io      = nova3di::util::io;
		namespace texture = nova3di::texture;
		namespace filters = nova3di::model::filters;

		// File
		constexpr u32 T3DO_MAGIC            = 0x314F4433;
		constexpr u32 T3DO_VERSION_1_1      = 0x0101;
		constexpr u32 T3DO_HEADER_SIZE      = 92;
		constexpr u32 T3DO_OFF_MAGIC        = 0x00;
		constexpr u32 T3DO_OFF_VERSION      = 0x04;
		constexpr u32 T3DO_OFF_NAME         = 0x08;
		constexpr u32 T3DO_NAME_LEN         = 8;
		constexpr u32 T3DO_OFF_TEX_COUNT    = 0x2C;
		constexpr u32 T3DO_OFF_FV_COUNT     = 0x30;
		constexpr u32 T3DO_OFF_POLY_COUNT   = 0x34;
		constexpr u32 T3DO_OFF_NORMAL_COUNT = 0x38;
		constexpr u32 T3DO_OFF_MESH_COUNT   = 0x3C;
		constexpr u32 T3DO_OFF_ATTACH_COUNT = 0x40;
		constexpr u32 T3DO_OFF_TEX_PTR      = 0x44;
		constexpr u32 T3DO_OFF_FV_PTR       = 0x48;
		constexpr u32 T3DO_OFF_POLY_PTR     = 0x4C;
		constexpr u32 T3DO_OFF_NORMAL_PTR   = 0x50;
		constexpr u32 T3DO_OFF_MESH_PTR     = 0x54;
		constexpr u32 T3DO_OFF_HARDPOINTS_PTR      = 0x58;

		// Texture
		constexpr u32 TEX_ENTRY_SIZE        = 28;
		constexpr u32 TEX_OFF_NAME          = 0x00;
		constexpr u32 TEX_NAME_LEN          = 12;
		constexpr u32 TEX_OFF_ALPHA         = 0x0F;
		constexpr u32 TEX_OFF_R             = 0x10;
		constexpr u32 TEX_OFF_G             = 0x11;
		constexpr u32 TEX_OFF_B             = 0x12;
		constexpr u32 TEX_OFF_UVP_U         = 0x14;
		constexpr u32 TEX_OFF_UVP_V         = 0x15;
		constexpr u32 TEX_OFF_RENDER_FLAGS  = 0x16;
		constexpr u32 TEX_OFF_FRAME_COUNT   = 0x19;
		constexpr u32 TEX_OFF_FRAME_INDEX   = 0x1A;

		// Vertex
		constexpr u32   VERT_ENTRY_SIZE = 44;
		constexpr u32   VERT_OFF_X      = 0x00;
		constexpr u32   VERT_OFF_Y      = 0x04;
		constexpr u32   VERT_OFF_Z      = 0x08;
		constexpr u32   VERT_OFF_U      = 0x20;
		constexpr u32   VERT_OFF_V      = 0x24;
		constexpr float VERT_UV_SCALE   = 65536.0f;

		// Normal
		constexpr u32   NORM_ENTRY_SIZE = 16;
		constexpr u32   NORM_OFF_NX     = 0x00;
		constexpr u32   NORM_OFF_NY     = 0x04;
		constexpr u32   NORM_OFF_NZ     = 0x08;
		constexpr float NORM_INT_SCALE  = 32768.0f;

		// Poly (triangle definition entry)
		constexpr u32 POLY_ENTRY_SIZE = 32;
		constexpr u32 POLY_OFF_TEX    = 0x04;
		constexpr u32 POLY_OFF_VI     = 0x08;
		constexpr u32 POLY_OFF_NI     = 0x14;

		// Mesh
		constexpr u32 MESH_ENTRY_SIZE = 12;

		// Attach points
		constexpr u32 ATTACH_ENTRY_SIZE = 16;
		constexpr u32 ATTACH_OFF_X      = 0x00;
		constexpr u32 ATTACH_OFF_Y      = 0x04;
		constexpr u32 ATTACH_OFF_Z      = 0x08;

		//========================================================================
		// Parse texture entries (28B): char name[12], u8 alpha, u8 r,g,b, u8 uvp_u,v,
		//========================================================================
		void parse_textures(const u8* data, Parsed3do& pf) {

			pf.textures.resize(pf.tex_count);

			for(u32 i = 0; i < pf.tex_count; ++i) {
				const u8* tex_entry = data + pf.off_tex + i * TEX_ENTRY_SIZE;
				char buf[13] = {};
				memcpy(buf, tex_entry, 12);

				pf.textures[i].name         = io::sanitize_name(buf, TEX_NAME_LEN);
				pf.textures[i].alpha        = tex_entry[TEX_OFF_ALPHA];
				pf.textures[i].r            = tex_entry[TEX_OFF_R];
				pf.textures[i].g            = tex_entry[TEX_OFF_G];
				pf.textures[i].b            = tex_entry[TEX_OFF_B];
				pf.textures[i].uvp_u        = tex_entry[TEX_OFF_UVP_U];
				pf.textures[i].uvp_v        = tex_entry[TEX_OFF_UVP_V];
				pf.textures[i].render_flags = io::read_u16(tex_entry + TEX_OFF_RENDER_FLAGS);
				pf.textures[i].frame_count  = tex_entry[TEX_OFF_FRAME_COUNT];
				pf.textures[i].frame_index  = tex_entry[TEX_OFF_FRAME_INDEX];
			}
		}

		//========================================================================
		// Face verts: i32 xyz, u32 uv (Q16.16)
		//========================================================================
		void parse_vertices(const u8* data, Parsed3do& pf) {

			pf.vertices.resize(pf.vert_count);

			for(u32 i = 0; i < pf.vert_count; ++i) {
				const u8* fv_entry = data + pf.off_fv + i * VERT_ENTRY_SIZE;

				pf.vertices[i].x = (i32)io::read_u32(fv_entry + VERT_OFF_X);
				pf.vertices[i].y = (i32)io::read_u32(fv_entry + VERT_OFF_Y);
				pf.vertices[i].z = (i32)io::read_u32(fv_entry + VERT_OFF_Z);
				pf.vertices[i].u = (i32)io::read_u32(fv_entry + VERT_OFF_U) / VERT_UV_SCALE;
				pf.vertices[i].v = (i32)io::read_u32(fv_entry + VERT_OFF_V) / VERT_UV_SCALE;
			}
		}

		//========================================================================
		// Normals: i32 xyz normalised by 32768
		//========================================================================
		void parse_normals(const u8* data, Parsed3do& pf) {

			pf.normals.resize(pf.normal_count);

			for(u32 i = 0; i < pf.normal_count; ++i) {
				const u8* norm_entry = data + pf.off_normals + i * NORM_ENTRY_SIZE;

				pf.normals[i].nx = (i32)io::read_u32(norm_entry + NORM_OFF_NX) / NORM_INT_SCALE;
				pf.normals[i].ny = (i32)io::read_u32(norm_entry + NORM_OFF_NY) / NORM_INT_SCALE;
				pf.normals[i].nz = (i32)io::read_u32(norm_entry + NORM_OFF_NZ) / NORM_INT_SCALE;
			}
		}

		//========================================================================
		// Triangles: i32 tex, i32 vi[3], i32 ni[3]
		//========================================================================
		void parse_triangles(const u8* data, Parsed3do& pf) {

			pf.triangles.reserve(pf.tri_count);

			for(u32 i = 0; i < pf.tri_count; ++i) {
				const u8* poly_entry = data + pf.off_poly + i * POLY_ENTRY_SIZE;
				i32 tex_off = (i32)io::read_u32(poly_entry + POLY_OFF_TEX);
				i32 fv_a    = (i32)io::read_u32(poly_entry + POLY_OFF_VI + 0);
				i32 fv_b    = (i32)io::read_u32(poly_entry + POLY_OFF_VI + 4);
				i32 fv_c    = (i32)io::read_u32(poly_entry + POLY_OFF_VI + 8);
				i32 norm_a  = (i32)io::read_u32(poly_entry + POLY_OFF_NI + 0);
				i32 norm_b  = (i32)io::read_u32(poly_entry + POLY_OFF_NI + 4);
				i32 norm_c  = (i32)io::read_u32(poly_entry + POLY_OFF_NI + 8);

				Triangle tri;
				tri.tex_idx = (tex_off >= 0) ? (u32)(tex_off / TEX_ENTRY_SIZE) : 0;
				tri.vi[0]   = (u32)(fv_a / VERT_ENTRY_SIZE);
				tri.vi[1]   = (u32)(fv_b / VERT_ENTRY_SIZE);
				tri.vi[2]   = (u32)(fv_c / VERT_ENTRY_SIZE);
				tri.ni[0]   = (u32)(norm_a / NORM_ENTRY_SIZE);
				tri.ni[1]   = (u32)(norm_b / NORM_ENTRY_SIZE);
				tri.ni[2]   = (u32)(norm_c / NORM_ENTRY_SIZE);

				if(tri.vi[0] == tri.vi[1] || tri.vi[1] == tri.vi[2] || tri.vi[0] == tri.vi[2])
					continue;

				if(tri.vi[0] < pf.vertices.size() && tri.vi[1] < pf.vertices.size() && tri.vi[2] < pf.vertices.size()) {
					const Vertex& va = pf.vertices[tri.vi[0]];
					const Vertex& vb = pf.vertices[tri.vi[1]];
					const Vertex& vc = pf.vertices[tri.vi[2]];

					auto same_pos = [](const Vertex& u, const Vertex& v) {
						return u.x == v.x && u.y == v.y && u.z == v.z;
					};

					if(same_pos(va, vb) || same_pos(vb, vc) || same_pos(va, vc))
						continue;
				}

				pf.triangles.push_back(tri);
			}

		}

		//========================================================================
		// Mesh entries: i32 xyz triples used by composite formats (PAK, AI, OCF)
		//========================================================================
		void parse_mesh_entries(const u8* data, Parsed3do& pf) {

			if(pf.mesh_count == 0)
				return;

			u32 mesh_entry_size = (pf.off_hardpoints > pf.off_meshes) ? (pf.off_hardpoints - pf.off_meshes) / pf.mesh_count : MESH_ENTRY_SIZE;
			pf.mesh_entries.reserve(pf.mesh_count);

			for(u32 i = 0; i < pf.mesh_count; ++i) {
				const u8* mesh_entry = data + pf.off_meshes + i * mesh_entry_size;

				pf.mesh_entries.push_back({
					(i32)io::read_u32(mesh_entry + 0),
					(i32)io::read_u32(mesh_entry + 4),
					(i32)io::read_u32(mesh_entry + 8)
				});

			}
		}

		//========================================================================
		// Parse attach points: i32 xyz triples at off_hardpoints
		//========================================================================
		void parse_attach_points(const u8* data, size_t file_size, Parsed3do& pf) {

			u32 attach_count = io::read_u32(data + T3DO_OFF_ATTACH_COUNT);

			if(attach_count == 0)
				return;

			size_t attach_off = pf.off_hardpoints;
			pf.attach_points.reserve(attach_count);

			for(u32 i = 0; i < attach_count && attach_off + ATTACH_ENTRY_SIZE <= file_size; ++i) {
				i32 file_x = (i32)io::read_u32(data + attach_off + ATTACH_OFF_X);
				i32 file_y = (i32)io::read_u32(data + attach_off + ATTACH_OFF_Y);
				i32 file_z = (i32)io::read_u32(data + attach_off + ATTACH_OFF_Z);

				pf.attach_points.push_back({file_x, file_z, -file_y});
				attach_off += ATTACH_ENTRY_SIZE;
			}

		}

		//========================================================================
		// Register one Material per texture or a default if none
		//========================================================================
		void build_materials(
			const Parsed3do& pf,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			for(size_t i = 0; i < pf.textures.size(); ++i) {

				std::string base = io::strip_ext(pf.textures[i].name);
				std::string suffix;

				if(i < ctx.texture_specs.size())
					suffix = std::string(texture::mode_suffix(ctx.texture_specs[i].mode));

				model::Material mat;
				mat.name = base + suffix;
				mat.color[0] = mat.color[1] = mat.color[2] = 0.8f;

				if(i < ctx.textures.filenames.size()) {
					const std::string& filename = ctx.textures.filenames[i];

					if(!filename.empty()) {
						mat.texture = filename;

						if(io::extension(filename) == ".tga")
							mat.alpha = filename;

					}
				}

				model.materials.push_back(mat);
			}

			if(pf.textures.empty()) {
				model::Material default_mat;
				default_mat.name = "default";
				default_mat.color[0] = default_mat.color[1] = default_mat.color[2] = 128;
				model.materials.push_back(default_mat);
			}
		}

		//========================================================================
		// Build a single Mesh holding all geometry
		//========================================================================
		void build_meshes(
			const Parsed3do& pf,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			model.meshes.emplace_back();
			auto& mesh = model.meshes.back();
			mesh.group_name = "mesh_0";

			std::map<u32, u32> v_local;
			std::map<u32, u32> n_local;

			auto add_vertex = [&](u32 src_idx) -> u32 {
				auto it = v_local.find(src_idx);

				if(it != v_local.end())
					return it->second;

				const Vertex& vert = pf.vertices[src_idx];
				u32 idx = (u32)mesh.positions.size();

				mesh.positions.push_back({(float)-vert.x, (float)vert.z, (float)-vert.y});
				mesh.uvs.push_back({vert.u, 1.0f - vert.v});
				v_local[src_idx] = idx;

				return idx;
			};

			auto add_normal = [&](u32 src_idx) -> u32 {
				auto it = n_local.find(src_idx);

				if(it != n_local.end())
					return it->second;

				const Normal& norm = pf.normals[src_idx];
				u32 idx = (u32)mesh.normals.size();
				mesh.normals.push_back({-norm.nx, norm.nz, -norm.ny});
				n_local[src_idx] = idx;

				return idx;
			};

			for(size_t tri_idx = 0; tri_idx < pf.triangles.size(); ++tri_idx) {

				if(tri_idx < ctx.geometry_exclusions.size() && ctx.geometry_exclusions[tri_idx])
					continue;

				const auto& tri = pf.triangles[tri_idx];

				u32 pos_a  = add_vertex(tri.vi[0]);
				u32 pos_c  = add_vertex(tri.vi[2]);
				u32 pos_b  = add_vertex(tri.vi[1]);
				u32 norm_a = add_normal(tri.ni[0]);
				u32 norm_c = add_normal(tri.ni[2]);
				u32 norm_b = add_normal(tri.ni[1]);

				model::Face face = {};
				face.v[0] = {(i32)pos_a, (i32)norm_a, (i32)pos_a};
				face.v[1] = {(i32)pos_c, (i32)norm_c, (i32)pos_c};
				face.v[2] = {(i32)pos_b, (i32)norm_b, (i32)pos_b};

				face.material = (!pf.textures.empty()) ? tri.tex_idx : 0;  // 0 == "default" mat
				mesh.faces.push_back(face);

			}
		}

		//========================================================================
		// Apply the same axis swap as build_meshes so positions match the OBJ
		//========================================================================
		bool build_hardpoints(const Parsed3do& pf, model::Model& model) {

			for(size_t i = 0; i < pf.mesh_entries.size(); ++i) {

				const auto& src = pf.mesh_entries[i];
				model::Hardpoint h;

				h.name = "hardpoint_" + std::to_string(i);

				h.pos  = {
					-(float)src[0],
					 (float)src[2],
					-(float)src[1],
				};

				model.hardpoints.push_back(std::move(h));

			}

			return !model.hardpoints.empty();
		}

		//========================================================================
		// Byte parse: header + bulk arrays + attach_points. Dialect-invariant.
		//========================================================================
		bool parse_bytes(
			std::span<const u8> bytes,
			Parsed3do& pf
		) {

		const u8* data = bytes.data();
		size_t file_size = bytes.size();

		if(file_size < T3DO_HEADER_SIZE)
			return false;

		if(io::read_u32(data + T3DO_OFF_MAGIC)   != T3DO_MAGIC ||
		   io::read_u32(data + T3DO_OFF_VERSION) != T3DO_VERSION_1_1)
			return false;

		char name_buf[T3DO_NAME_LEN + 1] = {};
		memcpy(name_buf, data + T3DO_OFF_NAME, T3DO_NAME_LEN);

		for(int i = (int)T3DO_NAME_LEN - 1; i >= 0 && name_buf[i] == ' '; --i)
			name_buf[i] = 0;

		pf.model_name = io::sanitize_name(name_buf, T3DO_NAME_LEN);

		if(pf.model_name.empty())
			pf.model_name = "model";

		pf.tex_count    = io::read_u32(data + T3DO_OFF_TEX_COUNT);
		pf.vert_count   = io::read_u32(data + T3DO_OFF_FV_COUNT);
		pf.tri_count    = io::read_u32(data + T3DO_OFF_POLY_COUNT);
		pf.normal_count = io::read_u32(data + T3DO_OFF_NORMAL_COUNT);
		pf.mesh_count   = io::read_u32(data + T3DO_OFF_MESH_COUNT);
		pf.off_tex      = io::read_u32(data + T3DO_OFF_TEX_PTR);
		pf.off_fv       = io::read_u32(data + T3DO_OFF_FV_PTR);
		pf.off_poly     = io::read_u32(data + T3DO_OFF_POLY_PTR);
		pf.off_normals  = io::read_u32(data + T3DO_OFF_NORMAL_PTR);
		pf.off_meshes   = io::read_u32(data + T3DO_OFF_MESH_PTR);
		pf.off_hardpoints      = io::read_u32(data + T3DO_OFF_HARDPOINTS_PTR);

		u32 attach_count = io::read_u32(data + T3DO_OFF_ATTACH_COUNT);
		pf.byte_size = pf.off_hardpoints + attach_count * ATTACH_ENTRY_SIZE;

		if(pf.byte_size > file_size)
			pf.byte_size = (u32)file_size;

		if(pf.vert_count == 0 && pf.tri_count == 0)
			return true;

		if(pf.off_hardpoints > file_size + 16)
			return false;

		parse_textures(data, pf);
		parse_vertices(data, pf);
		parse_normals(data, pf);
		parse_triangles(data, pf);
		parse_mesh_entries(data, pf);
		parse_attach_points(data, file_size, pf);

		return true;
	}

	//========================================================================
	// Texture exclusion
	//========================================================================
	model::TextureExclusions collect_texture_exclusions(
		const Parsed3do& pf, 
		filters::Scope scope, 
		const model::ConvertOptions& opts
	) {

		model::TextureExclusions tex_ex;

		tex_ex.drop_texture.assign(pf.textures.size(), 0);

		if(opts.raw)
			return tex_ex;

		if(opts.effects)
			return tex_ex;

		for(size_t i = 0; i < pf.textures.size(); ++i) {
			const auto& tex = pf.textures[i];

			if(!tex.name.empty()) {

				if(filters::is_shadow(scope, tex.name)
				   || filters::is_effect(scope, tex.name))
					tex_ex.drop_texture[i] = 1;

				continue;
			}

			// Hidden at rest animation
			if((tex.render_flags & 0x0100) != 0
				&& (tex.render_flags & 0x0040) == 0
				&& (tex.alpha == 0x80 || tex.frame_count > 0)) {

				tex_ex.drop_texture[i] = 1;

				for(size_t k = 1; k < tex.frame_count && i + k < pf.textures.size(); ++k)
					tex_ex.drop_texture[i + k] = 1;

			}

		}

		return tex_ex;
	}

	//========================================================================
	// Geometry exclusions (per-LOD)
	//========================================================================
	std::vector<u8> collect_geometry_exclusions(
		const Parsed3do& pf, 
		size_t /*lod_idx*/, 
		const model::TextureExclusions& tex_ex, 
		filters::Scope, 
		const model::ConvertOptions& opts
	) {

		std::vector<u8> drop(pf.triangles.size(), 0);

		if(opts.raw)
			return drop;

		for(size_t tri_idx = 0; tri_idx < pf.triangles.size(); ++tri_idx) {
			u32 tex_idx = pf.triangles[tri_idx].tex_idx;

			if(tex_idx < tex_ex.drop_texture.size() && tex_ex.drop_texture[tex_idx])
				drop[tri_idx] = 1;

		}

		return drop;
	}

	//========================================================================
	// Texture Spec
	//========================================================================
	std::vector<texture::TextureSpec> texture_specs(
		const Parsed3do& pf,
		const model::TextureExclusions&,
		filters::Scope scope,
		const model::ConvertOptions& opts
	) {

		std::vector<texture::TextureSpec> specs(pf.textures.size());

		// Raw mode: every spec stays default-Opaque so mode_suffix returns ""
		// and material/filename/lookup names collapse to the original.
		if(opts.raw)
			return specs;

		for(size_t i = 0; i < pf.textures.size(); ++i) {

			const auto& tex = pf.textures[i];
			u16 rf = tex.render_flags;
			auto& spec = specs[i];

			switch(scope) {

				case filters::Scope::Pak: {
					// F16/M29/F22/F22IBS/L3/TTF
					//   rf & 0x0098 == 0x0088 -> brightness-blend ramp (record 4/7)
					//   rf & 0x0010           -> record 1 pal[0] colour key
					//   else                  -> record 0 opaque (engine writes pal[0] direct, no key)
					bool brightness_blend = (rf & 0x0098) == 0x0088;
					bool keys_pal0        = (rf & 0x0010) != 0;

					if(brightness_blend)
						spec.mode = texture::AlphaMode::Shade;
					else if(keys_pal0)
						spec.mode = texture::AlphaMode::IndexZero;
					else
						spec.mode = texture::AlphaMode::Opaque;

					// Canopy tint blend-layer flag set without ALPHA_KEY/ANIM
					// bits, OR (0x0100 + 0x0800) set with other low-bits clear.
					bool canopy_tint = ((rf & 0x01CB) == 0x0140)
						|| ((rf & 0x09CB) == 0x0900);

					if(canopy_tint)
						spec.opacity = 0.44f;

					break;
				}

				case filters::Scope::T3do: {
					// Standalone .3DO (AF3/C3G) and AI composite. The AF3
					// rasterizer uses bit 0x0080 to flag brightness-blend
					// (cow tail / D_PILOT shadows) and rf 0x0900 with non-0x80
					// alpha to flag a palette-fill canopy (HIND).
					if((rf & 0x0080) != 0)
						spec.mode = texture::AlphaMode::Shade;
					else
						spec.mode = texture::AlphaMode::IndexZero;

					spec.rgb = texture::RgbSource::Palette;

					if((rf & 0x0900) == 0x0900 && tex.alpha != 0x80)
						spec.idx0_rgb = std::array<u8,3>{tex.r, tex.g, tex.b};

					break;
				}

			}

		}

		return specs;
	}

	}  // namespace

	//========================================================================
	// Parse a 3DO file from embedded pak or solo file
	//========================================================================
	bool parse_3do(
		std::span<const u8>             bytes,
		std::span<const texture::Image> pool,
		const std::string&              input_dir,
		const model::ConvertOptions&    opts,
		Parsed3do&                      parsed
	) {

		if(!parse_bytes(bytes, parsed))
			return false;

		filters::Scope scope = pool.empty() ? filters::Scope::T3do : filters::Scope::Pak;

		auto tex_ex = collect_texture_exclusions(parsed, scope, opts);
		auto specs  = texture_specs(parsed, tex_ex, scope, opts);
		auto geo_ex = collect_geometry_exclusions(parsed, 0, tex_ex, scope, opts);

		parsed.specs.assign(specs.begin(), specs.end());
		parsed.ex_textures.assign(tex_ex.drop_texture.begin(), tex_ex.drop_texture.end());
		parsed.ex_geometry = std::move(geo_ex);

		auto find_in_pool = [&](const std::string& name) -> const texture::Image* {
			std::string base_lc = io::to_lower(io::strip_ext(name));

			for(const auto& img : pool) {

				if(io::to_lower(img.name) == base_lc)
					return &img;

			}

			return nullptr;
		};

		parsed.resolved_textures.clear();
		parsed.resolved_textures.reserve(parsed.textures.size());

		for(size_t i = 0; i < parsed.textures.size(); ++i) {

			const auto& tex = parsed.textures[i];

			if(tex.name.empty())
				continue;

			if(i < parsed.ex_textures.size() && parsed.ex_textures[i])
				continue;

			texture::Image image;

			if(!pool.empty()) {

				const texture::Image* src = find_in_pool(tex.name);

				if(!src || src->width <= 0)
					continue;

				image = *src;

			} else {

				if(!texture::parse(image, input_dir, tex.name))
					continue;

				image.name = io::strip_ext(tex.name);
			}

			image.spec = parsed.specs[i];
			image.name = io::strip_ext(image.name) + std::string(texture::mode_suffix(image.spec.mode));

			parsed.resolved_textures.push_back(std::move(image));
		}

		return true;
	}

	//========================================================================
	// Materials + single combined mesh
	//========================================================================
	void build_model(
		const Parsed3do& pf, size_t /*lod_idx*/, 
		const model::BuildContext& ctx, 
		model::Model& model
	) {

		model.source_name = std::string(ctx.input_basename) + " (3DO v1.1)";
		model.format_tag  = "3do_v1.1";

		build_materials(pf, ctx, model);
		build_meshes(pf, ctx, model);
	}

	//========================================================================
	// Converter
	//========================================================================
	bool Converter::convert(
		const std::string& input_file, 
		const model::ConvertOptions& opts
	) {

		auto file = io::read_file(input_file);

		if(!file) {
			nlog::error("cannot open '%s'", input_file.c_str());
			return false;
		}

		std::string input_dir      = io::dirname(input_file);
		std::string input_basename = input_file.substr(input_file.find_last_of("/\\") + 1);

		Parsed3do pf;
		if(!parse_3do(*file, {}, input_dir, opts, pf))
			return false;

		std::string format_label = "3DO v1.1";

		nlog::announce(input_basename, format_label, 
			std::format("model={}", pf.model_name));

		io::make_dirs(opts.out_dir);

		filters::Scope scope = filters::Scope::T3do;

		auto write_result = texture::write_all(opts.out_dir,
			std::span<const texture::Image>{pf.resolved_textures}, opts.raw);

		// Map mode-suffixed image.name -> written filename, then build a
		// per-pf.textures parallel filename array for build_materials.
		std::map<std::string, std::string> name_to_filename;
		for(size_t i = 0; i < pf.resolved_textures.size(); ++i) {

			if(!write_result.filenames[i].empty())
				name_to_filename[pf.resolved_textures[i].name] = write_result.filenames[i];

		}

		model::TextureOutput tex_out;
		tex_out.filenames.assign(pf.textures.size(), std::string{});
		for(size_t i = 0; i < pf.textures.size(); ++i) {

			if(pf.textures[i].name.empty())
				continue;

			if(i < pf.ex_textures.size() && pf.ex_textures[i])
				continue;

			std::string key = io::strip_ext(pf.textures[i].name)
				+ std::string(texture::mode_suffix(pf.specs[i].mode));

			if(auto it = name_to_filename.find(key); it != name_to_filename.end())
				tex_out.filenames[i] = it->second;

		}

		model::TextureExclusions tex_ex;
		tex_ex.drop_texture.assign(pf.ex_textures.begin(), pf.ex_textures.end());

		for(size_t lod_idx = 0; lod_idx < 1; ++lod_idx) {

			model::BuildContext ctx {
				opts,
				std::string_view{input_basename},
				std::string_view{input_dir},
				scope,
				tex_ex,
				std::span<const u8>{pf.ex_geometry.data(), pf.ex_geometry.size()},
				tex_out,
				std::span<const texture::TextureSpec>{pf.specs.data(), pf.specs.size()},
			};

			model::Model model;
			build_model(pf, lod_idx, ctx, model);

			std::string base = io::file_prefix(pf.model_name);

			if(opts.hardpoints || opts.debug) {

				if(build_hardpoints(pf, model) && !model.hardpoints.empty() && opts.hardpoints) {
					std::string hp_path = opts.out_dir + "\\" + base + "_hardpoints.txt";

					if(writer::extras::write_hardpoints(model, hp_path))
						nlog::info("Wrote %s\n", hp_path.c_str());
						
				}
			}

			writer::DebugOpts dbg;
			if(opts.debug) {
				dbg.normals    = true;
				dbg.hardpoints = true;
			}

			if(!writer::export_model(model, pf.model_name, opts, dbg))
				return false;

			writer::debug::ModelInfo mi = writer::debug::measure(model);

			nlog::info(" dest=%s\\\n", opts.out_dir.c_str());
			nlog::info(" verts=%zu, tris=%zu, mats=%zu, textures=%zu\n\n",
				mi.position_count, mi.face_count, model.materials.size(), pf.textures.size());
		}

		return true;
	}

}
