#include "pak.h"

#include "t3do.h"
#include "../model/model.h"
#include "../texture/write.h"
#include "../texture/palette.h"
#include "../texture/pcx.h"
#include "../util/io.h"
#include "../util/log.h"
#include "../util/types.h"
#include "../writer/debug.h"
#include "../writer/export.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace nova3di::format::pak {

	namespace {

		namespace nlog    = nova3di::util::log;
		namespace io      = nova3di::util::io;
		namespace texture = nova3di::texture;
		namespace palette = nova3di::texture::palette;
		namespace pcx     = nova3di::texture::pcx;
		namespace t3do    = nova3di::format::t3do;

		// Header
		constexpr u32 PAK_MAGIC               = 0x4B504433;
		constexpr u32 PAK_HEADER_SIZE         = 96;
		constexpr u32 PAK_OFF_MAGIC           = 0x00;
		constexpr u32 PAK_OFF_VERSION         = 0x04;
		constexpr u32 PAK_OFF_NAME            = 0x08;
		constexpr u32 PAK_NAME_LEN            = 32;
		constexpr u32 PAK_OFF_FLAGS           = 0x28;
		constexpr u32 PAK_OFF_LOD_COUNT       = 0x2C;
		constexpr u32 PAK_OFF_LOD_DIR         = 0x38;
		constexpr u32 PAK_OFF_TEX_DATA        = 0x3C;

		// LOD directory
		constexpr u32 LOD_DIR_OFF_DATA        = 0x04;

		// LOD0 data
		constexpr u32 LOD0_HEADER_SIZE        = 12;
		constexpr u32 LOD0_OFF_PART_COUNT     = 0x04;
		constexpr u32 LOD0_OFF_FIRST_PART_OFF = 0x08;
		constexpr u32 LOD0_PART_ENTRY_SIZE    = 44;

		// Textures
		constexpr u32 PAK_TEX_HEADER_SIZE     = 28;
		constexpr u32 PAK_TEX_OFF_NAME        = 0x00;
		constexpr u32 PAK_TEX_NAME_LEN        = 12;
		constexpr u32 PAK_TEX_OFF_FLAGS       = 0x0C;
		constexpr u32 PAK_TEX_OFF_WIDTH       = 0x10;
		constexpr u32 PAK_TEX_OFF_HEIGHT      = 0x14;
		constexpr u32 PAK_TEX_OFF_PIX_SIZE    = 0x18;
		constexpr u32 PAK_TEX_HAS_PALETTE_BIT = 0x0100;

		// Palette
		constexpr u32 PALETTE_SIZE            = 768;

		//============================================================================
		// Resolve a PAK palette. Order:
		//   1. pal_filename
		//   2. <pak_stem>.PAL / .pal (sibling fallback)
		//   3. GENPAK.PCX/PAL chain
		//============================================================================
		bool resolve_palette(
			const std::string& input_dir,
			std::string_view pal_filename,
			std::string_view pak_stem,
			u8 palette_out[768]
		) {

			if(!pal_filename.empty()) {
				if(palette::load_palette(input_dir + "/" + std::string(pal_filename), palette_out))
					return true;
			}

			if(palette::load_palette(input_dir + "/" + std::string(pak_stem) + ".PAL", palette_out))
				return true;

			for(const char* name : { "GENPAK.PCX", "GENPAK.PAL" }) {

				if(palette::load_palette(input_dir + "/" + name, palette_out))
					return true;

			}

			return false;
		}

		//============================================================================
		// Resolve PAK textures from the texture data block
		//============================================================================
		void resolve_textures(
			const ParsedPak& pf,
			const model::ConvertOptions& opts,
			std::vector<texture::Image>& out
		) {

			out.clear();

			const u8* data    = pf.bytes.data();
			size_t data_size  = pf.bytes.size();
			u32 off_tex_data  = pf.off_tex_data;

			if(off_tex_data == 0 || off_tex_data >= data_size)
				return;

			const u8* fallback_palette = pf.palette.data();
			bool have_fallback         = pf.has_palette;

			u32 pak_tex_count = io::read_u32(data + off_tex_data);
			size_t tpos = off_tex_data + sizeof(u32);

			for(u32 t = 0; t < pak_tex_count && tpos + PAK_TEX_HEADER_SIZE <= data_size; ++t) {

				char name_buf[PAK_TEX_NAME_LEN + 1] = {};
				memcpy(name_buf, data + tpos + PAK_TEX_OFF_NAME, PAK_TEX_NAME_LEN);

				u16 has_palette  = (u16)io::read_u32(data + tpos + PAK_TEX_OFF_FLAGS) & 0xFFFF;
				u32 tex_w        = io::read_u32(data + tpos + PAK_TEX_OFF_WIDTH);
				u32 tex_h        = io::read_u32(data + tpos + PAK_TEX_OFF_HEIGHT);
				u32 pix_size     = io::read_u32(data + tpos + PAK_TEX_OFF_PIX_SIZE);
				size_t pix_off   = tpos + PAK_TEX_HEADER_SIZE;
				size_t pal_off   = pix_off + pix_size;
				bool has_embedded_pal = (has_palette & PAK_TEX_HAS_PALETTE_BIT) != 0 && pal_off + PALETTE_SIZE <= data_size;

				texture::Image image;
				image.name = io::strip_ext(io::sanitize_name(name_buf, PAK_TEX_NAME_LEN));

				if(tex_w > 0 && tex_h > 0 && pix_size > 0 && pix_off + pix_size <= data_size) {

					const u8* pal_ptr;

					if(has_embedded_pal)
						pal_ptr = data + pal_off;
					else if(have_fallback)
						pal_ptr = fallback_palette;
					else
						pal_ptr = palette::grayscale.data();

					std::vector<u8> pcx_buf;
					pcx_buf.reserve(128 + pix_size + 769);

					pcx_buf.resize(128, 0);
					pcx_buf[0]  = 0x0A;
					pcx_buf[1]  = 5;
					pcx_buf[2]  = 1;
					pcx_buf[3]  = 8;

					u16 xmin = 0;
					u16 ymin = 0;
					u16 xmax = (u16)(tex_w - 1);
					u16 ymax = (u16)(tex_h - 1);
					std::memcpy(pcx_buf.data() + 4, &xmin, 2);
					std::memcpy(pcx_buf.data() + 6, &ymin, 2);
					std::memcpy(pcx_buf.data() + 8, &xmax, 2);
					std::memcpy(pcx_buf.data() + 10, &ymax, 2);

					pcx_buf[65] = 1;
					u16 bpl = (u16)((tex_w + 1) & ~1);
					std::memcpy(pcx_buf.data() + 66, &bpl, 2);

					pcx_buf.insert(pcx_buf.end(), data + pix_off, data + pix_off + pix_size);

					pcx_buf.push_back(0x0C);
					pcx_buf.insert(pcx_buf.end(), pal_ptr, pal_ptr + PALETTE_SIZE);

					if(opts.raw || opts.extract) {
						image.format    = texture::Format::Paletted;
						image.width     = (int)tex_w;
						image.height    = (int)tex_h;
						image.raw_ext   = "pcx";
						image.raw_bytes = std::move(pcx_buf);
					} else {
						std::vector<u8> linear_pixels;
						int decoded_w = 0, decoded_h = 0;
						u8 decoded_pal[PALETTE_SIZE] = {};
						bool is_paletted = false;

						if(pcx::decode(std::span<const u8>{pcx_buf.data(), pcx_buf.size()}, linear_pixels, decoded_pal, decoded_w, decoded_h, is_paletted) && is_paletted) {
							image.format = texture::Format::Paletted;
							image.width  = decoded_w;
							image.height = decoded_h;
							image.pixels = std::move(linear_pixels);
							std::memcpy(image.palette.data(), decoded_pal, PALETTE_SIZE);
						}
					}
				}

				out.push_back(std::move(image));
				tpos = has_embedded_pal ? (pal_off + PALETTE_SIZE) : (pix_off + pix_size);
			}
		}

		//============================================================================
		// Dump every embedded 3DO + every pool texture flat into opts.out_dir
		//============================================================================
		bool extract(const ParsedPak& pf, const model::ConvertOptions& opts) {

			io::make_dirs(opts.out_dir);

			size_t tex_written = 0;
			for(const auto& tex : pf.textures) {

				if(tex.raw_bytes.empty() || tex.name.empty())
					continue;

				std::string path = opts.out_dir + "\\" + tex.name + "." + tex.raw_ext;
				std::ofstream out(path, std::ios::binary);

				if(!out)
					continue;

				out.write(reinterpret_cast<const char*>(tex.raw_bytes.data()), tex.raw_bytes.size());
				++tex_written;
			}

			size_t parts_written = 0;
			for(const auto& part : pf.parts) {

				if(part.data.byte_size == 0 || part.data.model_name.empty())
					continue;

				if(part.byte_offset + part.data.byte_size > pf.bytes.size())
					continue;

				std::string path = opts.out_dir + "\\" + part.data.model_name + ".3DO";
				std::ofstream out(path, std::ios::binary);

				if(!out)
					continue;

				out.write(reinterpret_cast<const char*>(pf.bytes.data() + part.byte_offset), part.data.byte_size);
				++parts_written;
			}

			nlog::info(" dest=%s\\\n", opts.out_dir.c_str());
			nlog::info(" extracted %zu 3DOs, %zu textures\n\n", parts_written, tex_written);

			return true;
		}

		//============================================================================
		// Propagate sub-model world offsets.
		//  v2003: BFS tree walk over each parent's mesh_entries
		//  v2001: flat rule where each sub-part attaches at body.mesh_entries[i-1]
		//============================================================================
		void resolve_part_offsets(ParsedPak& pf) {

			if(pf.parts.size() <= 1)
				return;

			if(pf.version >= 0x2003) {

				std::vector<std::array<i32,3>> offsets(pf.parts.size(), {0,0,0});
				std::vector<size_t> queue = { 0 };

				size_t next_unassigned = 1;

				while(!queue.empty() && next_unassigned < pf.parts.size()) {

					size_t parent = queue.front();
					queue.erase(queue.begin());
					const auto& parent_mesh = pf.parts[parent].data.mesh_entries;

					for(size_t i = 0; i < parent_mesh.size() && next_unassigned < pf.parts.size(); ++i) {
						size_t child = next_unassigned++;
						offsets[child][0] = offsets[parent][0] + parent_mesh[i][0];
						offsets[child][1] = offsets[parent][1] + parent_mesh[i][1];
						offsets[child][2] = offsets[parent][2] + parent_mesh[i][2];
						queue.push_back(child);
					}
				}

				for(size_t i = 0; i < pf.parts.size(); ++i)
					pf.parts[i].offset = offsets[i];

			} else {

				const auto& body_mesh = pf.parts[0].data.mesh_entries;

				for(size_t i = 1; i < pf.parts.size(); ++i) {

					if(pf.parts[i].data.vertices.size() < 4)
						continue;

					size_t mesh_entry_idx = i - 1;

					if(mesh_entry_idx >= body_mesh.size())
						continue;

					pf.parts[i].offset = body_mesh[mesh_entry_idx];
				}

			}

		}

	}  // namespace

	//============================================================================
	// Parse PAK 
	//============================================================================
	bool parse_pak(
		std::vector<u8>&&             bytes,
		const std::string&            input_dir,
		std::string_view              pal_filename,
		std::string_view              pak_filename,
		const model::ConvertOptions&  opts,
		ParsedPak&                    pf
	) {

		pf.bytes = std::move(bytes);
		const u8* data = pf.bytes.data();
		size_t file_size = pf.bytes.size();

		if(file_size < PAK_HEADER_SIZE) {
			nlog::error("PAK file too small");
			return false;
		}

		if(io::read_u32(data + PAK_OFF_MAGIC) != PAK_MAGIC) {
			nlog::error("bad PAK magic");
			return false;
		}

		char raw_name[PAK_NAME_LEN + 1] = {};
		memcpy(raw_name, data + PAK_OFF_NAME, PAK_NAME_LEN);

		for(int i = (int)PAK_NAME_LEN - 1; i >= 0 && (raw_name[i] == ' ' || raw_name[i] == 0); --i)
			raw_name[i] = 0;

		pf.pak_name = io::sanitize_name(raw_name, PAK_NAME_LEN);

		if(pf.pak_name.empty())
			pf.pak_name = "model";

		pf.lod_count    = io::read_u32(data + PAK_OFF_LOD_COUNT);
		pf.off_tex_data = io::read_u32(data + PAK_OFF_TEX_DATA);
		pf.version      = io::read_u16(data + PAK_OFF_VERSION);

		u32 off_lod_dir = io::read_u32(data + PAK_OFF_LOD_DIR);

		// Resolve palette + decode pool BEFORE parsing 3DO blocks so each
		// t3do::parse_3do call can resolve its textures from the same source 
		pf.has_palette = resolve_palette(input_dir, pal_filename, io::stem(std::string(pak_filename)), pf.palette.data());
		resolve_textures(pf, opts, pf.textures);

		auto add_part = [&](size_t model_off) -> bool {

			Part part;
			part.byte_offset = model_off;
			std::span<const u8> body {
				data + model_off,
				file_size - model_off
			};

			if(!t3do::parse_3do(body, std::span<const texture::Image>{pf.textures}, "", opts, part.data))
				return false;

			if(part.data.vertices.empty() || part.data.triangles.empty())
				return false;

			part.name = part.data.model_name;
			pf.parts.push_back(std::move(part));
			return true;
		};

		if(off_lod_dir > 0 && off_lod_dir + LOD_DIR_OFF_DATA + 4 <= (u32)file_size) {
			u32 lod0_data_off = io::read_u32(data + off_lod_dir + LOD_DIR_OFF_DATA);

			if(lod0_data_off + LOD0_HEADER_SIZE <= (u32)file_size) {

				u32 part_count = io::read_u32(data + lod0_data_off + LOD0_OFF_PART_COUNT);
				u32 first_off  = io::read_u32(data + lod0_data_off + LOD0_OFF_FIRST_PART_OFF);

				for(u32 i = 0; i < part_count; ++i) {
					size_t entry_off = lod0_data_off + first_off + i * LOD0_PART_ENTRY_SIZE;

					if(entry_off + LOD0_PART_ENTRY_SIZE > file_size)
						break;

					add_part(io::read_u32(data + entry_off));
				}
			}
		}

		resolve_part_offsets(pf);

		if(!pf.parts.empty())
			pf.model_name = pf.parts[0].data.model_name;

		if(pf.model_name.empty())
			pf.model_name = pf.pak_name;

		return true;
	}

	namespace {

		struct TexEntry {
			std::string filename;
			bool        has_alpha = false;
			float       opacity   = 1.0f;
		};

		//============================================================================
		// Aggregate parsed.resolved_textures across all parts
		//============================================================================
		std::map<std::string, TexEntry> get_pak_textures(
			const ParsedPak& pf,
			const model::ConvertOptions& opts
		) {

			std::map<std::string, TexEntry> tex_filename;

			std::vector<texture::Image> textures;
			std::vector<std::string>    texture_keys;

			for(const auto& part : pf.parts) {
				for(const auto& img : part.data.resolved_textures) {
					textures.push_back(img);
					texture_keys.push_back(img.name);
				}
			}

			auto write_result = texture::write_all(
				opts.out_dir, std::span<const texture::Image>{textures}, opts.raw);

			for(size_t i = 0; i < textures.size(); ++i) {
				if(write_result.filenames[i].empty())
					continue;

				bool has_alpha = textures[i].spec.mode != texture::AlphaMode::Opaque;
				tex_filename[texture_keys[i]] = TexEntry{
					write_result.filenames[i],
					has_alpha,
					textures[i].spec.opacity
				};
			}

			return tex_filename;
		}

		//============================================================================
		// Flat-color material name. Opacity is part of the dedup key so two
		// same-rgb unnamed textures with different opacities (PAK canopy_tint)
		// get different materials
		//============================================================================
		std::string flat_mat_name(const t3do::Texture& tex, float opacity) {

			if(opacity != 1.0f)
				return std::format("flat_{}_{}_{}_op{}", tex.r, tex.g, tex.b, (int)(opacity * 100));

			return std::format("flat_{}_{}_{}", tex.r, tex.g, tex.b);
		}

		//============================================================================
		// Material name from a (3DO entry, spec) pair
		//============================================================================
		std::string mat_name(const t3do::Texture& tex, const texture::TextureSpec& spec) {

			if(tex.name.empty())
				return flat_mat_name(tex, spec.opacity);

			std::string name = io::strip_ext(tex.name);
			name += std::string(texture::mode_suffix(spec.mode));

			if(spec.opacity != 1.0f)
				name += "_op" + std::to_string((int)(spec.opacity * 100));

			return name;
		}

		//============================================================================
		// Build a single Mesh for one Part at its propagated offset
		//============================================================================
		model::Mesh build_part_mesh(
			const Part& part,
			const std::map<std::string, int>& mat_index,
			int default_mat_idx
		) {

			model::Mesh mesh;
			mesh.group_name = part.name;

			i32 off_x = part.offset[0];
			i32 off_y = part.offset[1];
			i32 off_z = part.offset[2];

			mesh.positions.reserve(part.data.vertices.size());
			mesh.uvs.reserve(part.data.vertices.size());
			mesh.normals.reserve(part.data.normals.size());

			for(const auto& vert : part.data.vertices) {
				mesh.positions.push_back({
					(double)-(vert.x + off_x),
					(double)(vert.z + off_z),
					(double)-(vert.y + off_y)
				});
				mesh.uvs.push_back({(double)vert.u, 1.0 - (double)vert.v});
			}

			for(const auto& norm : part.data.normals)
				mesh.normals.push_back({(double)-norm.nx, (double)norm.nz, (double)-norm.ny});

			for(size_t tri_idx = 0; tri_idx < part.data.triangles.size(); ++tri_idx) {

				const auto& tri = part.data.triangles[tri_idx];

				if(tri_idx < part.data.ex_geometry.size() && part.data.ex_geometry[tri_idx])
					continue;

				u32 face_tex_idx = (tri.tex_idx < part.data.textures.size())
					? tri.tex_idx
					: (u32)part.data.textures.size();

				int mat_idx = -1;

				if(face_tex_idx < part.data.textures.size()) {

					const t3do::Texture& tex = part.data.textures[face_tex_idx];
					const texture::TextureSpec& spec = (face_tex_idx < part.data.specs.size())
						? part.data.specs[face_tex_idx]
						: texture::TextureSpec{};
					std::string mat = mat_name(tex, spec);

					auto mat_it = mat_index.find(io::to_lower(mat));

					if(mat_it != mat_index.end())
						mat_idx = mat_it->second;
				}

				if(mat_idx < 0) {
					if(default_mat_idx >= 0)
						mat_idx = default_mat_idx;
					else
						continue;
				}

				model::Face face = {};
				face.v[0] = {(i32)tri.vi[0], (i32)tri.ni[0], (i32)tri.vi[0]};
				face.v[1] = {(i32)tri.vi[2], (i32)tri.ni[2], (i32)tri.vi[2]};
				face.v[2] = {(i32)tri.vi[1], (i32)tri.ni[1], (i32)tri.vi[1]};
				face.material = (u32)mat_idx;
				mesh.faces.push_back(face);
			}

			return mesh;
		}

		//============================================================================
		// Cross part deduped materials + one mesh per part
		//============================================================================
		void build_model(
			const ParsedPak& pf,
			const std::map<std::string, TexEntry>& tex_filename,
			const std::string& source_label,
			model::Model& model
		) {

			model.source_name = source_label;
			model.format_tag  = (pf.version >= 0x2003) ? "pak_v2003" : "pak_v2001";

			std::map<std::string, int> mat_index;

			auto add_material = [&](size_t part_idx, size_t tex_idx) -> int {

				const auto& tex  = pf.parts[part_idx].data.textures[tex_idx];
				const auto& spec = pf.parts[part_idx].data.specs[tex_idx];

				bool is_flat     = tex.name.empty();
				std::string name = mat_name(tex, spec);
				std::string key  = io::to_lower(name);

				auto it = mat_index.find(key);

				if(it != mat_index.end())
					return it->second;

				int idx = (int)model.materials.size();

				model::Material mat;
				mat.name    = name;
				mat.opacity = spec.opacity;

				if(is_flat) {

					mat.color[0] = tex.r / 255.0f;
					mat.color[1] = tex.g / 255.0f;
					mat.color[2] = tex.b / 255.0f;

				} else {
					mat.color[0] = mat.color[1] = mat.color[2] = 0.8f;

					std::string tex_key = io::strip_ext(tex.name) + std::string(texture::mode_suffix(spec.mode));
					auto filename_it = tex_filename.find(tex_key);

					if(filename_it != tex_filename.end() && !filename_it->second.filename.empty()) {
						mat.texture = filename_it->second.filename;

						if(filename_it->second.has_alpha && io::extension(filename_it->second.filename) == ".tga")
							mat.alpha = filename_it->second.filename;

					}
				}

				model.materials.push_back(mat);
				mat_index[key] = idx;

				return idx;
			};

			for(size_t part_idx = 0; part_idx < pf.parts.size(); ++part_idx) {
				const auto& part = pf.parts[part_idx];

				for(size_t tex_idx = 0; tex_idx < part.data.textures.size(); ++tex_idx) {

					if(tex_idx < part.data.ex_textures.size() && part.data.ex_textures[tex_idx])
						continue;

					add_material(part_idx, tex_idx);
				}
			}

			int default_mat_idx = -1;
			bool need_default = std::ranges::any_of(pf.parts, [&](const auto& part) {

				return std::ranges::any_of(part.data.triangles, [&](const auto& tri) {
					return tri.tex_idx >= part.data.textures.size();
				});

			});

			if(need_default) {
				model::Material default_mat;

				default_mat.name = "default";
				default_mat.color[0] = default_mat.color[1] = default_mat.color[2] = 0.8f;
				default_mat_idx = (int)model.materials.size();

				model.materials.push_back(default_mat);
			}

			for(size_t i = 0; i < pf.parts.size(); ++i) {

				model.meshes.push_back(build_part_mesh(
					pf.parts[i],
					mat_index,
					default_mat_idx
				));

			}

		}

		//============================================================================
		// PartFrame at each part's origin. OBJ-space origin matches the
		// (-x, z, -y) transform applied in build_part_mesh
		//============================================================================
		std::vector<writer::debug::PartFrame> build_part_frames(const ParsedPak& pf) {

			std::vector<writer::debug::PartFrame> frames;
			frames.reserve(pf.parts.size());

			for(size_t i = 0; i < pf.parts.size(); ++i) {
				const auto& part = pf.parts[i];

				writer::debug::PartFrame f;
				f.name = !part.data.model_name.empty()
					? part.data.model_name
					: std::format("part_{}", i);

				f.origin[0] = -(double)part.offset[0];
				f.origin[1] =  (double)part.offset[2];
				f.origin[2] = -(double)part.offset[1];

				f.basis[0][0] = 1.0; f.basis[0][1] = 0.0; f.basis[0][2] = 0.0;
				f.basis[1][0] = 0.0; f.basis[1][1] = 1.0; f.basis[1][2] = 0.0;
				f.basis[2][0] = 0.0; f.basis[2][1] = 0.0; f.basis[2][2] = 1.0;

				frames.push_back(std::move(f));
			}

			return frames;
		}

	}  // namespace

	//============================================================================
	// Converter
	//============================================================================
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

		ParsedPak pf;
		if(!parse_pak(std::move(*file), input_dir, "", input_basename, opts, pf))
			return false;

		std::string format_label = std::format("PAK v0x{:04X}", pf.version);

		nlog::announce(input_basename, format_label,
			std::format("model={}, lods={}", pf.pak_name, pf.lod_count));

		if(opts.extract)
			return extract(pf, opts);

		io::make_dirs(opts.out_dir);

		auto tex_filename = get_pak_textures(pf, opts);

		std::string source_label = std::format("{} (PAK v0x{:04X})", input_basename, pf.version);

		model::Model model;
		build_model(pf, tex_filename, source_label, model);

		writer::DebugOpts dbg;
		if(opts.debug)
			dbg.triads = build_part_frames(pf);

		if(!writer::export_model(model, pf.model_name, opts, dbg))
			return false;

		writer::debug::ModelInfo mi = writer::debug::measure(model);

		size_t pak_tex_total = 0;
		for(const auto& part : pf.parts)
			pak_tex_total += part.data.textures.size();

		nlog::info(" dest=%s\\\n", opts.out_dir.c_str());
		nlog::info(" verts=%zu, tris=%zu, mats=%zu, textures=%zu\n\n",
			mi.position_count, mi.face_count, model.materials.size(), pak_tex_total);

		return true;
	}

}
