#include "export.h"

#include "debug.h"
#include "mtl.h"
#include "obj.h"
#include "../util/io.h"
#include "../util/log.h"

namespace nova3di::writer {

	namespace {
		namespace nlog    = nova3di::util::log;
		namespace io      = nova3di::util::io;
	}

	//============================================================================
	// Save model as <opts.out_dir>/_<model_name>.obj + .mtl
	//============================================================================
	bool export_model(
		const model::Model& model,
		std::string_view model_name,
		const model::ConvertOptions& opts,
		const DebugOpts& dbg
	) {

		std::string base     = io::file_prefix(model_name);
		std::string mtl_name = base + ".mtl";
		std::string mtl_path = opts.out_dir + "\\" + mtl_name;
		std::string obj_path = opts.out_dir + "\\" + base + ".obj";

		if(!mtl::write(model, mtl_path, opts.metadata)) {
			nlog::error("cannot create '%s'", mtl_path.c_str());
			return false;
		}

		if(!obj::write(model, obj_path, mtl_name)) {
			nlog::error("cannot create '%s'", obj_path.c_str());
			return false;
		}

		// Debug overlays append to the existing OBJ/MTL
		bool any_debug = !dbg.triads.empty() || dbg.normals || dbg.hardpoints || dbg.collision;

		if(any_debug) {
			debug::ModelInfo info = debug::measure(model);
			size_t v_counter  = info.position_count;
			size_t vt_counter = info.uv_count;
			size_t vn_counter = info.normal_count;

			if(!dbg.triads.empty()) {
				debug::append_triads(obj_path, mtl_path, dbg.triads, info.radius, v_counter, vt_counter, vn_counter);
				debug::write_rot_log(opts.out_dir + "\\" + base + ".rot.txt", dbg.triads);
			}

			if(dbg.hardpoints)
				debug::append_hardpoint_markers(obj_path, mtl_path, model, info.radius, v_counter, vt_counter, vn_counter);

			if(dbg.normals)
				debug::append_normal_lines(obj_path, mtl_path, model, info.radius, v_counter, vt_counter, vn_counter);

			if(dbg.collision)
				debug::append_collision_overlay(obj_path, mtl_path, model.collision, v_counter, vt_counter, vn_counter);
		}

		return true;
	}

}
