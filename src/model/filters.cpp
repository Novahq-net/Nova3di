#include "filters.h"

#include "../util/io.h"

#include <algorithm>
#include <span>

namespace nova3di::model::filters {

	namespace {

		//============================================================================
		// Per-format texture filter registry. Each list contains a list of 
		// textures we exclude without the --effects flag
		//============================================================================
		constexpr std::string_view t3do_effect[] = {
			"BGLOW", "BMGLOW", "OGLOW", "OMGLOW", "CGLOW", "IGLOW",
			"OBFGLOW", "FGLOW", "MGLOW", "OSGLOW", "HGLOW",
			"ATHRUST", "ITHRUST", "OTHRUST", "OTHRM", "OTHRH",
			"ITHRM", "ITHRH", "ICTHR", "MTHRST_", "SCTH1_", "ITHST",
			"CMISFLAM", "CMISGLOW", "DMISFLAM", "DMISGLOW",
			"MISLFLAM", "MISLGLOW", "TMISFLAM", "TMISGLOW",
			"HEILFLAM", "HEILGLOW", "PROKFLAM", "PROKGLOW",
			"SROKFLAM", "SROKGLOW",
			"BFLAR", "GFLAR", "CFLAR", "MFLAR", "SCFLAR_",
			"DCORVE01", "DCORVG01", "EAFTRG01", "EAFTRG02", "EAFTRG03",
			"DTGLOW0", "DTRAC05", "RAILGLOW", "BSOLPGLW", "BSOLPTHR",
			"DISCLITM", "DABTG", "ESMRTD", "DRADS06",
			"BCNBLU", "BCNRED", "BCNYEL", "BCNWHT", "BCNGRN",
			"BJET_", "EFRGJET", "EMLJET", "MJET", "JLET", "HJET",
			"BENGINE", "EMLGLW", "BURNER0", "ORUNLIT", "PROP01",
			"BLITE0", "BRLIT_", "RLIT_", "BEAMFWHT", "LITEBEAM",
			"SMOKE", "SPARK", "FLARE", "IRING", "SHLD", "ELEC4_",
			"BGMINE1", "ELTFGTST", "COM13",
			"V22H10",
			"DCLAWE01", "DCLAWF01", "DCLAWG01",
			"GM1803", "GVUL13",
		};

		constexpr std::string_view t3do_shadow[] = {
			"RSHDW", "rshdw", "R30SDW", "RTRPSDW",           // HOWIT / AMX30 / TNKTRPLN
			"M1S01", "M1S01_M", "M10917",                    // Universal
			"DANA16", "DANA17",                              // DANA
			"SSA08", "AA10",                                 // APC / Aardvark chains
			"hut08",                                         // building shadows
			"dlecshd1", "dlecshd2", "dolishd1", "dolishd2",  // building shadows (lec/oli families)
			"NMTNT06",                                       // uhhh
			"NAN10",                                         // nanuchka (c3g)
			"SA8_SH",                                        // SA8 (f22)
		};

		constexpr std::string_view t3di_v2_v5_effect[] = {
			"COM13", "fntn09",            // fountain
			"AMGF01", "AMGF02", "AMGF03", // muzzle flash
			"amga1_", "amga2_", "amga3_", // muzzle flash
			"amgb1_", "amgb2_", "amgb3_", // muzzle flash
		};

		constexpr std::string_view t3di_v2_v5_shadow[] = {
			"m1s01", "klshadow", "ALPHSHAD", "m35shad",
		};

		constexpr std::string_view t3di_v8_effect[] = {
			"BFIR", "KFIRE",                                    // big fire / ambient KHUFU fire
			"CLRBEM", "RKTFLM",                                 // streetlight beam cones / rocket flame
			"fntn09", "alphshad", "COM13", "DHAV10", "ACESN08", // misc (vpickup / Havoc blades / ocessna)
			"wflash0", "Yflash0",                               // muzzle flash white/yellow
			"AMGF01", "AMGF02", "AMGF03",                       // muzzle flash
			"amga1_", "amga2_", "amga3_",                       // muzzle flash
			"amgb1_", "amgb2_", "amgb3_",                       // muzzle flash
		};

		constexpr std::string_view t3di_v8_shadow[] = {
			"m1s01", "klshadow", "ALPHSHAD", "m35shad",
		};

		constexpr std::string_view t3di_v10_effect[] = {
			"WFLASH", "YFLASH", "gflash", "flash0", "flash1", "AMGF0", // muzzle flashes
			"ABeamA",                                                  // beam effect (v10+)
			"COM13",
			"LAWFIRE",
			"bh_rotor"
		};

		constexpr std::string_view t3di_v10_shadow[] = {
			"alphshad", "m1s01", "klshadow", "m35shad", "apcshad",
		};

		constexpr std::string_view gpm_effect[] = {
			"alphshad"
		};

		constexpr std::string_view gpm_shadow[] = {
			"m1s01"
		};

		constexpr std::string_view t3di3_effect[] = {
			//"alphshad"
			"fakealphashadfiltername"
		};

		constexpr std::string_view t3di3_shadow[] = {
			//"Tglow", "Jglow",
			"fakealphashadfiltername",
		};

		//============================================================================
		// Case insensitive substring match against a list
		//============================================================================
		bool matches_any(std::string_view name, std::span<const std::string_view> list) {

			if(name.empty() || list.empty())
				return false;

			std::string up = util::io::to_upper(std::string{name});

			return std::ranges::any_of(list, [&](std::string_view k) {
				std::string uk = util::io::to_upper(std::string{k});
				return up.find(uk) != std::string::npos;
			});
		}

		std::span<const std::string_view> effect_list(Scope scope) {
			switch(scope) {
				case Scope::T3do:       return t3do_effect;
				case Scope::Pak:        return t3do_effect;
				case Scope::T3di_v2_v5: return t3di_v2_v5_effect;
				case Scope::T3di_v7:    return t3di_v8_effect;
				case Scope::T3di_v8:    return t3di_v8_effect;
				case Scope::T3di_v10:   return t3di_v10_effect;
				case Scope::Gpm:        return gpm_effect;
				case Scope::T3di3:      return t3di3_effect;
			}
			return {};
		}

		std::span<const std::string_view> shadow_list(Scope scope) {
			switch(scope) {
				case Scope::T3do:       return t3do_shadow;
				case Scope::Pak:        return t3do_shadow;
				case Scope::T3di_v2_v5: return t3di_v2_v5_shadow;
				case Scope::T3di_v7:    return t3di_v8_shadow;
				case Scope::T3di_v8:    return t3di_v8_shadow;
				case Scope::T3di_v10:   return t3di_v10_shadow;
				case Scope::Gpm:        return gpm_shadow;
				case Scope::T3di3:      return t3di3_shadow;
			}
			return {};
		}

	}

	//============================================================================
	// Determine if a texture is an effect by texture name
	//============================================================================
	bool is_effect(Scope scope, std::string_view name) {
		return matches_any(name, effect_list(scope));
	}

	//============================================================================
	// Determine if a texture is a shadow by texture name
	//============================================================================
	bool is_shadow(Scope scope, std::string_view name) {
		return matches_any(name, shadow_list(scope));
	}

}
