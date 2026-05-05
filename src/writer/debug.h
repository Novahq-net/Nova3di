#pragma once

#include "../model/model.h"
#include <span>
#include <string>

namespace nova3di::writer::debug {

	struct PartFrame {
		std::string name;
		double origin[3];
		double basis[3][3];
	};

	struct ModelInfo {
		double radius;
		size_t position_count;
		size_t uv_count;
		size_t normal_count;
		size_t face_count;
	};

	ModelInfo measure(const model::Model& model);

	bool append_triads(
		const std::string& obj_path,
		const std::string& mtl_path,
		std::span<const PartFrame> parts,
		double radius,
		size_t& v_counter,
		size_t& vt_counter,
		size_t& vn_counter
	);

	bool write_rot_log(
		const std::string& out_file,
		std::span<const PartFrame> parts
	);

	bool append_hardpoint_markers(
		const std::string& obj_path,
		const std::string& mtl_path,
		const model::Model& model,
		double radius,
		size_t& v_counter,
		size_t& vt_counter,
		size_t& vn_counter
	);

	bool append_normal_lines(
		const std::string& obj_path,
		const std::string& mtl_path,
		const model::Model& model,
		double radius,
		size_t& v_counter,
		size_t& vt_counter,
		size_t& vn_counter
	);

	bool append_collision_overlay(
		const std::string& obj_path,
		const std::string& mtl_path,
		const model::Mesh& collision,
		size_t& v_counter,
		size_t& vt_counter,
		size_t& vn_counter
	);

}
