#pragma once

#include "../util/types.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nova3di::texture {

	//============================================================================
	// Pixel layout descriptor for Image::pixels
	//============================================================================
	enum class Format {
		Paletted, // 8bpp indices; palette is meaningful
		Bgra,     // 32bpp BGRA pre-decoded
	};

	//============================================================================
	// Alpha handling per texture
	//============================================================================
	enum class AlphaMode {
		Opaque,     // alpha = 255 (paletted) or strip Bgra alpha
		Preserve,   // pass source alpha through (Bgra only)
		IndexAlpha, // alpha = idx (raw)
		IndexZero,  // alpha = 0 when idx == 0
		Luminance,  // alpha = palette[idx] luminance
		Shade,      // alpha = abs(idx - 128) * 255 / shade_delta; RGB = idx as gray
		Mask,       // alpha = spec.mask_brightness[x,y]
		Explicit,   // alpha = byte 1 of [idx, alpha] pair (ratio-2)
	};

	enum class RgbSource {
		Palette,    // RGB from palette (default)
		Black,      // RGB = (0,0,0) (DF1 brightness-blend)
		Tint,       // RGB = constant {tint_r, tint_g, tint_b}
	};

	//============================================================================
	// Suffix appended to a texture base name to disambiguate per-3DO-entry
	// alpha-mode variants. Two 3DO entries that reference the same pool name
	// with different AlphaMode settings produce distinct output filenames.
	// Opaque + Preserve use the original name with no suffix
	//============================================================================
	constexpr std::string_view mode_suffix(AlphaMode mode) {
		switch(mode) {
			case AlphaMode::Opaque:     return "";
			case AlphaMode::Preserve:   return "";
			case AlphaMode::IndexAlpha: return "_idxalpha";
			case AlphaMode::IndexZero:  return "_idxzero";
			case AlphaMode::Luminance:  return "_luminance";
			case AlphaMode::Shade:      return "_shade";
			case AlphaMode::Mask:       return "_mask";
			case AlphaMode::Explicit:   return "_explicit";
		}
		return "";
	}

	//============================================================================
	// Per-pixel alpha map, populated by t3di's bake_mask_brightness
	// from a sibling texture (v10 mask composite). When set, AlphaMode::Mask
	// reads alpha values from these pixels per output pixel.
	//============================================================================
	struct MaskBrightness {
		int             width  = 0;
		int             height = 0;
		std::vector<u8> pixels; // w*h, brightness 0-255
	};

	//============================================================================
	// One output spec. The Image's primary spec produces the canonical output;
	// each Companion carries its own spec with a non-empty suffix.
	//============================================================================
	struct TextureSpec {
		AlphaMode mode = AlphaMode::Opaque;
		RgbSource rgb  = RgbSource::Palette;

		u8    tint_r = 0xFF; // RgbSource::Tint
		u8    tint_g = 0xFF;
		u8    tint_b = 0xFF;
		bool  bleed = true;
		float opacity = 1.0f;
		u8    alpha_cutoff = 0; // (D3DRS_ALPHAREF). 0 = no cutoff.
		bool  alpha_cutoff_invert = false;

		std::optional<MaskBrightness> mask_brightness;
		std::optional<std::array<u8, 3>> idx0_rgb;
	};

	//============================================================================
	// Same source pixels as the parent Image, different alpha treatment, output 
	// filename gets "<image.name><suffix>.<ext>". Used by gpm/t3di3 _opaque
	//============================================================================
	struct Companion {
		std::string suffix;
		TextureSpec spec;
	};

	//============================================================================
	// Unified texture asset
	//============================================================================
	struct Image {
		Format               format = Format::Paletted;
		TextureSpec          spec;  // primary output spec

		std::string          name; // basename
		int                  width  = 0;
		int                  height = 0;
		std::vector<u8>      pixels;       // 8bpp indices (Paletted) or 32bpp BGRA (Bgra)
		std::array<u8, 768>  palette = {}; // Paletted only

		std::vector<u8>      raw_bytes; // --raw mode passthrough
		std::string          raw_ext;

		std::vector<std::vector<u8>> frames;

		std::vector<Companion>       companions;
	};

}
