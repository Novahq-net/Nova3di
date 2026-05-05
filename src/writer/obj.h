#pragma once

#include "../model/model.h"
#include <string>

namespace nova3di::writer::obj {

	bool write(
		const model::Model& model,
		const std::string& obj_path,
		const std::string& mtl_name
	);

}
