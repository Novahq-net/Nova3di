#pragma once

#include "../model/model.h"
#include <span>
#include <string>

namespace nova3di::writer::extras {

	bool write_hardpoints(const model::Model& model, const std::string& out_file);

	bool write_collision(const model::Mesh& collision, const std::string& out_file);

	bool build_hull(
		std::span<const model::Plane> planes,
		const std::array<float, 6>&   bbox,
		const std::string&            group_name,
		model::Mesh&                  out
	);

}
