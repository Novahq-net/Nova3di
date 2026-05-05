#pragma once

#include <string_view>

namespace nova3di::model::filters {


	enum class Scope {
		T3di_v2_v5, // DF1 (v2-v5)
		T3di_v7,    // AF3 (v7, mission editor)
		T3di_v8,    // DF2 (v8-v9)
		T3di_v10,   // LW, TFD (v10)
		T3di3,      // JOP, DFX, DFX2
		Gpm,        // BHD, C4
		T3do,       // AF3, C3G (standalone 3DO files, no PAK)
		Pak,        // TTF, F16, M29, F22, L3 (embedded 3DO files in PAK)
	};

	bool is_effect(Scope scope, std::string_view name);

	bool is_shadow(Scope scope, std::string_view name);

}
