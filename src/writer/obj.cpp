#include "obj.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <numeric>

namespace nova3di::writer::obj {

	namespace {
		constexpr const char* HEADER = "Converted by NovaHQ Nova3di <https://novahq.net>";
	}

	//============================================================================
	// Wavefront OBJ
	//============================================================================
	bool write(
		const model::Model& model,
		const std::string& obj_path,
		const std::string& mtl_name
	) {

		std::ofstream file(obj_path);

		if(!file)
			return false;

		file << std::format("# {}\n", HEADER);

		if(!model.source_name.empty())
			file << std::format("# {}\n", model.source_name);

		if(!mtl_name.empty())
			file << std::format("mtllib {}\n", mtl_name);

		u32 v_base  = 1;
		u32 vt_base = 1;
		u32 vn_base = 1;

		u32 last_material = UINT32_MAX;

		for(const auto& mesh : model.meshes) {

			if(!mesh.group_name.empty())
				file << std::format("g {}\n", mesh.group_name);

			for(const auto& p : mesh.positions)
				file << std::format("v {:.6f} {:.6f} {:.6f}\n", p.x, p.y, p.z);

			for(const auto& uv : mesh.uvs)
				file << std::format("vt {:.6f} {:.6f}\n", uv.u, uv.v);

			for(const auto& n : mesh.normals)
				file << std::format("vn {:.6f} {:.6f} {:.6f}\n", n.x, n.y, n.z);

			std::vector<size_t> order(mesh.faces.size());

			std::iota(order.begin(), order.end(), 0);

			std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
				return mesh.faces[a].material < mesh.faces[b].material;
			});

			for(size_t i : order) {
				const auto& face = mesh.faces[i];

				if(face.material != last_material &&
				   face.material < model.materials.size() &&
				   !model.materials[face.material].name.empty()) {

					file << std::format("usemtl {}\n", model.materials[face.material].name);
					last_material = face.material;

				}

				file << 'f';

				for(int j = 0; j < 3; ++j) {

					const auto& fv = face.v[j];

					i32 v  = (fv.pos    >= 0) ? (fv.pos    + (i32)v_base)  : 0;
					i32 vt = (fv.uv     >= 0) ? (fv.uv     + (i32)vt_base) : 0;
					i32 vn = (fv.normal >= 0) ? (fv.normal + (i32)vn_base) : 0;

					if(vt && vn)
						file << std::format(" {}/{}/{}", v, vt, vn);
					else if(vn)
						file << std::format(" {}//{}", v, vn);
					else if(vt)
						file << std::format(" {}/{}", v, vt);
					else
						file << std::format(" {}", v);

				}

				file << '\n';
			}

			v_base  += (u32)mesh.positions.size();
			vt_base += (u32)mesh.uvs.size();
			vn_base += (u32)mesh.normals.size();
		}

		return true;
	}

}
