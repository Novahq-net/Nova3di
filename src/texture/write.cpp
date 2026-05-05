#include "write.h"

#include "palette.h"
#include "pcx.h"
#include "tga.h"

#include <fstream>
#include <map>
#include <set>

namespace nova3di::texture {

	namespace {

		//========================================================================
		// Bleed RGB from opaque pixels into transparent pixels in a 32bpp BGRA buffer
		//========================================================================
		void bleed_bgra_inplace(std::vector<u8>& bgra, int w, int h) {

			size_t npix = (size_t)w * h;
			std::vector<u8> filled(npix, 0);
			std::vector<i32> queue;
			queue.reserve(npix);

			for(size_t i = 0; i < npix; ++i) {
				if(bgra[i*4 + 3] > 0) {
					filled[i] = 1;
					queue.push_back((i32)i);
				}
			}

			tga::bleed_rgb(bgra.data(), filled, queue, w, h);
		}

		//========================================================================
		// Write one TGA/PCX from explicit pixel buffer + spec + name
		//========================================================================
		std::string write_image(
			const std::string& out_dir,
			const std::string& base_name,
			Format             format,
			int                w,
			int                h,
			const u8*          pixels,
			const std::array<u8, 768>& palette,
			const TextureSpec&         spec,
			const std::vector<u8>&     raw_bytes,
			const std::string&         raw_ext,
			bool                       raw
		) {
			if(raw) {
				if(!raw_bytes.empty() && !raw_ext.empty()) {
					std::string filename = base_name + "." + raw_ext;
					std::string dst = out_dir + "\\" + filename;
					std::ofstream out(dst, std::ios::binary);

					if(!out) 
						return "";

					out.write(reinterpret_cast<const char*>(raw_bytes.data()), raw_bytes.size());
					return filename;
				}

				if(format == Format::Paletted && w > 0 && h > 0) {
					std::string filename = base_name + ".pcx";
					std::string dst = out_dir + "\\" + filename;
					u8 pal_compact[768];

					for(int i = 0; i < 256; ++i) {
						pal_compact[i*3 + 0] = palette[i*4 + 0];
						pal_compact[i*3 + 1] = palette[i*4 + 1];
						pal_compact[i*3 + 2] = palette[i*4 + 2];
					}

					if(pcx::write_pixels(dst, w, h, pixels, pal_compact))
						return filename;

				}

				return "";
			}

			std::string filename = base_name + ".tga";
			std::string dst = out_dir + "\\" + filename;

			bool force_opaque = (spec.mode == AlphaMode::Opaque);

			if(format == Format::Bgra) {
	
				bool bgr24 = force_opaque;

				if(spec.mode == AlphaMode::Preserve && !force_opaque) {
					bgr24 = true;

					size_t npix = (size_t)w * h;
					u8 first_alpha = pixels[3];

					for(size_t i = 1; i < npix; ++i) {
						if(pixels[i*4 + 3] != first_alpha) {
							bgr24 = false;
							break;
						}

					}

				}

				bool result = bgr24
					? tga::write_bgr24(dst, w, h, pixels)
					: tga::write_bgra32(dst, w, h, pixels);


				if(!result) 
					return "";

				return filename;
			}

			u8 pal_rgba[1024];
			palette::expand_rgba(palette.data(), pal_rgba);

			if(!tga::write_tga(w, h, pixels, pal_rgba, dst, spec))
				return "";

			return filename;
		}

		//========================================================================
		// Render one Image to disk: bleed lifecycle, primary, frames, companions.
		// Returns the canonical filename (primary) or "" on failure
		//========================================================================
		std::string render_image(
			const std::string& out_dir,
			const std::string& base_name,
			const Image&       image,
			bool               raw
		) {

			if(base_name.empty() || image.width <= 0 || image.height <= 0)
				return "";

			// Bleed once for non-Opaque Bgra primaries; both primary 32bpp write
			// and any companion 24bpp writes consume the bled buffer (matches
			// the old dds::to_tga + filesystem-readback flow). Opaque-primary
			// Bgra textures use the unbled DXT-decoded RGB directly, also
			// matching the old strip_alpha=true branch.
			std::vector<u8> shared_bgra;
			const u8* primary_pixels = image.pixels.data();

			if(!raw && image.format == Format::Bgra && image.spec.mode != AlphaMode::Opaque) {
				shared_bgra = image.pixels;
				bleed_bgra_inplace(shared_bgra, image.width, image.height);

				if(image.spec.alpha_cutoff > 0)
					tga::cutoff_alpha(shared_bgra.data(), image.width, image.height,
						image.spec.alpha_cutoff, image.spec.alpha_cutoff_invert);

				primary_pixels = shared_bgra.data();
			}

			std::string canonical_name = image.frames.empty() ? base_name : (base_name + "_f0");

			std::string written = write_image(
				out_dir, canonical_name, image.format, image.width, image.height,
				primary_pixels, image.palette, image.spec, image.raw_bytes, image.raw_ext, raw);

			if(written.empty())
				return "";

			for(size_t f = 0; f < image.frames.size(); ++f) {
				std::string frame_name = base_name + "_f" + std::to_string(f + 1);
				write_image(
					out_dir, frame_name, image.format, image.width, image.height,
					image.frames[f].data(), image.palette, image.spec, {}, "", raw);
			}

			if(!raw) {
				for(const auto& comp : image.companions) {
					std::string comp_name = base_name + comp.suffix;
					write_image(
						out_dir, comp_name, image.format, image.width, image.height,
						primary_pixels, image.palette, comp.spec, {}, "", false);
				}
			}

			return written;
		}

	} // namespace

	//========================================================================
	// Write one Image to disk
	//========================================================================
	bool write(const std::string& out_dir, const Image& image, bool raw) {
		return !render_image(out_dir, image.name, image, raw).empty();
	}

	//========================================================================
	// Write a batch of Images to disk, return stats and filenames
	// Dedup by keys (t3di) or names (PAK/3DO/OCF/AI)
	//========================================================================
	WriteResult write_all(
		const std::string& out_dir,
		std::span<const Image> images,
		bool raw,
		std::span<const DedupKey> keys
	) {

		WriteResult result;
		result.filenames.assign(images.size(), std::string{});

		// Two dedup paths:
		// (a) DedupKey per image (t3di): two images sharing a key
		//     share an emitted filename. Disambiguated bases get _2/_3 suffixes.
		// (b) No keys (PAK / 3DO / OCF / AI): first-wins by image.name. A second
		//     image with the same name reuses the first's filename without
		//     re-rendering. Each Image's name is already disambiguated upstream
		//     via texture::mode_suffix when alpha-mode variants exist
		const bool dedup = !keys.empty();

		std::map<DedupKey, std::string>    seen;
		std::set<std::string>              used;
		std::map<std::string, std::string> name_to_filename;

		for(size_t i = 0; i < images.size(); ++i) {

			const Image& image = images[i];

			if(image.name.empty() || image.width <= 0 || image.height <= 0) {

				if(!image.name.empty())
					++result.missing;

				continue;
			}

			if(dedup) {

				DedupKey key = keys[i];

				if(auto it = seen.find(key); it != seen.end()) {
					result.filenames[i] = it->second;
					++result.found;
					continue;
				}

				std::string base = image.name;
				std::string name = base;
				int n = 2;

				while(used.count(name))
					name = base + "_" + std::to_string(n++);

				used.insert(name);

				std::string written = render_image(out_dir, name, image, raw);

				if(written.empty()) {
					++result.missing;
					continue;
				}

				seen[key] = written;
				result.filenames[i] = written;
				++result.found;
			}
			else {

				if(auto it = name_to_filename.find(image.name); it != name_to_filename.end()) {
					result.filenames[i] = it->second;
					++result.found;
					continue;
				}

				std::string written = render_image(out_dir, image.name, image, raw);

				if(written.empty()) {
					++result.missing;
					continue;
				}

				name_to_filename[image.name] = written;
				result.filenames[i] = written;
				++result.found;
			}
		}

		return result;
	}

}
