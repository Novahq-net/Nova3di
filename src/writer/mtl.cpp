#include "mtl.h"

#include <format>
#include <fstream>
#include <vector>

namespace nova3di::writer::mtl {

	namespace {
		constexpr const char* HEADER = "Converted by NovaHQ Nova3di <https://novahq.net>";
	}

	//============================================================================
	// Wavefront MTL
	//============================================================================
	bool write(
		const model::Model& model,
		const std::string& out_file,
		bool metadata
	) {

		std::ofstream file(out_file);

		if(!file)
			return false;

		std::vector<u8> used(model.materials.size(), 0);
		for(const auto& mesh : model.meshes) {
			for(const auto& face : mesh.faces) {
				if(face.material < model.materials.size())
					used[face.material] = 1;
			}
		}

		file << std::format("# {}\n", HEADER);

		if(!model.source_name.empty())
			file << std::format("# {}\n", model.source_name);

		for(size_t i = 0; i < model.materials.size(); ++i) {

			if(!used[i])
				continue;

			const auto& mat = model.materials[i];

			if(mat.name.empty())
				continue;

			file << std::format("\nnewmtl {}\n", mat.name);
			file << std::format("Kd {:g} {:g} {:g}\n", mat.color[0], mat.color[1], mat.color[2]);

			if(mat.opacity < 1.0f)
				file << std::format("d {:.4f}\n", mat.opacity);

			if(!mat.texture.empty())
				file << std::format("map_Kd {}\n", mat.texture);

			if(!mat.alpha.empty())
				file << std::format("map_d {}\n", mat.alpha);

			if(!mat.emission.empty()) {
				file << "Ke 1 1 1\n";
				file << std::format("map_Ke {}\n", mat.emission);
			}

			if(metadata) {

				for(const auto& [k, v] : mat.metadata)
					file << std::format("#nova:{} {}\n", k, v);

			}

		}

		return true;
	}

}
