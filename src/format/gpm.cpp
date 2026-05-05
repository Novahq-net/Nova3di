#include "gpm.h"

#include "../model/model.h"
#include "../texture/parse.h"
#include "../texture/write.h"
#include "../util/io.h"
#include "../util/log.h"
#include "../util/types.h"
#include "../writer/debug.h"
#include "../writer/export.h"
#include "../writer/extras.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace nova3di::format::gpm {

	namespace {

		namespace nlog    = nova3di::util::log;
		namespace io      = nova3di::util::io;
		namespace texture = nova3di::texture;
		namespace filters = nova3di::model::filters;

		// Header
		constexpr u32 GPM_HEADER_SIZE       = 236;
		constexpr u32 GPM_OFF_SUBTYPE       = 0x02;
		constexpr u32 GPM_OFF_VERSION_BYTES = 0x03;
		constexpr u32 GPM_OFF_NAME          = 0x08;
		constexpr u32 GPM_NAME_LEN          = 16;
		constexpr u32 GPM_OFF_LOD_COUNT     = 0x1C;
		constexpr u32 GPM_OFF_VERT_COUNT    = 0x88;
		constexpr u32 GPM_OFF_NUM_CTRL_REGS = 0xB0;

		// CtrlReg defs (hardpoints)
		constexpr u32   CTRL_REG_ENTRY_SIZE = 48;
		constexpr u32   CTRL_REG_OFF_POS_X  = 0x00;
		constexpr u32   CTRL_REG_OFF_POS_Y  = 0x04;
		constexpr u32   CTRL_REG_OFF_POS_Z  = 0x08;
		constexpr u32   CTRL_REG_OFF_DIR_X  = 0x0C;
		constexpr u32   CTRL_REG_OFF_DIR_Y  = 0x10;
		constexpr u32   CTRL_REG_OFF_DIR_Z  = 0x14;
		constexpr u32   CTRL_REG_OFF_FLAGS  = 0x18;
		constexpr u32   CTRL_REG_OFF_TYPE   = 0x1C;
		constexpr u32   CTRL_REG_OFF_NAME   = 0x20;
		constexpr u32   CTRL_REG_NAME_LEN   = 8;
		constexpr float CTRL_REG_POS_SCALE  = 65536.0f;

		// Texture table
		constexpr u32 TEX_ENTRY_SIZE = 60;
		constexpr u32 TEX_OFF_TEX_ID = 0x24;
		constexpr u32 TEX_OFF_FLAGS  = 0x26;

		// Collision data (CDTA)
		constexpr u32 CDTA_HEADER_SIZE          = 136;
		constexpr u32 CDTA_OFF_DATA_SIZE        = 0x04;
		constexpr u32 CDTA_OFF_VERT_COUNT       = 0x30;
		constexpr u32 CDTA_OFF_NORM_COUNT       = 0x38;
		constexpr u32 CDTA_OFF_FACE_COUNT       = 0x40;
		constexpr u32 CDTA_OFF_MESH_COUNT       = 0x48;
		constexpr u32 CDTA_OFF_FACE_COLOR_COUNT = 0x50;
		constexpr u32 CDTA_OFF_PLANE_COUNT      = 0x58;
		constexpr u32 CDTA_OFF_VOLUME_COUNT     = 0x60;
		constexpr u32 CDTA_MESH_ENTRY_SIZE      = 128;
		constexpr u32 CDTA_FACECOLOR_ENTRY_SIZE = 12;

		// Collision verts / normals / faces
		constexpr u32   CVRT_ENTRY_SIZE = 8;
		constexpr u32   CNRM_ENTRY_SIZE = 8;
		constexpr u32   CFAC_ENTRY_SIZE = 44;
		constexpr float CVRT_INT_SCALE  = 256.0f;
		constexpr float CNRM_INT_SCALE  = 16384.0f;

		// Collision planes (BPLN)
		constexpr u32   BPLN_ENTRY_SIZE   = 16;
		constexpr u32   BPLN_OFF_NORMAL_X = 0x00;
		constexpr u32   BPLN_OFF_NORMAL_Y = 0x04;
		constexpr u32   BPLN_OFF_NORMAL_Z = 0x08;
		constexpr u32   BPLN_OFF_D        = 0x0C;
		constexpr float BPLN_NORMAL_SCALE = 65536.0f;
		constexpr float BPLN_D_SCALE      = 65536.0f;

		// Collision volumes (BVOL)
		constexpr u32   BVOL_ENTRY_SIZE      = 96;
		constexpr u32   BVOL_OFF_TYPE        = 0x04;
		constexpr u32   BVOL_OFF_BBOX_X_MIN  = 0x30;
		constexpr u32   BVOL_OFF_BBOX_X_MAX  = 0x34;
		constexpr u32   BVOL_OFF_BBOX_Y_MIN  = 0x38;
		constexpr u32   BVOL_OFF_BBOX_Y_MAX  = 0x3C;
		constexpr u32   BVOL_OFF_BBOX_Z_MIN  = 0x40;
		constexpr u32   BVOL_OFF_BBOX_Z_MAX  = 0x44;
		constexpr u32   BVOL_OFF_PLANE_COUNT = 0x48;
		constexpr float BVOL_SCALE           = 65536.0f;

		// Render data (RDTA)
		constexpr u32 RDTA_HEADER_SIZE               = 136;
		constexpr u32 RDTA_OFF_DATA_SIZE             = 0x00;
		constexpr u32 RDTA_OFF_MAT_COUNT             = 0x08;
		constexpr u32 RDTA_OFF_MESH_COUNT            = 0x10;
		constexpr u32 RDTA_OFF_BOUNDING_TRIPLE_COUNT = 0x18;

		constexpr u32 RDTA_BOUNDING_TRIPLE_ENTRY_SIZE = 12;

		constexpr u32 RDTA_MESH_ENTRY_SIZE      = 72;
		constexpr u32 RDTA_MESH_OFF_STRIP_COUNT = 0x04;

		constexpr u32 RDTA_SUBMESH_ENTRY_SIZE        = 32;
		constexpr u32 RDTA_SUBMESH_OFF_STRIP_COUNT_A = 0x04;
		constexpr u32 RDTA_SUBMESH_OFF_STRIP_COUNT_B = 0x0C;

		constexpr u32 SKINNED_BONE_BLOCK_SIZE      = 28;
		constexpr u32 SKINNED_BONE_BLOCK_OFF_TOTAL = 0x04;

		// Render-strip header + indices
		constexpr u32 STRP_STATIC_HEADER_SIZE  = 40;
		constexpr u32 STRP_SKINNED_HEADER_SIZE = 48;
		constexpr u32 STRP_OFF_MAT             = 0x00;
		constexpr u32 STRP_OFF_IDX_COUNT       = 0x08;
		constexpr u32 STRP_OFF_FLAG            = 0x0C;
		constexpr u32 STRP_OFF_VERT_START      = 0x10;

		// Material entries (under RDTA data)
		constexpr u32 MTRL_ENTRY_SIZE       = 152;
		constexpr u32 MTRL_OFF_NAME         = 0x00;
		constexpr u32 MTRL_NAME_LEN         = 36;
		constexpr u32 MTRL_OFF_FLAGS        = 0x10;
		constexpr u32 MTRL_OFF_FRAME_COUNT  = 0x16;
		constexpr u32 MTRL_OFF_ENV_REFS     = 0x28;
		constexpr u32 MTRL_OFF_SHADER_TYPE  = 0x32;
		constexpr u32 MTRL_OFF_ENV_TAG_0    = 0x38;
		constexpr u32 MTRL_OFF_ENV_TAG_1    = 0x39;
		constexpr u32 MTRL_OFF_ALPHA_REF    = 0x3B;
		constexpr u32 MTRL_OFF_BLEND_MODE   = 0x3C;

		// Vertex
		constexpr u32 VERT_OFF_POS    = 0x00;
		constexpr u32 VERT_OFF_NORMAL = 0x0C;

		struct Texture {
			std::string name;
			i16         tex_id    = 0;
			u16         tex_flags = 0;
		};

		struct Material {
			std::string name;
			u32         flags        = 0;
			u8          frame_count  = 0;
			u8          shader_type  = 0;
			u8          env_tag_0    = 0;
			u8          env_tag_1    = 0;
			u8          alpha_cutoff = 128;
			u8          blend_mode   = 0;   // 0=opaque, 1=alpha-test class, 2=alpha-blend, 3=additive, 4=modulate, 5=multiply
			i8          env_refs[4]  = {};
		};

		struct Triangle {
			u32 vi[3];
			u32 mat_idx;
			u32 group_idx;
		};

		struct Plane {
			float nx;
			float ny;
			float nz;
			float d;
		};

		struct CollisionVolume {
			u8                   type;
			std::array<float, 6> bbox; // {xmin, ymin, zmin, xmax, ymax, zmax}
			u32                  plane_start;
			u32                  plane_count;
		};

		struct CollisionFace {
			u32 vi[3];
		};

		struct CollisionMesh {
			std::vector<std::array<float, 3>> positions;
			std::vector<std::array<float, 3>> normals;
			std::vector<CollisionFace>        faces;
		};

		struct Hardpoint {
			std::string name;
			i32         pos[3] = {};
			i32         dir[3] = {};
			u32         flags  = 0;
			u8          type   = 0;
		};

		struct ParsedGpm {

			// Metadata
			std::string model_name;
			u8          version[3];      // major.minor.patch
			char        subtype;         // 'M', 'S', or 'P'
			bool        skinned = false; // derived: subtype 'S' (1-bone) or 'P' (4-bone)

			// LOD
			u32         lod_count  = 0; // total LODs (1..4)
			u32         lod_picked = 0; // LOD index actually decoded (0=highest)

			// Materials + textures + binding
			std::vector<Material> materials;
			std::vector<Texture>  textures;
			std::vector<int>      mat_to_tex_idx;
			std::vector<Triangle> triangles;

			// Vertex
			const u8* vert_data   = nullptr;
			u32       vert_count  = 0;
			u32       vert_stride = 0;
			u32       uv_offset   = 0;

			// Mesh
			u32       mesh_count  = 0;

			// Collision
			CollisionMesh                collision;
			std::vector<Plane>           planes;
			std::vector<CollisionVolume> volumes;

			// Hardpoints
			std::vector<Hardpoint> hardpoints;
		};

		//========================================================================
		// Parse Textures
		//========================================================================
		bool parse_textures(const u8* data, size_t& pos, size_t file_size, ParsedGpm& pf) {

			if(pos + 4 > file_size) {
				nlog::error("unexpected EOF reading texture count");
				return false;
			}

			u32 tex_count = io::read_u32(data + pos);
			pos += 4;

			pf.textures.resize(tex_count);
			for(u32 i = 0; i < tex_count; ++i) {

				if(pos + TEX_ENTRY_SIZE > file_size) {
					nlog::error("unexpected EOF in texture %u", i);
					return false;
				}

				const u8* entry = data + pos;
				pf.textures[i].name      = io::sanitize_name((const char*)entry, TEX_OFF_TEX_ID);
				pf.textures[i].tex_id    = (i16)io::read_u16(entry + TEX_OFF_TEX_ID);
				pf.textures[i].tex_flags = io::read_u16(entry + TEX_OFF_FLAGS);
				pos += TEX_ENTRY_SIZE;
			}

			return true;
		}

		//========================================================================
		// Materials
		//========================================================================
		void parse_materials(const u8* rdta_data, size_t material_table_offset, u32 count, ParsedGpm& pf) {

			pf.materials.resize(count);

			for(u32 i = 0; i < count; ++i) {

				const u8* entry = rdta_data + material_table_offset + i * MTRL_ENTRY_SIZE;
				auto& mat = pf.materials[i];

				mat.name         = io::sanitize_name((const char*)entry + MTRL_OFF_NAME, MTRL_NAME_LEN);
				mat.flags        = io::read_u32(entry + MTRL_OFF_FLAGS);
				mat.frame_count  = entry[MTRL_OFF_FRAME_COUNT];
				mat.shader_type  = entry[MTRL_OFF_SHADER_TYPE];
				mat.env_tag_0    = entry[MTRL_OFF_ENV_TAG_0];
				mat.env_tag_1    = entry[MTRL_OFF_ENV_TAG_1];
				mat.alpha_cutoff = entry[MTRL_OFF_ALPHA_REF];
				mat.blend_mode   = entry[MTRL_OFF_BLEND_MODE];
				mat.env_refs[0]  = (i8)entry[MTRL_OFF_ENV_REFS + 0];
				mat.env_refs[1]  = (i8)entry[MTRL_OFF_ENV_REFS + 1];
				mat.env_refs[2]  = (i8)entry[MTRL_OFF_ENV_REFS + 2];
				mat.env_refs[3]  = (i8)entry[MTRL_OFF_ENV_REFS + 3];
			}
		}

		//========================================================================
		// Vertices (RVert)
		//========================================================================
		bool parse_vertices(const u8* data, size_t& pos, size_t file_size, ParsedGpm& pf) {

			pf.vert_count  = io::read_u32(data + GPM_OFF_VERT_COUNT);
			pf.vert_stride = (pf.subtype == 'M') ? 44 : (pf.subtype == 'S') ? 48 : 60;
			pf.uv_offset   = (pf.subtype == 'P') ? 44 : (pf.subtype == 'S') ? 32 : 28;

			if(pos + (size_t)pf.vert_count * pf.vert_stride > file_size) {
				nlog::error("unexpected EOF in vertex data");
				return false;
			}

			pf.vert_data = data + pos;
			pos += pf.vert_count * pf.vert_stride;

			return true;
		}

		//========================================================================
		// Decode a single strip header
		//========================================================================
		void decode_strip(
			const u8* strip_entry,
			const u8* indices,
			u32 group_idx,
			const ParsedGpm& pf,
			std::vector<Triangle>& tris
		) {

			u32 mat_idx    = io::read_u32(strip_entry + STRP_OFF_MAT);
			u16 idx_count  = io::read_u16(strip_entry + STRP_OFF_IDX_COUNT);
			u32 strip_flag = io::read_u32(strip_entry + STRP_OFF_FLAG);
			u32 vtx_start  = io::read_u32(strip_entry + STRP_OFF_VERT_START);

			const bool is_list = pf.skinned || strip_flag == 0;
			const int  step    = is_list ? 3 : 1;

			for(int i = 0; i + 2 < (int)idx_count; i += step) {

				u16 idx_a = io::read_u16(indices + i * 2);
				u16 idx_b = io::read_u16(indices + (i + 1) * 2);
				u16 idx_c = io::read_u16(indices + (i + 2) * 2);

				if(idx_a == idx_b || idx_b == idx_c || idx_a == idx_c)
					continue;

				const bool flip = !is_list && (i & 1);

				Triangle tri;
				tri.mat_idx   = mat_idx;
				tri.group_idx = group_idx;

				if(flip) {
					tri.vi[0] = vtx_start + idx_a;
					tri.vi[1] = vtx_start + idx_c;
					tri.vi[2] = vtx_start + idx_b;
				} else {
					tri.vi[0] = vtx_start + idx_a;
					tri.vi[1] = vtx_start + idx_b;
					tri.vi[2] = vtx_start + idx_c;
				}

				tris.push_back(tri);
			}

		}

		//========================================================================
		// Decode all strips
		//========================================================================
		size_t decode_strips(
			const u8* rdta_data,
			size_t mesh_offset,
			ParsedGpm& pf
		) {

			size_t read_pos = mesh_offset;
			u32 strip_hdr_size = pf.skinned ? STRP_SKINNED_HEADER_SIZE : STRP_STATIC_HEADER_SIZE;

			size_t mesh_hdr_start = read_pos;
			read_pos += pf.mesh_count * RDTA_MESH_ENTRY_SIZE;

			std::vector<u32> submesh_to_group;
			if(pf.skinned) {

				u32 submesh_count = io::read_u32(rdta_data + read_pos + SKINNED_BONE_BLOCK_OFF_TOTAL);
				read_pos += SKINNED_BONE_BLOCK_SIZE;
				submesh_to_group.assign(submesh_count, 0);

			} else {
				size_t mesh_read_pos = mesh_hdr_start;

				for(u32 group_idx = 0; group_idx < pf.mesh_count; ++group_idx) {

					u32 submesh_count = io::read_u32(rdta_data + mesh_read_pos + RDTA_MESH_OFF_STRIP_COUNT);
					
					for(u32 i = 0; i < submesh_count; ++i)
						submesh_to_group.push_back(group_idx);

					mesh_read_pos += RDTA_MESH_ENTRY_SIZE;
				}

			}

			std::vector<u32> strip_to_group;
			for(u32 submesh_idx = 0; submesh_idx < submesh_to_group.size(); ++submesh_idx) {
				u32 strip_count = io::read_u32(rdta_data + read_pos + RDTA_SUBMESH_OFF_STRIP_COUNT_A)
				                + io::read_u32(rdta_data + read_pos + RDTA_SUBMESH_OFF_STRIP_COUNT_B);

				for(u32 i = 0; i < strip_count; ++i)
					strip_to_group.push_back(submesh_to_group[submesh_idx]);

				read_pos += RDTA_SUBMESH_ENTRY_SIZE;
			}

			pf.triangles.reserve(strip_to_group.size() * 100);

			for(u32 strip_i = 0; strip_i < strip_to_group.size(); ++strip_i) {
				const u8* strip_entry = rdta_data + read_pos;
				const u8* indices     = strip_entry + strip_hdr_size;
				u16 idx_count         = io::read_u16(strip_entry + STRP_OFF_IDX_COUNT);

				decode_strip(strip_entry, indices, strip_to_group[strip_i], pf, pf.triangles);
				read_pos += strip_hdr_size + idx_count * 2;
			}

			return read_pos;
		}

		//========================================================================
		// Find the Nth LOD by walking the RDTA block
		//========================================================================
		bool find_nth_lod(const u8* data, size_t& pos, size_t file_size, u32 lod_idx) {

			for(u32 i = 0; i < lod_idx; ++i) {

				if(pos + RDTA_HEADER_SIZE > file_size)
					return false;

				u32 skip_size = io::read_u32(data + pos + RDTA_OFF_DATA_SIZE);
				pos += RDTA_HEADER_SIZE + skip_size;
			}

			return true;
		}

		//========================================================================
		// Pick the user-selected RDTA LOD and decode its strips + materials.
		// Out-of-range falls back to lowest-detail LOD.
		//========================================================================
		bool parse_lod(const u8* data, size_t& pos, size_t file_size, ParsedGpm& pf, int lod) {

			u32 find_lod = (u32)lod;

			if(find_lod >= pf.lod_count) {
				nlog::warn("--lod=%u not found (model has %u LODs), using default LOD 0", find_lod, pf.lod_count);
				find_lod = 0;
			}

			pf.lod_picked = find_lod;

			if(!find_nth_lod(data, pos, file_size, find_lod)) {
				nlog::error("LOD %u not found", find_lod);
				return false;
			}

			if(pos + RDTA_HEADER_SIZE > file_size) {
				nlog::error("unexpected EOF in RDTA (LOD %u)", find_lod);
				return false;
			}

			const u8* rdta_hdr = data + pos;
			u32 rdta_size = io::read_u32(rdta_hdr + RDTA_OFF_DATA_SIZE);
			u32 mat_count      = io::read_u32(rdta_hdr + RDTA_OFF_MAT_COUNT);
			u32 mesh_count     = io::read_u32(rdta_hdr + RDTA_OFF_MESH_COUNT);
			u32 bt_count       = io::read_u32(rdta_hdr + RDTA_OFF_BOUNDING_TRIPLE_COUNT);
			pos += RDTA_HEADER_SIZE;

			if(pos + rdta_size > file_size) {
				nlog::error("unexpected EOF in RDTA (LOD %u)", find_lod);
				return false;
			}

			pf.mesh_count = mesh_count;

			const u8* rdta_data = data + pos;
			size_t mesh_offset = bt_count * RDTA_BOUNDING_TRIPLE_ENTRY_SIZE;
			size_t material_table_offset = decode_strips(rdta_data, mesh_offset, pf);

			parse_materials(rdta_data, material_table_offset, mat_count, pf);

			pos += rdta_size;
			return true;
		}

		//========================================================================
		// Collision (CDTA)
		//========================================================================
		bool parse_collision(const u8* data, size_t& pos, size_t file_size, ParsedGpm& pf) {

			if(pos + CDTA_HEADER_SIZE > file_size) {
				nlog::error("unexpected EOF in CDTA header");
				return false;
			}

			const u8* cdta_hdr = data + pos;
			u32 cdta_data_size        = io::read_u32(cdta_hdr + CDTA_OFF_DATA_SIZE);
			u32 vert_count       = io::read_u32(cdta_hdr + CDTA_OFF_VERT_COUNT);
			u32 norm_count       = io::read_u32(cdta_hdr + CDTA_OFF_NORM_COUNT);
			u32 face_count       = io::read_u32(cdta_hdr + CDTA_OFF_FACE_COUNT);
			u32 mesh_count       = io::read_u32(cdta_hdr + CDTA_OFF_MESH_COUNT);
			u32 face_color_count = io::read_u32(cdta_hdr + CDTA_OFF_FACE_COLOR_COUNT);
			u32 plane_count      = io::read_u32(cdta_hdr + CDTA_OFF_PLANE_COUNT);
			u32 volume_count     = io::read_u32(cdta_hdr + CDTA_OFF_VOLUME_COUNT);
			pos += CDTA_HEADER_SIZE;

			u32 expected_size =
				  vert_count       * CVRT_ENTRY_SIZE
				+ norm_count       * CNRM_ENTRY_SIZE
				+ face_count       * CFAC_ENTRY_SIZE
				+ mesh_count       * CDTA_MESH_ENTRY_SIZE
				+ face_color_count * CDTA_FACECOLOR_ENTRY_SIZE
				+ plane_count      * BPLN_ENTRY_SIZE
				+ volume_count     * BVOL_ENTRY_SIZE;

			if(expected_size != cdta_data_size) {
				nlog::warn("CDTA data size field says %u, computed %u -- using computed", cdta_data_size, expected_size);
				cdta_data_size = expected_size;
			}

			if(pos + cdta_data_size > file_size) {
				nlog::error("unexpected EOF in CDTA data");
				return false;
			}

			const u8* cdta_data = data + pos;
			pf.collision.positions.reserve(vert_count);

			for(u32 i = 0; i < vert_count; ++i) {
				const u8* vert_ptr = cdta_data + i * CVRT_ENTRY_SIZE;
				i16 x = (i16)io::read_u16(vert_ptr);
				i16 y = (i16)io::read_u16(vert_ptr + 2);
				i16 z = (i16)io::read_u16(vert_ptr + 4);
				pf.collision.positions.push_back({
					x / CVRT_INT_SCALE,
					y / CVRT_INT_SCALE,
					z / CVRT_INT_SCALE,
				});
			}

			const u8* norm_data = cdta_data + vert_count * CVRT_ENTRY_SIZE;
			pf.collision.normals.reserve(norm_count);

			for(u32 i = 0; i < norm_count; ++i) {
				const u8* norm_ptr = norm_data + i * CNRM_ENTRY_SIZE;
				i16 nx = (i16)io::read_u16(norm_ptr);
				i16 ny = (i16)io::read_u16(norm_ptr + 2);
				i16 nz = (i16)io::read_u16(norm_ptr + 4);
				pf.collision.normals.push_back({
					nx / CNRM_INT_SCALE,
					ny / CNRM_INT_SCALE,
					nz / CNRM_INT_SCALE,
				});
			}

			const u8* face_data = norm_data + norm_count * CNRM_ENTRY_SIZE;
			pf.collision.faces.reserve(face_count);

			for(u32 i = 0; i < face_count; ++i) {
				const u8* face_ptr = face_data + i * CFAC_ENTRY_SIZE;
				u16 idx_a = io::read_u16(face_ptr);
				u16 idx_b = io::read_u16(face_ptr + 2);
				u16 idx_c = io::read_u16(face_ptr + 4);

				if(idx_a == idx_b || idx_b == idx_c || idx_a == idx_c)
					continue;

				const u8* vert_a = cdta_data + idx_a * CVRT_ENTRY_SIZE;
				const u8* vert_b = cdta_data + idx_b * CVRT_ENTRY_SIZE;
				const u8* vert_c = cdta_data + idx_c * CVRT_ENTRY_SIZE;

				if(memcmp(vert_a, vert_b, 6) == 0 ||
				   memcmp(vert_b, vert_c, 6) == 0 ||
				   memcmp(vert_a, vert_c, 6) == 0)
					continue;

				pf.collision.faces.push_back({{idx_a, idx_b, idx_c}});
			}

			size_t plane_offset =
				  vert_count       * CVRT_ENTRY_SIZE
				+ norm_count       * CNRM_ENTRY_SIZE
				+ face_count       * CFAC_ENTRY_SIZE
				+ mesh_count       * CDTA_MESH_ENTRY_SIZE
				+ face_color_count * CDTA_FACECOLOR_ENTRY_SIZE;

			const u8* plane_data = cdta_data + plane_offset;
			pf.planes.resize(plane_count);

			for(u32 i = 0; i < plane_count; ++i) {
				const u8* entry = plane_data + i * BPLN_ENTRY_SIZE;
				auto& pl = pf.planes[i];

				pl.nx = -(i32)io::read_u32(entry + BPLN_OFF_NORMAL_X) / BPLN_NORMAL_SCALE;
				pl.ny = -(i32)io::read_u32(entry + BPLN_OFF_NORMAL_Y) / BPLN_NORMAL_SCALE;
				pl.nz = -(i32)io::read_u32(entry + BPLN_OFF_NORMAL_Z) / BPLN_NORMAL_SCALE;
				pl.d  =  (i32)io::read_u32(entry + BPLN_OFF_D)        / BPLN_D_SCALE;
			}

			const u8* volume_data = plane_data + plane_count * BPLN_ENTRY_SIZE;
			pf.volumes.resize(volume_count);
			u32 plane_cursor = 0;

			for(u32 i = 0; i < volume_count; ++i) {
				const u8* entry = volume_data + i * BVOL_ENTRY_SIZE;
				auto& vol = pf.volumes[i];

				vol.type        = (u8)io::read_u32(entry + BVOL_OFF_TYPE);
				vol.plane_count = io::read_u32(entry + BVOL_OFF_PLANE_COUNT);
				vol.plane_start = plane_cursor;

				i32 x_min = (i32)io::read_u32(entry + BVOL_OFF_BBOX_X_MIN);
				i32 x_max = (i32)io::read_u32(entry + BVOL_OFF_BBOX_X_MAX);
				i32 y_min = (i32)io::read_u32(entry + BVOL_OFF_BBOX_Y_MIN);
				i32 y_max = (i32)io::read_u32(entry + BVOL_OFF_BBOX_Y_MAX);
				i32 z_min = (i32)io::read_u32(entry + BVOL_OFF_BBOX_Z_MIN);
				i32 z_max = (i32)io::read_u32(entry + BVOL_OFF_BBOX_Z_MAX);

				vol.bbox = {
					x_min / BVOL_SCALE, y_min / BVOL_SCALE, z_min / BVOL_SCALE,
					x_max / BVOL_SCALE, y_max / BVOL_SCALE, z_max / BVOL_SCALE,
				};

				plane_cursor += vol.plane_count;
			}

			pos += cdta_data_size;
			return true;
		}

		//========================================================================
		// Read CtrlReg defs for hardpoints
		//========================================================================
		bool parse_hardpoints(const u8* data, size_t& pos, size_t file_size, u32 count, ParsedGpm& pf) {

			if(pos + (size_t)count * CTRL_REG_ENTRY_SIZE > file_size) {
				nlog::error("unexpected EOF in CtrlReg defs (%u entries)", count);
				return false;
			}

			pf.hardpoints.resize(count);

			for(u32 i = 0; i < count; ++i) {

				const u8* entry = data + pos + i * CTRL_REG_ENTRY_SIZE;
				auto& hp = pf.hardpoints[i];

				hp.pos[0] = (i32)io::read_u32(entry + CTRL_REG_OFF_POS_X);
				hp.pos[1] = (i32)io::read_u32(entry + CTRL_REG_OFF_POS_Y);
				hp.pos[2] = (i32)io::read_u32(entry + CTRL_REG_OFF_POS_Z);
				hp.dir[0] = (i32)io::read_u32(entry + CTRL_REG_OFF_DIR_X);
				hp.dir[1] = (i32)io::read_u32(entry + CTRL_REG_OFF_DIR_Y);
				hp.dir[2] = (i32)io::read_u32(entry + CTRL_REG_OFF_DIR_Z);
				hp.flags  = io::read_u32(entry + CTRL_REG_OFF_FLAGS);
				hp.type   = (u8)io::read_u32(entry + CTRL_REG_OFF_TYPE);

				char name_buf[CTRL_REG_NAME_LEN + 1] = {};
				memcpy(name_buf, entry + CTRL_REG_OFF_NAME, CTRL_REG_NAME_LEN);
				hp.name = name_buf;
			}

			pos += (size_t)count * CTRL_REG_ENTRY_SIZE;
			return true;
		}

		//========================================================================
		// Resolve material > texture binding using the env_refs > tex_id chain
		//========================================================================
		void resolve_env(ParsedGpm& pf, int env_id) {

			if(env_id < 0 || env_id > 3)
				env_id = 0;

			pf.mat_to_tex_idx.assign(pf.materials.size(), -1);

			for(size_t m = 0; m < pf.materials.size(); ++m) {

				auto& mat = pf.materials[m];
				int tex_id = -1;

				if(mat.env_tag_0 == 1 || mat.env_tag_0 == 2)
					tex_id = mat.env_refs[env_id];
				else if(mat.env_tag_1 == 2)
					tex_id = mat.env_refs[3];

				if(tex_id >= 0) {

					for(size_t t = 0; t < pf.textures.size(); ++t) {

						if(pf.textures[t].tex_id == tex_id) {
							pf.mat_to_tex_idx[m] = (int)t;
							break;
						}

					}
				}

				if(mat.name.empty())
					mat.name = "mat_" + std::to_string(m);

			}
		}

		//========================================================================
		// Use alpha channel as opacity if
		//   mat.flags & 0x06 != 0  (D3DRS_ALPHATESTENABLE  = cutout)
		//   blend_mode == 2        (D3DRS_ALPHABLENDENABLE = translucent)
		//   blend_mode == 3        (additive if closest OBJ analog is translucent)
		//========================================================================
		bool alpha_as_opacity(const Material& mat) {

			if(mat.flags & 0x06)
				return true;

			if(mat.blend_mode == 2 || mat.blend_mode == 3)
				return true;

			return false;
		}

		//========================================================================
		// Parse GPM
		//========================================================================
		bool parse_gpm(std::span<const u8> bytes, ParsedGpm& pf, int env_id, int lod) {

			const u8* data = bytes.data();
			size_t file_size = bytes.size();

			if(file_size < GPM_HEADER_SIZE + 4) {
				nlog::error("GPM file too small");
				return false;
			}

			pf.subtype = (char)data[GPM_OFF_SUBTYPE];
			pf.skinned = (pf.subtype == 'S' || pf.subtype == 'P');

			if(data[GPM_OFF_VERSION_BYTES] != 2) {
				nlog::error("unsupported GPM version %d", data[GPM_OFF_VERSION_BYTES]);
				return false;
			}

			pf.version[0] = data[GPM_OFF_VERSION_BYTES];
			pf.version[1] = data[GPM_OFF_VERSION_BYTES + 1];
			pf.version[2] = data[GPM_OFF_VERSION_BYTES + 2];

			pf.model_name = io::sanitize_name((const char*)data + GPM_OFF_NAME, GPM_NAME_LEN);

			if(pf.model_name.empty())
				pf.model_name = "model";

			pf.lod_count = io::read_u32(data + GPM_OFF_LOD_COUNT);

			if(pf.lod_count == 0 || pf.lod_count > 4) {
				nlog::error("unsupported lod_count %u (expected 1..4)", pf.lod_count);
				return false;
			}

			u32 ctrl_reg_count = io::read_u32(data + GPM_OFF_NUM_CTRL_REGS);
			size_t pos = GPM_HEADER_SIZE;

			if(!parse_hardpoints(data, pos, file_size, ctrl_reg_count, pf))
				return false;

			if(!parse_textures(data, pos, file_size, pf))
				return false;

			if(!parse_collision(data, pos, file_size, pf))
				return false;

			if(!parse_vertices(data, pos, file_size, pf))
				return false;

			if(!parse_lod(data, pos, file_size, pf, lod))
				return false;

			resolve_env(pf, env_id);
			return true;
		}

		//========================================================================
		// Texture exclusions
		//========================================================================
		model::TextureExclusions collect_texture_exclusions(
			const ParsedGpm& pf,
			filters::Scope scope,
			const model::ConvertOptions& opts
		) {

			model::TextureExclusions tex_ex;
			tex_ex.drop_texture.assign(pf.textures.size(), 0);

			if(opts.raw || opts.effects)
				return tex_ex;

			for(size_t i = 0; i < pf.textures.size(); ++i) {

				if(filters::is_effect(scope, pf.textures[i].name) ||
				   filters::is_shadow(scope, pf.textures[i].name))
					tex_ex.drop_texture[i] = 1;
					
			}

			return tex_ex;
		}

		//========================================================================
		// Geometry exclusions - drop triangles whose material binds to an excluded texture
		//========================================================================
		std::vector<u8> collect_geometry_exclusions(
			const ParsedGpm& pf,
			size_t /*lod_idx*/,
			const model::TextureExclusions& tex_ex,
			filters::Scope,
			const model::ConvertOptions& opts
		) {

			std::vector<u8> drop(pf.triangles.size(), 0);

			if(opts.raw)
				return drop;

			for(size_t t = 0; t < pf.triangles.size(); ++t) {

				u32 mat_idx = pf.triangles[t].mat_idx;
				if(mat_idx >= pf.materials.size())
					continue;

				int tex_idx = pf.mat_to_tex_idx[mat_idx];
				if(tex_idx >= 0 && (size_t)tex_idx < tex_ex.drop_texture.size() && tex_ex.drop_texture[tex_idx])
					drop[t] = 1;

			}

			return drop;
		}

		//========================================================================
		// Map shader_type to a metadata tag
		//========================================================================
		const char* shader_meta(u8 shader_type) {
			switch(shader_type) {
				case 4:  return "bump_specular";
				case 7:  return "env_sphere";
				case 8:  return "bump_map";
				case 11: return "detail_blend";
				case 12: return "detail_blend2";
				case 15: return "bump_alt";
				case 16: return "env_detail";
				default: return nullptr;
			}
		}

		//========================================================================
		// Map blend_mode to a metadata tag
		//========================================================================
		const char* blend_mode_meta(u8 blend_mode) {
			switch(blend_mode) {
				case 1: return "alpha_test";
				case 2: return "alpha_blend";
				case 3: return "additive";
				case 4: return "modulate";
				case 5: return "multiply";
				default: return nullptr;
			}
		}

		//========================================================================
		// Texture specs
		//========================================================================
		std::vector<texture::TextureSpec> texture_specs(
			const ParsedGpm& pf,
			const model::TextureExclusions&,
			filters::Scope,
			const model::ConvertOptions&
		) {

			std::vector<texture::TextureSpec> specs(pf.textures.size());

			for(auto& spec : specs) {
				spec.mode = texture::AlphaMode::Opaque;
				spec.rgb  = texture::RgbSource::Palette;
			}

			for(size_t m = 0; m < pf.materials.size(); ++m) {

				if(!alpha_as_opacity(pf.materials[m]))
					continue;

				int t = pf.mat_to_tex_idx[m];

				if(t < 0 || (size_t)t >= specs.size())
					continue;

				specs[t].mode = texture::AlphaMode::Explicit;

				const auto& mat = pf.materials[m];

				if(mat.flags & 0x06) {
					specs[t].alpha_cutoff        = mat.alpha_cutoff;
					specs[t].alpha_cutoff_invert = (mat.flags & 0x04) != 0;
				}
			}

			std::map<std::string, std::vector<size_t>> entries_by_filename;

			for(size_t i = 0; i < pf.textures.size(); ++i)
				entries_by_filename[io::to_lower(pf.textures[i].name)].push_back(i);

			for(auto& [_, group] : entries_by_filename) {

				bool any_alpha = std::any_of(group.begin(), group.end(), [&](size_t i) {
					return specs[i].mode != texture::AlphaMode::Opaque;
				});

				if(!any_alpha)
					continue;

				for(size_t i : group)
					specs[i].mode = texture::AlphaMode::Explicit;
			}

			return specs;
		}

		//========================================================================
		// Find filenames bound by both opaque and alpha materials
		//========================================================================
		std::set<std::string> resolve_mixed_textures(const ParsedGpm& pf) {

			std::map<std::string, std::pair<bool, bool>> seen;  // (opaque, alpha)

			for(size_t m = 0; m < pf.materials.size(); ++m) {
				int t = pf.mat_to_tex_idx[m];

				if(t < 0 || (size_t)t >= pf.textures.size())
					continue;

				std::string key = io::to_lower(pf.textures[t].name);
				auto& [op, al] = seen[key];

				if(alpha_as_opacity(pf.materials[m]))
					al = true;
				else
					op = true;
			}

			std::set<std::string> mixed;
			for(const auto& [key, modes] : seen) {

				if(modes.first && modes.second)
					mixed.insert(key);
			}

			return mixed;
		}

		//========================================================================
		// Build materials
		//========================================================================
		void build_materials(
			const ParsedGpm& pf,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			std::vector<std::string> unique_mat_name(pf.materials.size());
			std::map<std::string, int> name_count;

			for(size_t m = 0; m < pf.materials.size(); ++m) {

				std::string base = io::strip_ext(pf.materials[m].name);

				if(base.empty())
					base = "mat_" + std::to_string(m);

				int idx = name_count[base]++;
				unique_mat_name[m] = (idx == 0) ? base : (base + "_" + std::to_string(idx));
			}

			model.materials.reserve(pf.materials.size());

			for(size_t i = 0; i < pf.materials.size(); ++i) {

				const auto& src = pf.materials[i];

				model::Material mat;
				mat.name = unique_mat_name[i];
				mat.color[0] = mat.color[1] = mat.color[2] = 0.8f;

				int tex_idx = pf.mat_to_tex_idx[i];

				if(tex_idx >= 0 && (size_t)tex_idx < ctx.textures.filenames.size()) {

					std::string filename = ctx.textures.filenames[tex_idx];

					if(!filename.empty() && !alpha_as_opacity(src)) {
						std::string opaque_name = io::strip_ext(filename) + "_opaque" + io::extension(filename);
						std::string opaque_path = std::string(ctx.opts.out_dir) + "\\" + opaque_name;
						std::error_code ec;

						if(std::filesystem::exists(opaque_path, ec))
							filename = opaque_name;

					}

					if(!filename.empty()) {
						mat.texture = filename;

						if(io::extension(filename) == ".tga" && alpha_as_opacity(src))
							mat.alpha = filename;
					}

					if((size_t)tex_idx < pf.textures.size() && (pf.textures[tex_idx].tex_flags & 0x100)) {
						std::string base = io::strip_ext(pf.textures[tex_idx].name);

						if(!base.empty())
							mat.metadata["mdt_bump"] = base + ".mdt";
					}
				}

				// nudge opacity below 1.0 so f3d shows alpha
				if(!mat.texture.empty() && (src.blend_mode == 2 || src.blend_mode == 3))
					mat.opacity = 0.99f;

				if(const char* tag = blend_mode_meta(src.blend_mode))
					mat.metadata["blend_mode"] = tag;

				if(src.env_tag_1 == 2)
					mat.metadata["double_sided"] = "1";

				if(const char* tag = shader_meta(src.shader_type))
					mat.metadata["shader"] = tag;

				if(src.flags & 0x200)
					mat.metadata["brightness_alpha"] = "1";

				model.materials.push_back(mat);
			}

		}

		//========================================================================
		// Build one mesh per group_idx in the triangle list
		//========================================================================
		void build_meshes(
			const ParsedGpm& pf,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			u32 group_count = pf.mesh_count;

			const u32 norm_off = VERT_OFF_NORMAL;
			const u32 uv_off   = pf.uv_offset;

			for(u32 group_idx = 0; group_idx < group_count; ++group_idx) {

				model::Mesh mesh;
				mesh.group_name = "mesh_" + std::to_string(group_idx);

				std::map<u32, u32> v_to_local;

				auto add_vertex = [&](u32 src_idx) -> u32 {

					auto it = v_to_local.find(src_idx);

					if(it != v_to_local.end())
						return it->second;

					const u8* vert_ptr = pf.vert_data + src_idx * pf.vert_stride;
					float px, py, pz, nx, ny, nz, u, v;

					memcpy(&px, vert_ptr + VERT_OFF_POS + 0, 4);
					memcpy(&py, vert_ptr + VERT_OFF_POS + 4, 4);
					memcpy(&pz, vert_ptr + VERT_OFF_POS + 8, 4);
					memcpy(&nx, vert_ptr + norm_off + 0, 4);
					memcpy(&ny, vert_ptr + norm_off + 4, 4);
					memcpy(&nz, vert_ptr + norm_off + 8, 4);
					memcpy(&u,  vert_ptr + uv_off + 0, 4);
					memcpy(&v,  vert_ptr + uv_off + 4, 4);

					u32 idx = (u32)mesh.positions.size();
					mesh.positions.push_back({px, pz, py});
					mesh.normals.push_back({nx, nz, ny});
					mesh.uvs.push_back({u, 1.0f - v});  // V-flip: D3D top-left > OBJ bottom-left
					v_to_local[src_idx] = idx;

					return idx;
				};

				for(size_t tri_idx = 0; tri_idx < pf.triangles.size(); ++tri_idx) {

					if(tri_idx < ctx.geometry_exclusions.size() && ctx.geometry_exclusions[tri_idx])
						continue;

					const auto& tri = pf.triangles[tri_idx];

					if(tri.group_idx != group_idx)
						continue;

					u32 idx_a = add_vertex(tri.vi[0]);
					u32 idx_b = add_vertex(tri.vi[1]);
					u32 idx_c = add_vertex(tri.vi[2]);

					model::Face face = {};
					face.v[0] = {(i32)idx_a, (i32)idx_a, (i32)idx_a};
					face.v[1] = {(i32)idx_b, (i32)idx_b, (i32)idx_b};
					face.v[2] = {(i32)idx_c, (i32)idx_c, (i32)idx_c};
					face.material = tri.mat_idx;
					mesh.faces.push_back(face);
				}

				model.meshes.push_back(std::move(mesh));
			}
		}

		//========================================================================
		// Build collision mesh from the CDTA
		//========================================================================
		bool build_collision(const ParsedGpm& pf, model::Mesh& out) {

			out.group_name = pf.model_name + "_collision";

			if(!pf.volumes.empty() && !pf.planes.empty()) {

				std::vector<model::Plane> ir_planes;
				ir_planes.reserve(pf.planes.size());
				for(const auto& p : pf.planes)
					ir_planes.push_back({p.nx, p.ny, p.nz, p.d});

				for(size_t i = 0; i < pf.volumes.size(); ++i) {
					const auto& vol = pf.volumes[i];

					if(vol.plane_start + vol.plane_count > ir_planes.size())
						continue;

					std::span<const model::Plane> subset(
						ir_planes.data() + vol.plane_start, vol.plane_count);

					std::string group = "hull_" + std::to_string(i);
					writer::extras::build_hull(subset, vol.bbox, group, out);
				}

				if(!out.faces.empty())
					return true;
			}

			// CFAC fallback - copy decoded mesh into IR
			out.positions.reserve(pf.collision.positions.size());
			for(const auto& p : pf.collision.positions)
				out.positions.push_back({p[0], p[1], p[2]});

			out.normals.reserve(pf.collision.normals.size());
			for(const auto& n : pf.collision.normals)
				out.normals.push_back({n[0], n[1], n[2]});

			out.faces.reserve(pf.collision.faces.size());
			for(const auto& f : pf.collision.faces) {
				model::Face face = {};
				face.v[0] = {(i32)f.vi[0], -1, -1};
				face.v[1] = {(i32)f.vi[1], -1, -1};
				face.v[2] = {(i32)f.vi[2], -1, -1};
				out.faces.push_back(face);
			}

			return !out.faces.empty();
		}

		//========================================================================
		// Copy parsed hardpoints with engine /65536 scale.
		//========================================================================
		bool build_hardpoints(const ParsedGpm& pf, model::Model& model) {

			for(const auto& src : pf.hardpoints) {
				model::Hardpoint h;
				h.name = src.name;
				h.pos  = {
					src.pos[0] / CTRL_REG_POS_SCALE,
					src.pos[1] / CTRL_REG_POS_SCALE,
					src.pos[2] / CTRL_REG_POS_SCALE,
				};
				model.hardpoints.push_back(std::move(h));
			}

			return !model.hardpoints.empty();
		}

		//========================================================================
		// Materials + per-mesh geometry.
		//========================================================================
		void build_model(
			const ParsedGpm& pf,
			size_t /*lod_idx*/,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			model.source_name = std::format("{} (GP{} v{}.{}.{})",
				std::string(ctx.input_basename), pf.subtype,
				pf.version[0], pf.version[1], pf.version[2]);

			model.format_tag = "gpm_v2";

			build_materials(pf, ctx, model);
			build_meshes(pf, ctx, model);
		}

	}  // namespace

	//========================================================================
	// Converter
	//========================================================================
	bool Converter::convert(
		const std::string& input_file,
		const model::ConvertOptions& opts
	) {

		auto file = io::read_file(input_file);

		if(!file) {
			nlog::error("cannot open '%s'", input_file.c_str());
			return false;
		}

		std::string input_dir      = io::dirname(input_file);
		std::string input_basename = input_file.substr(input_file.find_last_of("/\\") + 1);

		ParsedGpm pf;
		if(!parse_gpm(*file, pf, opts.env, opts.lod))
			return false;

		const char* type_name = (pf.subtype == 'M') ? "static"
		                      : (pf.subtype == 'S') ? "skinned-1"
		                                            : "skinned-4";

		std::string format_label = std::format("GP{} v{}.{}.{}",
			pf.subtype, pf.version[0], pf.version[1], pf.version[2]);

		nlog::announce(input_basename, format_label,
			std::format("model={}, type={}, lod={}/{}", pf.model_name, type_name, pf.lod_picked, pf.lod_count));

		bool any_per_env = false;
		bool any_bound   = false;

		for(size_t m = 0; m < pf.materials.size(); ++m) {

			if(pf.materials[m].env_tag_0 != 2)
				continue;

			any_per_env = true;

			if(pf.mat_to_tex_idx[m] >= 0) {
				any_bound = true;
				break;
			}

		}

		if(any_per_env && !any_bound)
			nlog::warn("This model may not support env %d", opts.env, opts.env);

		io::make_dirs(opts.out_dir);

		filters::Scope scope = filters::Scope::Gpm;

		model::TextureExclusions tex_ex = collect_texture_exclusions(pf, scope, opts);
		auto specs = texture_specs(pf, tex_ex, scope, opts);

		std::set<std::string> mixed = resolve_mixed_textures(pf);

		std::vector<texture::Image> textures;
		textures.reserve(pf.textures.size());

		int missing = 0;

		for(size_t i = 0; i < pf.textures.size(); ++i) {

			texture::Image tex;
			tex.name = io::strip_ext(pf.textures[i].name);

			if(i < tex_ex.drop_texture.size() && tex_ex.drop_texture[i]) {
				textures.push_back(std::move(tex));
				continue;
			}

			if(!texture::parse(tex, input_dir, pf.textures[i].name)) {
				++missing;
				textures.push_back(std::move(tex));
				continue;
			}

			tex.spec = specs[i];

			if(mixed.count(io::to_lower(pf.textures[i].name))) {
				texture::TextureSpec opaque_spec = specs[i];
				opaque_spec.mode         = texture::AlphaMode::Opaque;
				opaque_spec.alpha_cutoff = 0;
				tex.companions.push_back({"_opaque", opaque_spec});
			}

			textures.push_back(std::move(tex));
		}

		if(missing > 0)
			nlog::warn("%d texture files not found", missing);

		auto write_result = texture::write_all(opts.out_dir, std::span<const texture::Image>{textures}, opts.raw);

		model::TextureOutput tex_out;
		tex_out.filenames = std::move(write_result.filenames);

		size_t lod_count = 1;

		for(size_t lod_idx = 0; lod_idx < lod_count; ++lod_idx) {

			std::vector<u8> geo_ex = collect_geometry_exclusions(pf, lod_idx, tex_ex, scope, opts);

			model::BuildContext ctx {
				opts,
				std::string_view{input_basename},
				std::string_view{input_dir},
				scope,
				tex_ex,
				std::span<const u8>{geo_ex.data(), geo_ex.size()},
				tex_out,
				std::span<const texture::TextureSpec>{specs.data(), specs.size()},
			};

			model::Model model;
			build_model(pf, lod_idx, ctx, model);

			if(lod_idx == 0) {

				std::string base = io::file_prefix(pf.model_name);

				if(opts.collision || opts.debug) {
					if(build_collision(pf, model.collision) && !model.collision.faces.empty() && opts.collision) {
						std::string col_path = opts.out_dir + "\\" + base + "_collision.obj";

						if(writer::extras::write_collision(model.collision, col_path))
							nlog::info("Wrote %s\n", col_path.c_str());

					}
				}

				if(opts.hardpoints || opts.debug) {
					if(build_hardpoints(pf, model) && !model.hardpoints.empty() && opts.hardpoints) {
						std::string hp_path = opts.out_dir + "\\" + base + "_hardpoints.txt";

						if(writer::extras::write_hardpoints(model, hp_path))
							nlog::info("Wrote %s\n", hp_path.c_str());
							
					}
				}
			}

			writer::DebugOpts dbg;
			if(opts.debug) {
				dbg.normals    = true;
				dbg.hardpoints = true;
				dbg.collision  = true;
			}

			if(!writer::export_model(model, pf.model_name, opts, dbg))
				return false;

			writer::debug::ModelInfo mi = writer::debug::measure(model);

			nlog::info(" dest=%s\\\n", opts.out_dir.c_str());
			nlog::info(" verts=%zu, tris=%zu, mats=%zu, textures=%zu\n\n",
				mi.position_count, mi.face_count, model.materials.size(), pf.textures.size());

		}

		return true;
	}

}
