#pragma once

#include "../model/converter.h"
#include "../model/model.h"
#include "debug.h"

#include <string_view>
#include <vector>

namespace nova3di::writer {

	struct DebugOpts {
		std::vector<debug::PartFrame> triads;
		bool normals    = false;
		bool hardpoints = false;
		bool collision  = false;
	};

	bool export_model(
		const model::Model& model,
		std::string_view model_name,
		const model::ConvertOptions& opts,
		const DebugOpts& debug = {}
	);

}
