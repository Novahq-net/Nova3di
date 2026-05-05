#include "t3di3.h"

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

namespace nova3di::format::t3di3 {

	namespace {

		namespace nlog    = nova3di::util::log;
		namespace io      = nova3di::util::io;
		namespace texture = nova3di::texture;
		namespace filters = nova3di::model::filters;

		// Header
		constexpr u32 T3DI3_HEADER_SIZE = 8;
		constexpr u32 T3DI3_OFF_VERSION = 0x04;
		constexpr u32 T3DI3_MIN_VERSION = 0x0103;

		// IFF count + stride
		constexpr u32 ARRAY_HEADER_SIZE = 8;
		constexpr u32 VERT_HEADER_SIZE  = 12;

		// Game defs (GHDR)
		constexpr u32 GHDR_SIZE          = 28;
		constexpr u32 GHDR_OFF_NAME      = 0x00;
		constexpr u32 GHDR_NAME_LEN      = 16;
		constexpr u32 GHDR_OFF_TYPE      = 0x10;
		constexpr u32 GHDR_OFF_LOD_COUNT = 0x14;

		// Material (MTRL)
		constexpr u32 MTRL_OFF_SHADER        = 0x00;
		constexpr u32 MTRL_SHADER_LEN        = 32;
		constexpr u32 MTRL_OFF_NUM_TEX       = 0x20;
		constexpr u32 MTRL_OFF_TEX_ARR       = 0x24;
		constexpr u32 MTRL_OFF_FLAGS         = 0x240; // bit 0=alpha_test, 1=alpha_invert, 2=cull_none
		constexpr u32 MTRL_OFF_ALPHA_REF     = 0x241; // D3DRS_ALPHAREF
		constexpr u8  MTRL_FLAG_ALPHA_TEST   = 0x01;
		constexpr u8  MTRL_FLAG_ALPHA_INVERT = 0x02;
		constexpr u8  MTRL_FLAG_CULL_NONE    = 0x04;

		// Material textures
		constexpr u32 MTRL_TEX_ENTRY_SIZE    = 20;
		constexpr u32 MTRL_TEX_NAME_LEN      = 16;
		constexpr u32 MTRL_TEX_OFF_BLEND_SRC = 0x10;
		constexpr u32 MTRL_TEX_OFF_BLEND_DST = 0x11;
		constexpr u32 MTRL_TEX_OFF_FLAGS     = 0x12;
		constexpr u32 MTRL_TEX_OFF_ALPHA_REF = 0x13;

		// Render strip entry (STRP)
		constexpr u32 STRP_OFF_MAT        = 0x00;
		constexpr u32 STRP_OFF_IDX_START  = 0x04;
		constexpr u32 STRP_OFF_IDX_COUNT  = 0x08;
		constexpr u32 STRP_OFF_FLAG       = 0x0C;
		constexpr u32 STRP_OFF_VERT_START = 0x10;

		// Render object entry (ROBJ)
		constexpr u32 ROBJ_OFF_NUM_STRIPS = 0x04;

		// Collision verts / normals / faces (CVRT / CNRM / CFAC)
		constexpr u32   CVRT_ENTRY_SIZE = 8;
		constexpr u32   CNRM_ENTRY_SIZE = 8;
		constexpr u32   CFAC_ENTRY_SIZE = 44;
		constexpr float CVRT_INT_SCALE  = 256.0f;
		constexpr float CNRM_INT_SCALE  = 16384.0f;

		// Collision planes (BPLN)
		constexpr u32   BPLN_ENTRY_SIZE   = 12;
		constexpr u32   BPLN_OFF_TYPE     = 0x00;
		constexpr u32   BPLN_OFF_NORMAL_X = 0x02;
		constexpr u32   BPLN_OFF_NORMAL_Y = 0x04;
		constexpr u32   BPLN_OFF_NORMAL_Z = 0x06;
		constexpr u32   BPLN_OFF_D        = 0x08;
		constexpr float BPLN_NORMAL_SCALE = 16384.0f;
		constexpr float BPLN_D_SCALE      = 65536.0f;

		// Collision volumes (BVOL)
		constexpr u32   BVOL_ENTRY_SIZE      = 36;
		constexpr u32   BVOL_OFF_TYPE        = 0x00;
		constexpr u32   BVOL_OFF_BBOX_X_MIN  = 0x08;
		constexpr u32   BVOL_OFF_BBOX_Y_MIN  = 0x0C;
		constexpr u32   BVOL_OFF_BBOX_Z_MIN  = 0x10;
		constexpr u32   BVOL_OFF_BBOX_X_MAX  = 0x14;
		constexpr u32   BVOL_OFF_BBOX_Y_MAX  = 0x18;
		constexpr u32   BVOL_OFF_BBOX_Z_MAX  = 0x1C;
		constexpr u32   BVOL_OFF_PLANE_COUNT = 0x20;
		constexpr float BVOL_SCALE           = 65536.0f;

		// User positioned hardpoints (USRP)
		constexpr u32   USRP_OFF_POS_X  = 0x00;
		constexpr u32   USRP_OFF_POS_Y  = 0x04;
		constexpr u32   USRP_OFF_POS_Z  = 0x08;
		constexpr u32   USRP_OFF_DIR_X  = 0x0C;
		constexpr u32   USRP_OFF_DIR_Y  = 0x10;
		constexpr u32   USRP_OFF_DIR_Z  = 0x14;
		constexpr u32   USRP_OFF_FLAGS  = 0x18;
		constexpr u32   USRP_OFF_TYPE   = 0x1C;
		constexpr u32   USRP_OFF_NAME   = 0x20;
		constexpr u32   USRP_NAME_LEN   = 16;
		constexpr float USRP_POS_SCALE  = 256.0f;

		// Vertex
		constexpr u32 VERT_OFF_POS            = 0x00;
		constexpr u32 VERT_OFF_NORMAL_BASIC   = 0x0C;
		constexpr u32 VERT_OFF_UV_BASIC       = 0x18;
		constexpr u32 VERT_OFF_NORMAL_TANGENT = 0x1C;
		constexpr u32 VERT_OFF_UV_TANGENT     = 0x28;
		constexpr u32 VERT_FLAG_TANGENT       = 0x40;

		struct Texture {
			std::string name;
		};

		struct Material {
			std::string shader;
			std::string tex_name;
			u8          flags        = 0;     // 0x01=alpha_test, 0x02=alpha_invert, 0x04=cull_none
			u8          alpha_cutoff = 0;     // D3DRS_ALPHAREF when flags & 0x01
			u8          blend_mode   = 0;     // 0=opaque, 1=alpha_test, 2=alpha_blend, 3=additive
			bool        illuminate   = false; // shader contains "_LUM"
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

		struct Parsed3di3 {

			// Metadata
			std::string         model_name;
			u16                 version    = 0;
			u32                 model_type = 0;     // 1 = static, 2 = skinned
			bool                skinned    = false; // model_type == 2

			// LOD
			u32                 lod_count  = 0;
			u32                 lod_picked = 0;

			// Materials + textures + binding
			std::vector<Material> materials;
			std::vector<Texture>  textures;
			std::vector<int>      mat_to_tex_idx;  // -1 = no binding
			std::vector<Triangle> triangles;

			// Vertex data
			const u8* vert_data   = nullptr;
			u32       vert_count  = 0;
			u32       vert_stride = 0;
			u32       vert_flags  = 0;

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
		// Chunk helpers
		//========================================================================
		constexpr u32 chunk_tag(char a, char b, char c, char d) {
			return (u32)(u8)a | ((u32)(u8)b << 8) | ((u32)(u8)c << 16) | ((u32)(u8)d << 24);
		}

		const u8* chunk_find(const u8* data, size_t start, size_t end, u32 tag) {
			size_t pos = start;

			while(pos + 8 <= end) {
				u32 t     = io::read_u32(data + pos);
				u32 s_raw = io::read_u32(data + pos + 4);
				u32 s     = s_raw & 0x7FFFFFFF;

				if(t == tag)
					return data + pos;

				pos += 8 + s;
			}
			return nullptr;
		}

		u32 chunk_size(const u8* chunk) { 
			return io::read_u32(chunk + 4) & 0x7FFFFFFF; 
		}

		const u8* chunk_data(const u8* chunk) { 
			return chunk + 8; 
		}

		struct Chunks {
			const u8* ghdr = nullptr;
			const u8* mtrl = nullptr;
			const u8* rdta = nullptr;
			const u8* cdta = nullptr;
			const u8* usrp = nullptr;
		};

		//========================================================================
		// Find GHDR/MTRL/CDTA/RDTA/USRP chunks and return their pointers
		//========================================================================
		Chunks find_chunks(const u8* root, size_t body_begin, size_t body_end) {
			Chunks c;
			c.ghdr = chunk_find(root, body_begin, body_end, chunk_tag('G','H','D','R'));
			c.mtrl = chunk_find(root, body_begin, body_end, chunk_tag('M','T','R','L'));
			c.cdta = chunk_find(root, body_begin, body_end, chunk_tag('C','D','T','A'));
			c.rdta = chunk_find(root, body_begin, body_end, chunk_tag('R','D','T','A'));
			c.usrp = chunk_find(root, body_begin, body_end, chunk_tag('U','S','R','P'));

			if(c.ghdr && c.mtrl)
				return c;

			const u8* info_chunk = chunk_find(root, body_begin, body_end, chunk_tag('I','N','F','O'));
			if(!info_chunk)
				return c;

			u32 info_size = chunk_size(info_chunk);
			if(info_size == 0)
				return c;

			if(!c.ghdr) c.ghdr = chunk_find(info_chunk, 8, 8 + info_size, chunk_tag('G','H','D','R'));
			if(!c.mtrl) c.mtrl = chunk_find(info_chunk, 8, 8 + info_size, chunk_tag('M','T','R','L'));
			if(!c.usrp) c.usrp = chunk_find(info_chunk, 8, 8 + info_size, chunk_tag('U','S','R','P'));

			return c;
		}

		//========================================================================
		// Blend mode resolver based on shader name + material flags
		//========================================================================
		u8 resolve_blend_mode(const std::string& shader, u8 flags) {

			if(shader == "FFP_GLASS" || shader == "VS_SKGLASS" || shader == "VS_BUMPMIRRT")
				return 2;

			if(shader.find("_AB") != std::string::npos)
				return 2;

			if(shader.find("_AD") != std::string::npos)
				return 3;

			if(flags & MTRL_FLAG_ALPHA_TEST)
				return 1;

			return 0;
		}

		//========================================================================
		// Material
		//========================================================================
		void parse_materials(const u8* mtrl_chunk, Parsed3di3& pf) {

			if(!mtrl_chunk)
				return;

			const u8* mtrl_data = chunk_data(mtrl_chunk);
			u32 mat_count  = io::read_u32(mtrl_data);
			u32 mat_stride = io::read_u32(mtrl_data + 4);

			pf.materials.resize(mat_count);

			for(u32 i = 0; i < mat_count; ++i) {

				const u8* entry = mtrl_data + ARRAY_HEADER_SIZE + i * mat_stride;
				auto& mat = pf.materials[i];

				mat.shader     = io::sanitize_name((const char*)entry + MTRL_OFF_SHADER, MTRL_SHADER_LEN);
				mat.flags = entry[MTRL_OFF_FLAGS];
				mat.alpha_cutoff  = entry[MTRL_OFF_ALPHA_REF];
				mat.blend_mode = resolve_blend_mode(mat.shader, mat.flags);
				mat.illuminate = mat.shader.find("_LUM") != std::string::npos;

				u32 num_tex = io::read_u32(entry + MTRL_OFF_NUM_TEX);
				if(num_tex == 0)
					continue;

				const u8* layer_0 = entry + MTRL_OFF_TEX_ARR;
				mat.tex_name = io::sanitize_name((const char*)layer_0, MTRL_TEX_NAME_LEN);
			}
		}

		//========================================================================
		// Vertices
		//========================================================================
		void parse_vertices(const u8* vert_chunk, Parsed3di3& pf) {

			const u8* vert_hdr = chunk_data(vert_chunk);
			pf.vert_count  = io::read_u32(vert_hdr);
			pf.vert_stride = io::read_u32(vert_hdr + 4);
			pf.vert_flags  = io::read_u32(vert_hdr + 8);
			pf.vert_data   = vert_hdr + VERT_HEADER_SIZE;
		}

		//========================================================================
		// Decoding render strips (STRP) into triangles (TRIS)
		// STRP = material index + vertex index array defining a triangle list or strip
		// INDX = vertex index arrays referenced by STRP
		// ROBJ = optional grouping of STRP into meshes (groups)
		//========================================================================
		void decode_strip(
			const u8* strip_entry,
			const u8* indices,
			u32 group_idx,
			const Parsed3di3& pf,
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
		// Decode all strips from the picked LOD's RLOD chunk
		//========================================================================
		bool decode_strips(
			const u8* rlod_chunk, 
			u32 rlod_size, 
			Parsed3di3& pf
		) {

			const u8* indx_chunk = chunk_find(rlod_chunk, 8, 8 + rlod_size, chunk_tag('I','N','D','X'));
			const u8* strp_chunk = chunk_find(rlod_chunk, 8, 8 + rlod_size, chunk_tag('S','T','R','P'));
			const u8* robj_chunk = chunk_find(rlod_chunk, 8, 8 + rlod_size, chunk_tag('R','O','B','J'));

			if(!indx_chunk) { 
				nlog::error("INDX chunk not found"); 
				return false; 
			}

			if(!strp_chunk) { 
				nlog::error("STRP chunk not found"); 
				return false; 
			}

			const u8* indx_hdr  = chunk_data(indx_chunk);
			u32 indx_count      = io::read_u32(indx_hdr);
			const u8* indx_data = indx_hdr + ARRAY_HEADER_SIZE;

			const u8* strp_hdr  = chunk_data(strp_chunk);
			u32 strp_count      = io::read_u32(strp_hdr);
			u32 strp_stride     = io::read_u32(strp_hdr + 4);
			const u8* strp_data = strp_hdr + ARRAY_HEADER_SIZE;

			std::vector<u32> strip_to_group(strp_count, 0);
			pf.mesh_count = 1;

			if(robj_chunk) {
				const u8* robj_hdr  = chunk_data(robj_chunk);
				u32 robj_count      = io::read_u32(robj_hdr);
				u32 robj_stride     = io::read_u32(robj_hdr + 4);
				const u8* robj_data = robj_hdr + ARRAY_HEADER_SIZE;

				u32 cumulative = 0;
				for(u32 robj_i = 0; robj_i < robj_count; ++robj_i) {

					const u8* robj_entry = robj_data + robj_i * robj_stride;
					u32 num_strips = io::read_u32(robj_entry + ROBJ_OFF_NUM_STRIPS);

					for(u32 i = 0; i < num_strips && cumulative < strp_count; ++i)
						strip_to_group[cumulative++] = robj_i;

				}

				pf.mesh_count = robj_count;
			}

			pf.triangles.reserve(indx_count);
			for(u32 strip_i = 0; strip_i < strp_count; ++strip_i) {
				const u8* strip_entry = strp_data + strip_i * strp_stride;
				u32 idx_start         = io::read_u32(strip_entry + STRP_OFF_IDX_START);
				const u8* indices     = indx_data + idx_start * 2;

				decode_strip(strip_entry, indices, strip_to_group[strip_i], pf, pf.triangles);
			}

			return true;
		}

		//========================================================================
		// RLOD under RDTA. block 0 = LOD 0 (highest)
		//========================================================================
		const u8* find_nth_lod(const u8* rdta_chunk, u32 lod_idx) {

			u32 rdta_size = chunk_size(rdta_chunk);
			size_t pos = 8;
			size_t end = 8 + rdta_size;
			u32 found  = 0;

			while(pos + 8 <= end) {
				u32 t = io::read_u32(rdta_chunk + pos);
				u32 s = io::read_u32(rdta_chunk + pos + 4) & 0x7FFFFFFF;

				if(t == chunk_tag('R','L','O','D')) {

					if(found == lod_idx)
						return rdta_chunk + pos;

					++found;
				}

				pos += 8 + s;
			}

			return nullptr;
		}

		//========================================================================
		// Pick the user-selected RLOD container under RDTA and decode its
		// VERT + (INDX/STRP/ROBJ) sub-chunks. Out-of-range falls back to
		// lowest-detail LOD.
		//========================================================================
		bool parse_lod(const u8* rdta_chunk, Parsed3di3& pf, int lod) {

			u32 find_lod = (u32)lod;

			if(find_lod >= pf.lod_count) {
				nlog::warn("--lod=%u not found (model has %u LODs), using default LOD 0", find_lod, pf.lod_count);
				find_lod = 0;
			}

			pf.lod_picked = find_lod;

			const u8* rlod_chunk = find_nth_lod(rdta_chunk, find_lod);
			if(!rlod_chunk) {
				nlog::error("LOD %u not found", find_lod);
				return false;
			}

			u32 rlod_size = chunk_size(rlod_chunk);

			const u8* vert_chunk = chunk_find(rlod_chunk, 8, 8 + rlod_size, chunk_tag('V','E','R','T'));

			if(!vert_chunk) { 
				nlog::error("VERT chunk not found"); 
				return false; 
			}

			parse_vertices(vert_chunk, pf);

			return decode_strips(rlod_chunk, rlod_size, pf);
		}

		//========================================================================
		// Collision mesh
		// CVRT = vertex positions
		// CNRM = vertex normals
		// CFAC = faces (vertex indices 
		// BPLN = collision planes
		// BVOL = collision volumes
		//========================================================================
		void parse_collision(const u8* cdta_chunk, Parsed3di3& pf) {

			if(!cdta_chunk)
				return;

			u32 cdta_size = chunk_size(cdta_chunk);
			const u8* cvrt_chunk = chunk_find(cdta_chunk, 8, 8 + cdta_size, chunk_tag('C','V','R','T'));
			const u8* cnrm_chunk = chunk_find(cdta_chunk, 8, 8 + cdta_size, chunk_tag('C','N','R','M'));
			const u8* cfac_chunk = chunk_find(cdta_chunk, 8, 8 + cdta_size, chunk_tag('C','F','A','C'));
			const u8* bpln_chunk = chunk_find(cdta_chunk, 8, 8 + cdta_size, chunk_tag('B','P','L','N'));
			const u8* bvol_chunk = chunk_find(cdta_chunk, 8, 8 + cdta_size, chunk_tag('B','V','O','L'));

			if(!cvrt_chunk || !cfac_chunk)
				return;

			const u8* cvrt_hdr  = chunk_data(cvrt_chunk);
			u32       vert_count = io::read_u32(cvrt_hdr);
			const u8* vert_data  = cvrt_hdr + ARRAY_HEADER_SIZE;

			pf.collision.positions.reserve(vert_count);
			for(u32 i = 0; i < vert_count; ++i) {
				const u8* vert_ptr = vert_data + i * CVRT_ENTRY_SIZE;
				i16 x = (i16)io::read_u16(vert_ptr);
				i16 y = (i16)io::read_u16(vert_ptr + 2);
				i16 z = (i16)io::read_u16(vert_ptr + 4);
				pf.collision.positions.push_back({
					x / CVRT_INT_SCALE,
					y / CVRT_INT_SCALE,
					z / CVRT_INT_SCALE,
				});
			}

			if(cnrm_chunk) {
				const u8* cnrm_hdr  = chunk_data(cnrm_chunk);
				u32       norm_count = io::read_u32(cnrm_hdr);
				const u8* norm_data  = cnrm_hdr + ARRAY_HEADER_SIZE;

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
			}

			const u8* cfac_hdr  = chunk_data(cfac_chunk);
			u32       face_count = io::read_u32(cfac_hdr);
			const u8* face_data  = cfac_hdr + ARRAY_HEADER_SIZE;

			pf.collision.faces.reserve(face_count);
			for(u32 i = 0; i < face_count; ++i) {
				const u8* face_ptr = face_data + i * CFAC_ENTRY_SIZE;

				u16 idx_a = io::read_u16(face_ptr);
				u16 idx_b = io::read_u16(face_ptr + 2);
				u16 idx_c = io::read_u16(face_ptr + 4);

				if(idx_a == idx_b || idx_b == idx_c || idx_a == idx_c)
					continue;

				const u8* vert_a = vert_data + idx_a * CVRT_ENTRY_SIZE;
				const u8* vert_b = vert_data + idx_b * CVRT_ENTRY_SIZE;
				const u8* vert_c = vert_data + idx_c * CVRT_ENTRY_SIZE;

				if(memcmp(vert_a, vert_b, 6) == 0 ||
				   memcmp(vert_b, vert_c, 6) == 0 ||
				   memcmp(vert_a, vert_c, 6) == 0)
					continue;

				pf.collision.faces.push_back({{idx_a, idx_b, idx_c}});
			}

			if(bpln_chunk) {
				const u8* bpln_hdr   = chunk_data(bpln_chunk);
				u32       plane_count = io::read_u32(bpln_hdr);
				const u8* plane_data = bpln_hdr + ARRAY_HEADER_SIZE;

				pf.planes.resize(plane_count);
				for(u32 i = 0; i < plane_count; ++i) {
					const u8* entry = plane_data + i * BPLN_ENTRY_SIZE;
					auto& pl = pf.planes[i];

					pl.nx = -(i16)io::read_u16(entry + BPLN_OFF_NORMAL_X) / BPLN_NORMAL_SCALE;
					pl.ny = -(i16)io::read_u16(entry + BPLN_OFF_NORMAL_Y) / BPLN_NORMAL_SCALE;
					pl.nz = -(i16)io::read_u16(entry + BPLN_OFF_NORMAL_Z) / BPLN_NORMAL_SCALE;
					pl.d  =  (i32)io::read_u32(entry + BPLN_OFF_D)        / BPLN_D_SCALE;
				}
			}

			if(bvol_chunk) {
				const u8* bvol_hdr    = chunk_data(bvol_chunk);
				u32       volume_count = io::read_u32(bvol_hdr);
				const u8* volume_data = bvol_hdr + ARRAY_HEADER_SIZE;

				pf.volumes.resize(volume_count);
				u32 plane_cursor = 0;
				for(u32 i = 0; i < volume_count; ++i) {
					const u8* entry = volume_data + i * BVOL_ENTRY_SIZE;
					auto& vol = pf.volumes[i];

					vol.type        = (u8)io::read_u32(entry + BVOL_OFF_TYPE);
					vol.plane_count = io::read_u32(entry + BVOL_OFF_PLANE_COUNT);
					vol.plane_start = plane_cursor;

					i32 x_min = (i32)io::read_u32(entry + BVOL_OFF_BBOX_X_MIN);
					i32 y_min = (i32)io::read_u32(entry + BVOL_OFF_BBOX_Y_MIN);
					i32 z_min = (i32)io::read_u32(entry + BVOL_OFF_BBOX_Z_MIN);
					i32 x_max = (i32)io::read_u32(entry + BVOL_OFF_BBOX_X_MAX);
					i32 y_max = (i32)io::read_u32(entry + BVOL_OFF_BBOX_Y_MAX);
					i32 z_max = (i32)io::read_u32(entry + BVOL_OFF_BBOX_Z_MAX);

					vol.bbox = {
						x_min / BVOL_SCALE, y_min / BVOL_SCALE, z_min / BVOL_SCALE,
						x_max / BVOL_SCALE, y_max / BVOL_SCALE, z_max / BVOL_SCALE,
					};

					plane_cursor += vol.plane_count;
				}
			}
		}

		//========================================================================
		// Parse user-positioned hardpoints from USRP
		//========================================================================
		void parse_hardpoints(const u8* usrp_chunk, Parsed3di3& pf) {

			if(!usrp_chunk)
				return;

			const u8* usrp_data = chunk_data(usrp_chunk);
			u32 hp_count  = io::read_u32(usrp_data);
			u32 hp_stride = io::read_u32(usrp_data + 4);

			pf.hardpoints.resize(hp_count);

			for(u32 i = 0; i < hp_count; ++i) {

				const u8* entry = usrp_data + ARRAY_HEADER_SIZE + i * hp_stride;
				auto& hp = pf.hardpoints[i];

				hp.pos[0] = (i32)io::read_u32(entry + USRP_OFF_POS_X);
				hp.pos[1] = (i32)io::read_u32(entry + USRP_OFF_POS_Y);
				hp.pos[2] = (i32)io::read_u32(entry + USRP_OFF_POS_Z);
				hp.dir[0] = (i32)io::read_u32(entry + USRP_OFF_DIR_X);
				hp.dir[1] = (i32)io::read_u32(entry + USRP_OFF_DIR_Y);
				hp.dir[2] = (i32)io::read_u32(entry + USRP_OFF_DIR_Z);
				hp.flags  = io::read_u32(entry + USRP_OFF_FLAGS);
				hp.type   = (u8)io::read_u32(entry + USRP_OFF_TYPE);

				char name_buf[USRP_NAME_LEN + 1] = {};
				memcpy(name_buf, entry + USRP_OFF_NAME, USRP_NAME_LEN);
				hp.name = name_buf;
			}
		}

		//========================================================================
		// Resolve material-to-texture bindings by ci string-matching of
		// material tex_name against deduped texture names
		//========================================================================
		void resolve_textures(Parsed3di3& pf) {

			// Dedupe filenames into pf.textures (ci)
			for(const auto& mat : pf.materials) {

				if(mat.tex_name.empty())
					continue;

				bool dup = false;
				for(const auto& seen : pf.textures) {
					if(io::to_lower(seen.name) == io::to_lower(mat.tex_name)) {
						dup = true;
						break;
					}
				}

				if(!dup) {
					Texture tex;
					tex.name = mat.tex_name;
					pf.textures.push_back(std::move(tex));
				}
			}

			pf.mat_to_tex_idx.assign(pf.materials.size(), -1);

			for(size_t m = 0; m < pf.materials.size(); ++m) {

				const std::string& name = pf.materials[m].tex_name;

				if(name.empty())
					continue;

				std::string base = io::to_lower(io::strip_ext(name));

				for(size_t t = 0; t < pf.textures.size(); ++t) {

					if(io::to_lower(io::strip_ext(pf.textures[t].name)) == base) {
						pf.mat_to_tex_idx[m] = (int)t;
						break;
					}
				}

			}

		}

		//========================================================================
		// Use alpha channel as opacity if
		//   flags bit 0 set   (D3DRS_ALPHATESTENABLE = cutout)
		//   blend_mode == 2   (D3DRS_ALPHABLENDENABLE = translucent)
		//   blend_mode == 3   (additive if closest OBJ analog is translucent)
		//========================================================================
		bool alpha_as_opacity(const Material& mat) {

			if(mat.flags & MTRL_FLAG_ALPHA_TEST)
				return true;

			if(mat.blend_mode == 2 || mat.blend_mode == 3)
				return true;

			return false;
		}

		//========================================================================
		// Parse 3DI3
		//========================================================================
		bool parse_3di3(std::span<const u8> bytes, Parsed3di3& pf, int lod) {

			const u8* data = bytes.data();
			size_t file_size = bytes.size();

			if(file_size < T3DI3_HEADER_SIZE + 8) {
				nlog::error("3DI3 file too small");
				return false;
			}

			pf.version = io::read_u16(data + T3DI3_OFF_VERSION);
			if(pf.version < T3DI3_MIN_VERSION) {
				nlog::error("unsupported 3DI3 version 0x%04X", pf.version);
				return false;
			}

			const u8* root = data + T3DI3_HEADER_SIZE;
			if(io::read_u32(root) != chunk_tag('R','O','O','T')) {
				nlog::error("expected ROOT chunk, got '%.4s'", (const char*)root);
				return false;
			}

			u32 root_size = chunk_size(root);
			Chunks chunks = find_chunks(root, 8, 8 + root_size);

			if(!chunks.ghdr) { 
				nlog::error("GHDR chunk not found"); 
				return false; 
			}

			if(!chunks.rdta) { 
				nlog::error("RDTA chunk not found"); 
				return false; 
			}

			const u8* ghdr = chunk_data(chunks.ghdr);
			pf.model_name = io::sanitize_name((const char*)ghdr + GHDR_OFF_NAME, GHDR_NAME_LEN);
			
			if(pf.model_name.empty())
				pf.model_name = "model";

			pf.model_type = io::read_u32(ghdr + GHDR_OFF_TYPE);
			pf.skinned    = (pf.model_type == 2);
			pf.lod_count  = io::read_u32(ghdr + GHDR_OFF_LOD_COUNT);

			parse_materials(chunks.mtrl, pf);

			if(!parse_lod(chunks.rdta, pf, lod))
				return false;

			parse_collision(chunks.cdta, pf);
			parse_hardpoints(chunks.usrp, pf);

			resolve_textures(pf);

			return true;
		}

		//========================================================================
		// Texture exclusions. Drops effect and shadow textures by default
		//========================================================================
		model::TextureExclusions collect_texture_exclusions(
			const Parsed3di3& pf,
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
		// Drop triangles whose material binds to an excluded texture
		//========================================================================
		std::vector<u8> collect_geometry_exclusions(
			const Parsed3di3& pf,
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
		// Map blend_mode to a metadata tag
		//========================================================================
		const char* blend_mode_meta(u8 blend_mode) {
			switch(blend_mode) {
				case 1: return "alpha_test";
				case 2: return "alpha_blend";
				case 3: return "additive";
				default: return nullptr;
			}
		}

		//========================================================================
		// Texture specs
		//========================================================================
		std::vector<texture::TextureSpec> texture_specs(
			const Parsed3di3& pf,
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

				const auto& mat = pf.materials[m];

				specs[t].mode = texture::AlphaMode::Explicit;

				if(mat.flags & MTRL_FLAG_ALPHA_TEST) {
					specs[t].alpha_cutoff        = mat.alpha_cutoff;
					specs[t].alpha_cutoff_invert = (mat.flags & MTRL_FLAG_ALPHA_INVERT) != 0;
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
		std::set<std::string> resolve_mixed_textures(const Parsed3di3& pf) {

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
			const Parsed3di3& pf,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			std::vector<std::string> unique_mat_name(pf.materials.size());
			std::map<std::string, int> name_count;

			for(size_t m = 0; m < pf.materials.size(); ++m) {

				std::string base = pf.materials[m].shader;

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

						if(io::extension(filename) == ".tga" && alpha_as_opacity(src) && src.blend_mode != 3)
							mat.alpha = filename;

					}
				}

				if(mat.texture.empty() && !src.tex_name.empty())
					mat.texture = src.tex_name;

				if(!mat.texture.empty() && src.illuminate)
					mat.emission = mat.texture;

				if(mat.texture.empty() && src.blend_mode == 2) {
					mat.color[0] = 0.1f;
					mat.color[1] = 0.1f;
					mat.color[2] = 0.1f;
					mat.opacity  = 0.2f;
				}

				else if(!mat.texture.empty() && src.blend_mode == 3) {
					mat.opacity = 0.5f;
				}

				// nudge opacity below 1.0 so f3d shows alpha
				else if(!mat.texture.empty() && src.blend_mode == 2) {
					mat.opacity = 0.99f;
				}

				if(const char* tag = blend_mode_meta(src.blend_mode))
					mat.metadata["blend_mode"] = tag;

				if(src.flags & MTRL_FLAG_CULL_NONE)
					mat.metadata["double_sided"] = "1";

				if(!src.shader.empty())
					mat.metadata["shader"] = src.shader;

				if(src.flags & MTRL_FLAG_ALPHA_TEST) {
					mat.metadata["alpha_ref"] = std::to_string(src.alpha_cutoff);
					if(src.flags & MTRL_FLAG_ALPHA_INVERT)
						mat.metadata["alpha_invert"] = "1";
				}

				model.materials.push_back(mat);
			}
		}

		//========================================================================
		// Build one mesh per group_idx
		//========================================================================
		void build_meshes(
			const Parsed3di3& pf,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			u32 group_count = pf.mesh_count;

			const bool tangent_variant = (pf.vert_flags & VERT_FLAG_TANGENT) != 0;
			const u32  norm_off = tangent_variant ? VERT_OFF_NORMAL_TANGENT : VERT_OFF_NORMAL_BASIC;
			const u32  uv_off   = tangent_variant ? VERT_OFF_UV_TANGENT     : VERT_OFF_UV_BASIC;

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
		// Build collision mesh
		//========================================================================
		bool build_collision(const Parsed3di3& pf, model::Mesh& out) {

			out.group_name = pf.model_name + "_collision";

			if(pf.collision.faces.empty() && pf.volumes.empty())
				return false;

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
		// Copy parsed hardpoints with engine 1/256 scale
		//========================================================================
		bool build_hardpoints(const Parsed3di3& pf, model::Model& model) {

			for(const auto& src : pf.hardpoints) {
				
				model::Hardpoint h;
				h.name = src.name;
				h.pos  = {
					src.pos[0] / USRP_POS_SCALE,
					src.pos[1] / USRP_POS_SCALE,
					src.pos[2] / USRP_POS_SCALE,
				};

				model.hardpoints.push_back(std::move(h));
			}

			return !model.hardpoints.empty();
		}

		//========================================================================
		// Materials + per-group geometry
		//========================================================================
		void build_model(
			const Parsed3di3& pf,
			size_t /*lod_idx*/,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			model.source_name = std::format("{} (3DI3 v{}.{})",
				pf.model_name, pf.version >> 8, pf.version & 0xFF);
			model.format_tag = "3di3";

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

		Parsed3di3 pf;
		if(!parse_3di3(*file, pf, opts.lod))
			return false;

		const char* type_name = pf.skinned ? "skinned" : "static";

		std::string format_label = std::format("3DI3 v{}.{}", pf.version >> 8, pf.version & 0xFF);

		nlog::announce(input_basename, format_label,
			std::format("model={}, type={}, lod={}/{}", pf.model_name, type_name, pf.lod_picked, pf.lod_count));

		io::make_dirs(opts.out_dir);

		filters::Scope scope = filters::Scope::T3di3;

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
