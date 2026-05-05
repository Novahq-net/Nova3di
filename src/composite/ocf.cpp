#include "ocf.h"

#include "../format/pak.h"
#include "../format/t3do.h"
#include "../model/model.h"
#include "../texture/write.h"
#include "../util/cbin.h"
#include "../util/io.h"
#include "../util/log.h"
#include "../util/types.h"
#include "../writer/debug.h"
#include "../writer/export.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace nova3di::composite::ocf {

	namespace {

		namespace nlog    = nova3di::util::log;
		namespace io      = nova3di::util::io;
		namespace cbin    = nova3di::util::cbin;
		namespace texture = nova3di::texture;
		namespace t3do    = nova3di::format::t3do;
		namespace pak     = nova3di::format::pak;

		//============================================================================
		// 3x3 rotation matrix
		//============================================================================
		struct Rotation3 {
			double m[3][3] = {{1,0,0},{0,1,0},{0,0,1}};

			// ANGLES[0] = Y, ANGLES[1] = X (LH), ANGLES[2] = Z.
			static Rotation3 from_angles(double angle_y, double angle_x, double angle_z) {

				const double DEG_TO_RAD = 3.14159265358979323846 / 180.0;

				double cos_y = std::cos(angle_y * DEG_TO_RAD), sin_y = std::sin(angle_y * DEG_TO_RAD);
				double cos_x = std::cos(angle_x * DEG_TO_RAD), sin_x = std::sin(angle_x * DEG_TO_RAD);
				double cos_z = std::cos(angle_z * DEG_TO_RAD), sin_z = std::sin(angle_z * DEG_TO_RAD);

				Rotation3 mat;

				mat.m[0][0] =  cos_y*cos_z - sin_y*sin_x*sin_z;
				mat.m[0][1] = -cos_y*sin_z - sin_y*sin_x*cos_z;
				mat.m[0][2] =  sin_y*cos_x;
				mat.m[1][0] =  cos_x*sin_z;
				mat.m[1][1] =  cos_x*cos_z;
				mat.m[1][2] =  sin_x;
				mat.m[2][0] = -sin_y*cos_z - cos_y*sin_x*sin_z;
				mat.m[2][1] =  sin_y*sin_z - cos_y*sin_x*cos_z;
				mat.m[2][2] =  cos_y*cos_x;

				return mat;
			}

			void rotate_point(double& x, double& y, double& z) const {
				double out_x = m[0][0]*x + m[0][1]*y + m[0][2]*z;
				double out_y = m[1][0]*x + m[1][1]*y + m[1][2]*z;
				double out_z = m[2][0]*x + m[2][1]*y + m[2][2]*z;

				x = out_x;
				y = out_y;
				z = out_z;
			}
		};

		//============================================================================
		// CBIN-parsed structs
		//============================================================================
		struct Pak {
			std::string    name;
			std::string    pak_file;
			std::string    pal_file;
			pak::ParsedPak parsed;
		};

		struct Part {
			std::string name;
			std::string pak_name;
			std::string parent_name; // (empty = root)
			int parent_attach_idx = 0;
			int child_attach_idx = 0;
			float scale = 1.0f;
			float angles[3] = {};
			int   type = 0;
			Rotation3 world_rot;
			double wx = 0;
			double wy = 0;
			double wz = 0;
		};

		struct ParsedOcf {
			std::span<const u8>           bytes;
			std::string                   model_name;
			int                           pak_count  = 0;
			int                           part_count = 0;
			std::vector<Pak>              paks;        // [paks] section
			std::vector<Part>             parts;       // [part_NN] sections
			std::map<std::string, size_t> pak_lookup;  // name (lowercased) -> index in paks
			std::map<std::string, size_t> part_lookup; // name -> index in parts
		};

		//============================================================================
		// Lookup a Pak by its OCF name
		//============================================================================
		const Pak* resolve_pak(const ParsedOcf& pf, std::string_view name) {
			auto pak = pf.pak_lookup.find(io::to_lower(std::string(name)));
			return (pak != pf.pak_lookup.end()) ? &pf.paks[pak->second] : nullptr;
		}

		//============================================================================
		// Lookup a Part by its section name
		//============================================================================
		const Part* resolve_part(const ParsedOcf& pf, std::string_view name) {
			auto part = pf.part_lookup.find(io::to_lower(std::string(name)));
			return (part != pf.part_lookup.end()) ? &pf.parts[part->second] : nullptr;
		}

		//============================================================================
		// Populate the lookup maps in ParsedOcf for O(1) access by name
		//============================================================================
		void resolve_indices(ParsedOcf& pf) {
			pf.pak_lookup.clear();
			pf.part_lookup.clear();

			for (size_t i = 0; i < pf.paks.size(); ++i)
				pf.pak_lookup[io::to_lower(pf.paks[i].name)] = i;

			for (size_t i = 0; i < pf.parts.size(); ++i)
				pf.part_lookup[io::to_lower(pf.parts[i].name)] = i;
		}

		//============================================================================
		// Parse OCF from CBIN data
		//============================================================================
		bool parse_ocf(std::span<const u8> bytes, ParsedOcf& pf) {

			pf.bytes = bytes;
			cbin::File cfg;

			if(!cbin::parse(bytes, cfg)) {
				nlog::error("failed to parse CBIN data");
				return false;
			}

			const cbin::Section* hdr = cfg.find("header");
			if(!hdr) {
				nlog::error("OCF missing [header] section");
				return false;
			}

			pf.model_name   = hdr->get_str("NAME");
			pf.pak_count  = hdr->get_int("NUMPAKS");
			pf.part_count = hdr->get_int("NUMPARTS");

			const cbin::Section* paks_sec = cfg.find("paks");
			if(!paks_sec) {
				nlog::error("OCF missing [paks] section");
				return false;
			}

			for(const auto& entry : paks_sec->entries) {

				if(!io::iequals(entry.key, "PAK") || entry.values.size() < 3)
					continue;

				Pak p;
				p.name     = entry.values[0].str;
				p.pak_file = entry.values[1].str;
				p.pal_file = entry.values[2].str;
				pf.paks.push_back(std::move(p));
			}

			for(int part_i = 0; part_i < 10000; ++part_i) {
				std::string sec_name = std::format("part_{:02}", part_i);
				const cbin::Section* section = cfg.find(sec_name);

				if(!section)
					break;

				Part p;
				p.name     = section->get_str("NAME");
				p.pak_name = io::to_lower(section->get_str("PAK"));
				p.scale    = section->get_float("SCALE");

				if(p.scale == 0.0f)
					p.scale = 1.0f;

				const cbin::Entry* angles_e = section->find("ANGLES");

				if(angles_e && angles_e->values.size() >= 3) {
					p.angles[0] = angles_e->values[0].as_float();
					p.angles[1] = angles_e->values[1].as_float();
					p.angles[2] = angles_e->values[2].as_float();
				}

				p.type = section->get_int("TYPE");

				const cbin::Entry* attach_e = section->find("ATTACH");

				if(attach_e && attach_e->values.size() >= 3) {
					p.parent_name       = attach_e->values[0].str;
					p.parent_attach_idx = attach_e->values[1].as_int();
					p.child_attach_idx  = attach_e->values[2].as_int();
				}

				pf.parts.push_back(p);
			}

			return true;
		}

		//============================================================================
		// Load each PAK file referenced by the OCF
		//============================================================================
		void load_paks(const std::string& input_dir, const model::ConvertOptions& opts, ParsedOcf& pf) {

			for(auto& p : pf.paks) {

				auto buf = io::read_file(input_dir + "/" + p.pak_file);

				if(!buf) {
					nlog::warn("cannot open PAK '%s'", p.pak_file.c_str());
					continue;
				}

				if(!pak::parse_pak(std::move(*buf), input_dir, p.pal_file, p.pak_file, opts, p.parsed))
					nlog::warn("'%s' is not a valid PAK", p.pak_file.c_str());
			}
		}

		//============================================================================
		// Walk the part tree and set each part's world rotation + translation
		//============================================================================
		void resolve_part_transforms(ParsedOcf& pf) {

			for(size_t i = 0; i < pf.parts.size(); ++i) {
				Part& part = pf.parts[i];
				Rotation3 local_rot = Rotation3::from_angles(part.angles[0], part.angles[1], part.angles[2]);

				if(part.parent_name.empty()) {
					part.world_rot = local_rot;
					continue;
				}

				const Part* parent = resolve_part(pf, part.parent_name);

				if(!parent) {
					nlog::warn("part '%s' parent '%s' not found", part.name.c_str(), part.parent_name.c_str());
					part.world_rot = local_rot;
					continue;
				}

				const Pak* parent_pak = resolve_pak(pf, parent->pak_name);
				const Pak* child_pak  = resolve_pak(pf, part.pak_name);

				double parent_x = 0, parent_y = 0, parent_z = 0;

				if(parent_pak && !parent_pak->parsed.parts.empty()) {
					const auto& parent_attach = parent_pak->parsed.parts[0].data.attach_points;

					if(part.parent_attach_idx >= 0 && part.parent_attach_idx < (int)parent_attach.size()) {
						parent_x = parent_attach[part.parent_attach_idx][0] * (double)parent->scale;
						parent_y = parent_attach[part.parent_attach_idx][1] * (double)parent->scale;
						parent_z = parent_attach[part.parent_attach_idx][2] * (double)parent->scale;
					}
				}

				Rotation3 parent_local = Rotation3::from_angles(parent->angles[0], parent->angles[1], parent->angles[2]);
				parent_local.rotate_point(parent_x, parent_y, parent_z);

				double child_x = 0, child_y = 0, child_z = 0;

				if(child_pak && !child_pak->parsed.parts.empty() && part.child_attach_idx >= 0) {

					const auto& child_attach = child_pak->parsed.parts[0].data.attach_points;

					if(part.child_attach_idx < (int)child_attach.size()) {
						child_x = child_attach[part.child_attach_idx][0] * (double)part.scale;
						child_y = child_attach[part.child_attach_idx][1] * (double)part.scale;
						child_z = child_attach[part.child_attach_idx][2] * (double)part.scale;
					}
				}

				local_rot.rotate_point(child_x, child_y, child_z);

				part.world_rot = local_rot;
				part.wx = parent->wx + parent_x - child_x;
				part.wy = parent->wy + parent_y - child_y;
				part.wz = parent->wz + parent_z - child_z;
			}
		}

		struct TexEntry {
			std::string filename;
			bool        has_alpha = false;
			float       opacity   = 1.0f;
		};

		//============================================================================
		// Aggregate parsed.resolved_textures across all parts of all PAKs in alpha order
		//============================================================================
		std::map<std::string, TexEntry> get_ocf_textures(
			const ParsedOcf& pf,
			const model::ConvertOptions& opts
		) {

			std::map<std::string, TexEntry> tex_filename;

			std::vector<texture::Image> textures;
			std::vector<std::string>    texture_keys;

			for(const auto& [key, pak_idx] : pf.pak_lookup) {
				const auto& p = pf.paks[pak_idx];
				for(const auto& sub : p.parsed.parts) {
					for(const auto& img : sub.data.resolved_textures) {
						textures.push_back(img);
						texture_keys.push_back(img.name);
					}
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
		// Flat color material name
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
		// Build one Mesh for one (Part, pak::Part) pair
		//============================================================================
		model::Mesh build_part_mesh(
			const Part& part,
			const pak::Part& sub,
			size_t sub_idx,
			const std::map<std::string, int>& mat_index,
			int default_mat_idx
		) {

			auto round2 = [](double v) {
				return std::stod(std::format("{:.2f}", v));
			};

			model::Mesh mesh;

			std::string sub_label = sub.data.model_name.empty()
				? std::format("sub{}", sub_idx)
				: sub.data.model_name;

			mesh.group_name = (sub_idx == 0)
				? part.name
				: part.name + "_" + sub_label;

			mesh.positions.reserve(sub.data.vertices.size());
			mesh.uvs.reserve(sub.data.vertices.size());
			mesh.normals.reserve(sub.data.normals.size());

			for(const auto& src_vert : sub.data.vertices) {
				double offset_x = (double)(src_vert.x + sub.offset[0]);
				double offset_y = (double)(src_vert.y + sub.offset[1]);
				double offset_z = (double)(src_vert.z + sub.offset[2]);
				double x =  offset_x * (double)part.scale;
				double y =  offset_z * (double)part.scale;
				double z = -offset_y * (double)part.scale;
				part.world_rot.rotate_point(x, y, z);
				mesh.positions.push_back({round2(-(x + part.wx)), round2(y + part.wy), round2(z + part.wz)});
				mesh.uvs.push_back({(double)src_vert.u, 1.0 - (double)src_vert.v});
			}

			for(const auto& src_norm : sub.data.normals) {
				double nx = (double)src_norm.nx;
				double ny =  (double)src_norm.nz;
				double nz = -(double)src_norm.ny;
				part.world_rot.rotate_point(nx, ny, nz);
				mesh.normals.push_back({-nx, ny, nz});
			}

			for(size_t tri_idx = 0; tri_idx < sub.data.triangles.size(); ++tri_idx) {

				const auto& tri = sub.data.triangles[tri_idx];

				if(tri_idx < sub.data.ex_geometry.size() && sub.data.ex_geometry[tri_idx])
					continue;

				u32 face_tex_idx = (tri.tex_idx < sub.data.textures.size())
					? tri.tex_idx
					: (u32)sub.data.textures.size();

				int mat_idx = -1;

				if(face_tex_idx < sub.data.textures.size()) {

					const t3do::Texture& tex = sub.data.textures[face_tex_idx];
					const texture::TextureSpec& spec = (face_tex_idx < sub.data.specs.size())
						? sub.data.specs[face_tex_idx]
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
		// Cross-PAK deduped materials + per-(Part, pak::Part) meshes
		//============================================================================
		void build_model(
			const ParsedOcf& pf,
			const std::map<std::string, TexEntry>& tex_filename,
			std::string_view source_name,
			model::Model& model
		) {

			model.source_name = std::format("{} (OCF, {} parts)", source_name, pf.parts.size());
			model.format_tag  = "ocf";

			std::map<std::string, int> mat_index;

			auto add_material = [&](const std::string& pak_key, size_t sub_idx, size_t tex_idx) -> int {

				const Pak* pak_ptr = resolve_pak(pf, pak_key);
				if(!pak_ptr) return -1;

				const auto& tex  = pak_ptr->parsed.parts[sub_idx].data.textures[tex_idx];
				const auto& spec = pak_ptr->parsed.parts[sub_idx].data.specs[tex_idx];

				bool        is_flat = tex.name.empty();
				std::string name    = mat_name(tex, spec);
				std::string key     = io::to_lower(name);

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

			for(const auto& [key, pak_idx] : pf.pak_lookup) {
				const auto& p = pf.paks[pak_idx];

				for(size_t sub_idx = 0; sub_idx < p.parsed.parts.size(); ++sub_idx) {
					const auto& sub = p.parsed.parts[sub_idx];

					for(size_t tex_idx = 0; tex_idx < sub.data.textures.size(); ++tex_idx) {

						if(tex_idx < sub.data.ex_textures.size() && sub.data.ex_textures[tex_idx])
							continue;

						add_material(key, sub_idx, tex_idx);
					}
				}
			}

			int default_mat_idx = -1;
			bool need_default = false;

			for(const auto& part : pf.parts) {

				const Pak* pak_ptr = resolve_pak(pf, part.pak_name);

				if(!pak_ptr)
					continue;

				for(const auto& sub : pak_ptr->parsed.parts) {

					for(const auto& tri : sub.data.triangles) {

						if(tri.tex_idx >= sub.data.textures.size()) {
							need_default = true;
							break;
						}
					}

					if(need_default)
						break;
				}

				if(need_default)
					break;
			}

			if(need_default || model.materials.empty()) {
				model::Material default_mat;
				default_mat.name = "default";
				default_mat.color[0] = default_mat.color[1] = default_mat.color[2] = 0.5f;
				default_mat_idx = (int)model.materials.size();
				model.materials.push_back(default_mat);
			}

			for(const auto& part : pf.parts) {

				const Pak* pak_ptr = resolve_pak(pf, part.pak_name);

				if(!pak_ptr || pak_ptr->parsed.parts.empty())
					continue;

				for(size_t sub_idx = 0; sub_idx < pak_ptr->parsed.parts.size(); ++sub_idx) {

					model.meshes.push_back(build_part_mesh(
						part,
						pak_ptr->parsed.parts[sub_idx],
						sub_idx,
						mat_index,
						default_mat_idx
					));
				}
			}
		}

		//============================================================================
		// PartFrame at each part's world-space origin + rotation
		//============================================================================
		std::vector<writer::debug::PartFrame> build_part_frames(std::span<const Part> parts) {

			std::vector<writer::debug::PartFrame> frames;
			frames.reserve(parts.size());

			for(const auto& part : parts) {
				writer::debug::PartFrame f;

				f.name = part.name;

				f.origin[0] = -part.wx;
				f.origin[1] =  part.wy;
				f.origin[2] =  part.wz;

				for(int i = 0; i < 3; ++i) {
					f.basis[i][0] = -part.world_rot.m[0][i];
					f.basis[i][1] =  part.world_rot.m[1][i];
					f.basis[i][2] =  part.world_rot.m[2][i];
				}

				frames.push_back(std::move(f));
			}

			return frames;
		}

	}  // namespace

	//============================================================================
	// Converter
	//============================================================================
	bool Converter::convert(const std::string& input_file, const model::ConvertOptions& opts) {

		auto file = io::read_file(input_file);

		if(!file) {
			nlog::error("cannot open '%s'", input_file.c_str());
			return false;
		}

		std::string input_dir      = io::dirname(input_file);
		std::string input_basename = input_file.substr(input_file.find_last_of("/\\") + 1);

		ParsedOcf pf;
		if(!parse_ocf(*file, pf))
			return false;

		nlog::announce(input_basename, "OCF",
			std::format("model={}, paks={}, parts={}", pf.model_name, pf.pak_count, pf.part_count));

		if(opts.extract) {

			int success = 0, fail = 0;
			format::pak::Converter pak_converter;

			for(const auto& p : pf.paks) {

				std::string pak_path = input_dir + "/" + p.pak_file;

				model::ConvertOptions sub_opts = opts;
				sub_opts.out_dir = opts.out_dir + "\\" + io::strip_ext(p.pak_file);

				if(pak_converter.convert(pak_path, sub_opts))
					++success;
				else
					++fail;
			}

			return fail == 0 || success > 0;
		}

		load_paks(input_dir, opts, pf);
		resolve_indices(pf);
		resolve_part_transforms(pf);

		io::make_dirs(opts.out_dir);

		auto tex_filename = get_ocf_textures(pf, opts);

		model::Model model;
		build_model(pf, tex_filename, input_basename, model);

		writer::DebugOpts dbg;
		if(opts.debug)
			dbg.triads = build_part_frames(pf.parts);

		if(!writer::export_model(model, pf.model_name, opts, dbg))
			return false;

		writer::debug::ModelInfo mi = writer::debug::measure(model);

		nlog::info(" dest=%s\\\n", opts.out_dir.c_str());
		nlog::info(" verts=%zu, tris=%zu, mats=%zu, textures=%zu\n\n",
			mi.position_count, mi.face_count, model.materials.size(), tex_filename.size());

		return true;
	}

}
