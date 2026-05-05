#include "converter.h"

#include "../util/io.h"

namespace nova3di::model {

	FileFormat detect(const u8* bytes, size_t size, std::string_view filename) {

		std::string ext = util::io::extension(filename);

		if(ext == ".ai")
			return FileFormat::Ai;
			
		if(ext == ".ocf")
			return FileFormat::Ocf;

		if(size < 4)
			return FileFormat::Unknown;

		auto magic = [bytes](const char* m) {

			for(size_t i = 0; m[i]; ++i) {

				if(bytes[i] != (u8)m[i]) 
					return false;

			}

			return true;
		};

		if(magic("GPM") || magic("GPS") || magic("GPP")) return FileFormat::Gpm;
		if(magic("3DPK"))                                return FileFormat::Pak;
		if(magic("3DI3"))                                return FileFormat::T3di3;
		if(magic("3DO"))                                 return FileFormat::T3do;
		if(magic("3DI"))                                 return FileFormat::T3di;

		return FileFormat::Unknown;
	}

}
