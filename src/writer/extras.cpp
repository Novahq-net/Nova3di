#include "extras.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <vector>

namespace nova3di::writer::extras {

	namespace {
		constexpr const char* HEADER = "Converted by NovaHQ Nova3di <https://novahq.net>";

		//====================================================================
		// Convex hull from half-spaces - small math helpers
		//====================================================================
		// Brute-force for the small N (max ~30 planes per volume):
		//   1. Every triple (i, j, k) of distinct planes intersects at a candidate vertex
		//   2. Filter candidates to those satisfying n.p >= d for every plane in the volume
		//   3. For each plane, collect filtered points lying ON it, sort
		//      around the plane normal, fan triangulate
		//====================================================================

		constexpr float HULL_EPSILON = 1e-4f;

		// Solve the 3x3 system [na; nb; nc] * p = (da, db, dc) via Cramer's
		// rule. Returns false if the planes are parallel/coplanar (det ~= 0)
		bool intersect_planes(
			const model::Plane& a,
			const model::Plane& b,
			const model::Plane& c,
			float out[3]
		) {

			float det =
				  a.nx * (b.ny * c.nz - b.nz * c.ny)
				- a.ny * (b.nx * c.nz - b.nz * c.nx)
				+ a.nz * (b.nx * c.ny - b.ny * c.nx);

			if(std::abs(det) < 1e-8f)
				return false;

			float inv_det = 1.0f / det;

			out[0] = inv_det * (
				  a.d  * (b.ny * c.nz - b.nz * c.ny)
				- a.ny * (b.d  * c.nz - b.nz * c.d )
				+ a.nz * (b.d  * c.ny - b.ny * c.d ));

			out[1] = inv_det * (
				  a.nx * (b.d  * c.nz - b.nz * c.d )
				- a.d  * (b.nx * c.nz - b.nz * c.nx)
				+ a.nz * (b.nx * c.d  - b.d  * c.nx));

			out[2] = inv_det * (
				  a.nx * (b.ny * c.d  - b.d  * c.ny)
				- a.ny * (b.nx * c.d  - b.d  * c.nx)
				+ a.d  * (b.nx * c.ny - b.ny * c.nx));

			return true;
		}

		// True when p is on the inside of every half-space (n.p >= d - eps)
		bool inside_hull(
			const float                  p[3],
			std::span<const model::Plane> planes,
			float                        eps
		) {

			for(const auto& pl : planes) {
				float dist = pl.nx * p[0] + pl.ny * p[1] + pl.nz * p[2] - pl.d;

				if(dist < -eps)
					return false;
			}
			return true;
		}

		// Merge a candidate vertex into `verts`, returning its index
		u32 push_unique_vertex(
			std::vector<std::array<float, 3>>& verts,
			const float                        p[3]
		) {

			for(u32 i = 0; i < verts.size(); ++i) {
				float dx = verts[i][0] - p[0];
				float dy = verts[i][1] - p[1];
				float dz = verts[i][2] - p[2];

				if(dx*dx + dy*dy + dz*dz < HULL_EPSILON * HULL_EPSILON)
					return i;
			}

			verts.push_back({p[0], p[1], p[2]});
			return (u32)verts.size() - 1;
		}

		// Order `vertex_indices` (all lying on `face`) counter-clockwise
		// around `face.n` so a fan triangulation is well-formed
		void sort_around_normal(
			std::vector<u32>&                          vertex_indices,
			const model::Plane&                        face,
			const std::vector<std::array<float, 3>>&   verts
		) {

			if(vertex_indices.size() < 3)
				return;

			// Centroid of the face vertices
			float cx = 0, cy = 0, cz = 0;

			for(u32 i : vertex_indices) {
				cx += verts[i][0];
				cy += verts[i][1];
				cz += verts[i][2];
			}

			float n = (float)vertex_indices.size();
			cx /= n; cy /= n; cz /= n;

			// Build an in-plane basis (u, v) orthogonal to face.n
			float ax = (std::abs(face.nx) < 0.9f) ? 1.0f : 0.0f;
			float ay = (std::abs(face.nx) < 0.9f) ? 0.0f : 1.0f;
			float az = 0.0f;

			float ux = face.ny * az - face.nz * ay;
			float uy = face.nz * ax - face.nx * az;
			float uz = face.nx * ay - face.ny * ax;

			float ulen = std::sqrt(ux*ux + uy*uy + uz*uz);

			if(ulen < HULL_EPSILON)
				return;

			ux /= ulen; uy /= ulen; uz /= ulen;

			float vx = face.ny * uz - face.nz * uy;
			float vy = face.nz * ux - face.nx * uz;
			float vz = face.nx * uy - face.ny * ux;

			std::sort(vertex_indices.begin(), vertex_indices.end(), [&](u32 a, u32 b) {
				float dax = verts[a][0] - cx, day = verts[a][1] - cy, daz = verts[a][2] - cz;
				float dbx = verts[b][0] - cx, dby = verts[b][1] - cy, dbz = verts[b][2] - cz;

				float angle_a = std::atan2(dax * vx + day * vy + daz * vz, dax * ux + day * uy + daz * uz);
				float angle_b = std::atan2(dbx * vx + dby * vy + dbz * vz, dbx * ux + dby * uy + dbz * uz);

				return angle_a < angle_b;
			});
		}

		// Append an axis-aligned box from `bbox` to `out` as a fallback when
		// the plane intersection fails. 8 verts, 12 tris
		void append_aabb_box(
			const std::array<float, 6>& bbox,
			const std::string&          group_name,
			model::Mesh&                out
		) {

			u32 base = (u32)out.positions.size();
			float xmin = bbox[0], ymin = bbox[1], zmin = bbox[2];
			float xmax = bbox[3], ymax = bbox[4], zmax = bbox[5];

			out.positions.push_back({xmin, ymin, zmin});
			out.positions.push_back({xmax, ymin, zmin});
			out.positions.push_back({xmax, ymax, zmin});
			out.positions.push_back({xmin, ymax, zmin});
			out.positions.push_back({xmin, ymin, zmax});
			out.positions.push_back({xmax, ymin, zmax});
			out.positions.push_back({xmax, ymax, zmax});
			out.positions.push_back({xmin, ymax, zmax});

			static constexpr int box_tris[12][3] = {
				{0,1,2}, {0,2,3},  {4,6,5}, {4,7,6},
				{0,4,5}, {0,5,1},  {3,2,6}, {3,6,7},
				{0,3,7}, {0,7,4},  {1,5,6}, {1,6,2},
			};

			for(const auto& t : box_tris) {
				model::Face face = {};
				face.v[0] = {(i32)(base + t[0]), -1, -1};
				face.v[1] = {(i32)(base + t[1]), -1, -1};
				face.v[2] = {(i32)(base + t[2]), -1, -1};
				out.faces.push_back(face);
			}

			(void)group_name;  // group declarations are written by extras::write_collision
		}

	}  // namespace

	//============================================================================
	// Convex hull from half-space planes, one mesh per volume
	//============================================================================
	bool build_hull(
		std::span<const model::Plane> planes,
		const std::array<float, 6>&   bbox,
		const std::string&            group_name,
		model::Mesh&                  out
	) {

		// Need at least 4 planes to bound a finite volume.
		if(planes.size() < 4) {
			append_aabb_box(bbox, group_name, out);
			return false;
		}

		// Generate candidate vertices from every (i, j, k) plane triple
		std::vector<std::array<float, 3>> verts;
		std::vector<std::vector<u32>>     plane_to_verts(planes.size());

		for(size_t i = 0; i < planes.size(); ++i) {
			for(size_t j = i + 1; j < planes.size(); ++j) {
				for(size_t k = j + 1; k < planes.size(); ++k) {

					float p[3];

					if(!intersect_planes(planes[i], planes[j], planes[k], p))
						continue;

					if(!inside_hull(p, planes, HULL_EPSILON))
						continue;

					u32 idx = push_unique_vertex(verts, p);

					plane_to_verts[i].push_back(idx);
					plane_to_verts[j].push_back(idx);
					plane_to_verts[k].push_back(idx);
				}
			}
		}

		if(verts.size() < 4) {
			append_aabb_box(bbox, group_name, out);
			return false;
		}

		// For each plane, dedupe its vertex list, sort around the plane normal, and fan-triangulate
		u32 v_base = (u32)out.positions.size();

		for(const auto& v : verts)
			out.positions.push_back({v[0], v[1], v[2]});

		size_t face_count_before = out.faces.size();

		for(size_t i = 0; i < planes.size(); ++i) {
			auto& list = plane_to_verts[i];

			std::sort(list.begin(), list.end());
			list.erase(std::unique(list.begin(), list.end()), list.end());

			if(list.size() < 3)
				continue;

			sort_around_normal(list, planes[i], verts);

			for(size_t k = 1; k + 1 < list.size(); ++k) {
				model::Face face = {};
				face.v[0] = {(i32)(v_base + list[0]),     -1, -1};
				face.v[1] = {(i32)(v_base + list[k]),     -1, -1};
				face.v[2] = {(i32)(v_base + list[k + 1]), -1, -1};
				out.faces.push_back(face);
			}
		}

		if(out.faces.size() == face_count_before) {
			out.positions.resize(v_base);
			append_aabb_box(bbox, group_name, out);
			return false;
		}

		(void)group_name;
		return true;
	}

	//============================================================================
	// Write hardpoints to a simple text file
	//============================================================================
	bool write_hardpoints(const model::Model& model, const std::string& out_file) {

		if(model.hardpoints.empty())
			return true;

		std::ofstream file(out_file);

		if(!file)
			return false;

		file << std::format("# {}\n", HEADER);

		if(!model.source_name.empty())
			file << std::format("# Hardpoints for {}\n", model.source_name);

		for(const auto& hp : model.hardpoints)
			file << std::format("{:<32} {:.6f} {:.6f} {:.6f}\n", hp.name, hp.pos.x, hp.pos.y, hp.pos.z);

		return true;
	}

	//============================================================================
	// Write collision mesh
	//============================================================================
	bool write_collision(const model::Mesh& collision, const std::string& out_file) {

		if(collision.positions.empty() || collision.faces.empty())
			return true;

		std::ofstream file(out_file);

		if(!file)
			return false;

		std::string name = collision.group_name.empty() ? "collision" : collision.group_name;

		std::string mtl_path = out_file;
		size_t dot = mtl_path.find_last_of('.');
		if(dot != std::string::npos)
			mtl_path.replace(dot, std::string::npos, ".mtl");
		else
			mtl_path += ".mtl";

		std::string mtl_name = mtl_path.substr(mtl_path.find_last_of("/\\") + 1);

		std::ofstream mtl(mtl_path);
		if(mtl) {
			mtl << std::format("# {}\n", HEADER);
			mtl << "newmtl collision\n";
			mtl << "Kd 0.2 0.5 1\n";
			mtl << "d 0.4\n";
		}

		file << std::format("# {}\n", HEADER);
		file << std::format("mtllib {}\n", mtl_name);
		file << std::format("o {}\n", name);
		file << std::format("g {}\n", name);
		file << "usemtl collision\n";

		for(const auto& p : collision.positions)
			file << std::format("v {:.6f} {:.6f} {:.6f}\n", p.x, p.y, p.z);

		for(const auto& face : collision.faces)
			file << std::format("f {} {} {}\n", face.v[0].pos + 1, face.v[1].pos + 1, face.v[2].pos + 1);

		return true;
	}

}
