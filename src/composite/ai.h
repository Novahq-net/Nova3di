#pragma once

#include "../model/converter.h"

namespace nova3di::composite::ai {

	class Converter : public model::Converter {
	public:
		bool convert(
			const std::string& input_file, 
			const model::ConvertOptions& opts
		) override;

		std::string_view format_name() const override {
			return "AI";
		}
	};

}
