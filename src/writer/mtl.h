#pragma once

#include "../model/model.h"
#include <string>

namespace nova3di::writer::mtl {

	bool write(
		const model::Model& model, 
		const std::string& out_file, 
		bool metadata = false
	);

}
