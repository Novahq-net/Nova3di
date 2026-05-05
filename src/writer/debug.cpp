#include "debug.h"

#include <cmath>
#include <format>
#include <fstream>

namespace nova3di::writer::debug {

	namespace {

		constexpr const char* HEADER = "Converted by NovaHQ Nova3di <https://novahq.net>";

		// Arm lengths as a fraction of model radius
		constexpr double TRIAD_ARM_FRAC     = 0.04;
		constexpr double HARDPOINT_ARM_FRAC = 0.02;
		constexpr double NORMAL_ARM_FRAC    = 0.04;

		// Arm thickness as a fraction of arm length.
		constexpr double TRIAD_THICKNESS_FRAC     = 0.02;
		constexpr double HARDPOINT_THICKNESS_FRAC = 0.02;
		constexpr double NORMAL_THICKNESS_FRAC    = 0.04;

		struct Rgb {
			double r;
			double g;
			double b;
		};

		constexpr Rgb COLOR_X         { 1.0, 0.0, 0.0 };
		constexpr Rgb COLOR_Y         { 0.0, 1.0, 0.0 };
		constexpr Rgb COLOR_Z         { 0.0, 0.4, 1.0 };
		constexpr Rgb COLOR_NORMAL    { 0.5, 1.0, 0.0 };
		constexpr Rgb COLOR_COLLISION { 1.0, 0.0, 1.0 };

		//========================================================================
		// Extended OBJ `v x y z r g b` form
		//========================================================================
		void append_colored_vertex(
			std::ofstream& obj, 
			double x, 
			double y, 
			double z, 
			const Rgb& c
		) {

			obj << std::format("v {:.6f} {:.6f} {:.6f} {:.3f} {:.3f} {:.3f}\n", x, y, z, c.r, c.g, c.b);
		}

		//========================================================================
		// Append a single material definition (illum 1)
		//========================================================================
		void append_material_def(
			std::ofstream& mtl, 
			const char* name, 
			double r, 
			double g, 
			double b
		) {

			mtl << std::format("newmtl {}\n", name);
			mtl << std::format("Ka {:.3f} {:.3f} {:.3f}\n", r, g, b);
			mtl << std::format("Kd {:.3f} {:.3f} {:.3f}\n", r, g, b);
			mtl << "Ks 0.000 0.000 0.000\n"
			       "d 1.000\n"
			       "illum 1\n";
		}

		//========================================================================
		// Append the +X / +Y / +Z axis triad at (ox,oy,oz) using basis[3][3] arms
		//========================================================================
		void build_triad(
			std::ofstream& obj,
			const std::string& part_name,
			double ox, 
			double oy, 
			double oz,
			const double basis[3][3], double arm,
			size_t& v_counter,
			size_t vt_idx, size_t vn_idx
		) {
			const double thickness = arm * TRIAD_THICKNESS_FRAC;

			struct Axis { const char* suffix; const char* mtl; Rgb color; };
			static const Axis axes[3] = {
				{ "x", "debug_x_red",   COLOR_X },
				{ "y", "debug_y_green", COLOR_Y },
				{ "z", "debug_z_blue",  COLOR_Z },
			};

			for(int i = 0; i < 3; ++i) {

				int p = (i + 1) % 3;

				double tx = ox + arm * basis[i][0];
				double ty = oy + arm * basis[i][1];
				double tz = oz + arm * basis[i][2];

				double dx = thickness * basis[p][0];
				double dy = thickness * basis[p][1];
				double dz = thickness * basis[p][2];

				obj << std::format("g debug_{}_{}\n", part_name, axes[i].suffix);
				obj << std::format("usemtl {}\n", axes[i].mtl);

				append_colored_vertex(obj, ox - dx, oy - dy, oz - dz, axes[i].color);
				append_colored_vertex(obj, ox + dx, oy + dy, oz + dz, axes[i].color);
				append_colored_vertex(obj, tx,      ty,      tz,      axes[i].color);

				size_t a = v_counter + 1, b = v_counter + 2, c = v_counter + 3;

				obj << std::format("f {}/{}/{} {}/{}/{} {}/{}/{}\n", a, vt_idx, vn_idx, b, vt_idx, vn_idx, c, vt_idx, vn_idx);
				obj << std::format("f {}/{}/{} {}/{}/{} {}/{}/{}\n", a, vt_idx, vn_idx, c, vt_idx, vn_idx, b, vt_idx, vn_idx);

				v_counter += 3;
			}
		}

	}

	//============================================================================
	// Max vertex distance from origin, total position / uv / normal counts
	//============================================================================
	ModelInfo measure(const model::Model& model) {

		ModelInfo info {1.0, 0, 0, 0, 0};
		double r2_max = 0.0;

		for(const auto& m : model.meshes) {

			info.position_count += m.positions.size();
			info.uv_count       += m.uvs.size();
			info.normal_count   += m.normals.size();
			info.face_count     += m.faces.size();

			for(const auto& v : m.positions) {
				double d2 = v.x * v.x + v.y * v.y + v.z * v.z;

				if(d2 > r2_max)
					r2_max = d2;
			}
		}

		double r = std::sqrt(r2_max);

		if(r > 0.0)
			info.radius = r;

		return info;
	}

	//============================================================================
	// Per-Part Axis triads (OCF)
	//============================================================================
	bool append_triads(
		const std::string& obj_path,
		const std::string& mtl_path,
		std::span<const PartFrame> parts,
		double radius,
		size_t& v_counter,
		size_t& vt_counter,
		size_t& vn_counter
	) {

		if(parts.empty())
			return true;

		std::ofstream mtl(mtl_path, std::ios::app);

		if(!mtl)
			return false;

		mtl << std::format("\n# {} debug triads\n", HEADER);
		append_material_def(mtl, "debug_x_red",   1.0, 0.0, 0.0);
		append_material_def(mtl, "debug_y_green", 0.0, 1.0, 0.0);
		append_material_def(mtl, "debug_z_blue",  0.0, 0.4, 1.0);

		std::ofstream obj(obj_path, std::ios::app);

		if(!obj)
			return false;

		// One dummy vt + vn, referenced by every debug face corner so the face
		// format matches the main model's pos/uv/normal convention
		obj << "vt 0.000000 0.000000\n";
		obj << "vn 0.000000 0.000000 1.000000\n";
		++vt_counter;
		++vn_counter;

		double arm = radius * TRIAD_ARM_FRAC;

		for(const auto& p : parts)
			build_triad(obj, p.name, p.origin[0], p.origin[1], p.origin[2], p.basis, arm, v_counter, vt_counter, vn_counter);

		return true;
	}

	//============================================================================
	// Rotation log
	//============================================================================
	bool write_rot_log(
		const std::string& out_file,
		std::span<const PartFrame> parts
	) {

		if(parts.empty())
			return true;

		std::ofstream file(out_file);

		if(!file)
			return false;

		file << std::format("# {}\n", HEADER);
		file << "# Per-part rotation frames: origin and local +X/+Y/+Z axes in world space.\n\n";

		for(const auto& p : parts) {
			file << std::format("{}\n", p.name);
			file << std::format("  origin   {:+.6f} {:+.6f} {:+.6f}\n", p.origin[0],   p.origin[1],   p.origin[2]);
			file << std::format("  +X world {:+.6f} {:+.6f} {:+.6f}\n", p.basis[0][0], p.basis[0][1], p.basis[0][2]);
			file << std::format("  +Y world {:+.6f} {:+.6f} {:+.6f}\n", p.basis[1][0], p.basis[1][1], p.basis[1][2]);
			file << std::format("  +Z world {:+.6f} {:+.6f} {:+.6f}\n", p.basis[2][0], p.basis[2][1], p.basis[2][2]);
			file << "\n";
		}

		return true;
	}

	//============================================================================
	// Hardpoint markers
	//============================================================================
	bool append_hardpoint_markers(
		const std::string& obj_path,
		const std::string& mtl_path,
		const model::Model& model,
		double radius,
		size_t& v_counter,
		size_t& vt_counter,
		size_t& vn_counter
	) {

		if(model.hardpoints.empty())
			return true;

		std::ofstream mtl(mtl_path, std::ios::app);

		if(!mtl)
			return false;

		mtl << std::format("\n# {} debug hardpoints\n", HEADER);
		append_material_def(mtl, "debug_x_red",   1.0, 0.0, 0.0);
		append_material_def(mtl, "debug_y_green", 0.0, 1.0, 0.0);
		append_material_def(mtl, "debug_z_blue",  0.0, 0.4, 1.0);

		std::ofstream obj(obj_path, std::ios::app);

		if(!obj)
			return false;

		obj << "vt 0.000000 0.000000\n";
		obj << "vn 0.000000 0.000000 1.000000\n";
		++vt_counter;
		++vn_counter;

		double arm = radius * HARDPOINT_ARM_FRAC;

		// World-axis triad: identity basis.
		const double world_basis[3][3] = {
			{1.0, 0.0, 0.0},
			{0.0, 1.0, 0.0},
			{0.0, 0.0, 1.0},
		};

		for(const auto& h : model.hardpoints)
			build_triad(obj, h.name, h.pos.x, h.pos.y, h.pos.z, world_basis, arm, v_counter, vt_counter, vn_counter);

		return true;
	}

	//============================================================================
	// Per-vertex normal lines
	//============================================================================
	bool append_normal_lines(
		const std::string& obj_path,
		const std::string& mtl_path,
		const model::Model& model,
		double radius,
		size_t& v_counter,
		size_t& vt_counter,
		size_t& vn_counter
	) {

		bool has_any = false;

		for(const auto& m : model.meshes) {

			if(!m.positions.empty() && !m.normals.empty()) {
				has_any = true;
				break;
			}
		}

		if(!has_any)
			return true;

		std::ofstream mtl(mtl_path, std::ios::app);

		if(!mtl)
			return false;

		mtl << std::format("\n# {} debug normals\n", HEADER);
		append_material_def(mtl, "debug_normal_lime", 0.5, 1.0, 0.0);

		std::ofstream obj(obj_path, std::ios::app);

		if(!obj)
			return false;

		obj << "g debug_normals\n";
		obj << "usemtl debug_normal_lime\n";

		obj << "vt 0.000000 0.000000\n";
		obj << "vn 0.000000 0.000000 1.000000\n";
		++vt_counter;
		++vn_counter;

		size_t vt_idx = vt_counter;
		size_t vn_idx = vn_counter;

		double arm       = radius * NORMAL_ARM_FRAC;
		double thickness = arm * NORMAL_THICKNESS_FRAC;

		// One narrow double-sided triangle per face-vertex corner
		for(const auto& mesh : model.meshes) {

			for(const auto& face : mesh.faces) {

				for(int i = 0; i < 3; ++i) {

					i32 pi = face.v[i].pos;
					i32 ni = face.v[i].normal;

					if(pi < 0 || ni < 0)
						continue;

					if((size_t)pi >= mesh.positions.size() || (size_t)ni >= mesh.normals.size())
						continue;

					const auto& p = mesh.positions[pi];
					const auto& n = mesh.normals[ni];

					// Perpendicular to n: cross with whichever world axis is
					// least aligned with n, then normalize.
					double ax = (std::abs(n.x) < 0.9) ? 1.0 : 0.0;
					double ay = (std::abs(n.x) < 0.9) ? 0.0 : 1.0;
					double az = 0.0;

					double px = n.y * az - n.z * ay;
					double py = n.z * ax - n.x * az;
					double pz = n.x * ay - n.y * ax;
					double plen = std::sqrt(px*px + py*py + pz*pz);

					if(plen > 0.0) {
						px /= plen;
						py /= plen;
						pz /= plen;
					}

					double dx = thickness * px;
					double dy = thickness * py;
					double dz = thickness * pz;

					double tx = p.x + arm * n.x;
					double ty = p.y + arm * n.y;
					double tz = p.z + arm * n.z;

					append_colored_vertex(obj, p.x - dx, p.y - dy, p.z - dz, COLOR_NORMAL);
					append_colored_vertex(obj, p.x + dx, p.y + dy, p.z + dz, COLOR_NORMAL);
					append_colored_vertex(obj, tx,       ty,       tz,       COLOR_NORMAL);

					size_t a = v_counter + 1, b = v_counter + 2, c = v_counter + 3;
					obj << std::format("f {}/{}/{} {}/{}/{} {}/{}/{}\n", a, vt_idx, vn_idx, b, vt_idx, vn_idx, c, vt_idx, vn_idx);
					obj << std::format("f {}/{}/{} {}/{}/{} {}/{}/{}\n", a, vt_idx, vn_idx, c, vt_idx, vn_idx, b, vt_idx, vn_idx);

					v_counter += 3;
				}
			}
		}

		return true;
	}

	//============================================================================
	// Wireframe in magenta
	//============================================================================
	bool append_collision_overlay(
		const std::string& obj_path,
		const std::string& mtl_path,
		const model::Mesh& collision,
		size_t& v_counter,
		size_t& vt_counter,
		size_t& vn_counter
	) {

		if(collision.positions.empty() || collision.faces.empty())
			return true;

		std::ofstream mtl(mtl_path, std::ios::app);

		if(!mtl)
			return false;

		mtl << std::format("\n# {} debug collision\n", HEADER);
		append_material_def(mtl, "debug_collision_magenta", 1.0, 0.0, 1.0);

		std::ofstream obj(obj_path, std::ios::app);

		if(!obj)
			return false;

		obj << "g debug_collision\n";
		obj << "usemtl debug_collision_magenta\n";

		obj << "vt 0.000000 0.000000\n";
		obj << "vn 0.000000 0.000000 1.000000\n";
		++vt_counter;
		++vn_counter;

		size_t vt_idx = vt_counter;
		size_t vn_idx = vn_counter;

		size_t base = v_counter;

		for(const auto& p : collision.positions)
			append_colored_vertex(obj, p.x, p.y, p.z, COLOR_COLLISION);

		v_counter += collision.positions.size();

		for(const auto& face : collision.faces) {
			size_t a = base + face.v[0].pos + 1;
			size_t b = base + face.v[1].pos + 1;
			size_t c = base + face.v[2].pos + 1;
			obj << std::format("f {}/{}/{} {}/{}/{} {}/{}/{}\n", a, vt_idx, vn_idx, b, vt_idx, vn_idx, c, vt_idx, vn_idx);
			obj << std::format("f {}/{}/{} {}/{}/{} {}/{}/{}\n", a, vt_idx, vn_idx, c, vt_idx, vn_idx, b, vt_idx, vn_idx);
		}

		return true;
	}

}
