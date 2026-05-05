#include "t3di.h"

#include "../model/model.h"
#include "../texture/parse.h"
#include "../texture/write.h"
#include "../texture/palette.h"
#include "../texture/tga.h"
#include "../util/io.h"
#include "../util/log.h"
#include "../util/types.h"
#include "../writer/debug.h"
#include "../writer/extras.h"
#include "../writer/export.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <span>
#include <vector>

namespace nova3di::format::t3di {

	namespace {

		namespace nlog    = nova3di::util::log;
		namespace io      = nova3di::util::io;
		namespace texture = nova3di::texture;
		namespace palette = nova3di::texture::palette;
		namespace filters = nova3di::model::filters;
		namespace tga     = nova3di::texture::tga;

		constexpr u32 T3DI_OFF_VERSION      = 0x03;
		constexpr u32 T3DI_OFF_NAME         = 0x04;
		constexpr u32 T3DI_NAME_LEN         = 12;
		constexpr u32 T3DI_OFF_LOD_COUNT    = 0x14;
		constexpr u32 T3DI_OFF_PORTAL_COUNT = 0x78; // v10
		constexpr u32 PORTAL_ENTRY_SIZE     = 20;

		// LOD header / sub-objects
		constexpr u32 LOD_OFF_DATA_SIZE  = 0x14;
		constexpr u32 LOD_OFF_VERT_COUNT = 0x80;
		constexpr u32 LOD_OFF_NORM_COUNT = 0x88;
		constexpr u32 LOD_OFF_FACE_COUNT = 0x90;

		// Vertex / Normal
		constexpr u32   VERT_ENTRY_SIZE     = 8;
		constexpr u32   VERT_OFF_X          = 0x00;
		constexpr u32   VERT_OFF_Y          = 0x02;
		constexpr u32   VERT_OFF_Z          = 0x04;
		constexpr u32   VERT_OFF_MESH_INDEX = 0x06;

		constexpr u32   NORM_ENTRY_SIZE     = 8;
		constexpr u32   NORM_OFF_NX         = 0x00;
		constexpr u32   NORM_OFF_NY         = 0x02;
		constexpr u32   NORM_OFF_NZ         = 0x04;
		constexpr u32   NORM_OFF_FLAGS      = 0x06;
		constexpr float NORM_INT_SCALE      = 16384.0f;

		// Face / Triangle
		constexpr u32   FACE_OFF_FLAGS       = 0x00;
		constexpr u32   FACE_OFF_FACE_NORMAL = 0x02;
		constexpr u32   FACE_OFF_U           = 0x04;
		constexpr u32   FACE_OFF_V           = 0x10;
		constexpr u32   FACE_OFF_VI          = 0x1C;
		constexpr u32   FACE_OFF_NI          = 0x22;
		constexpr u32   FACE_UV_TRIPLE_SIZE  = 12;
		constexpr float FACE_UV_SCALE        = 65536.0f; 

		// Mesh
		constexpr u32 MESH_OFF_VERT_COUNT       = 0x04;
		constexpr u32 MESH_OFF_FACE_COUNT       = 0x0C;
		constexpr u32 MESH_OFF_NORM_COUNT_SMALL = 0x14;
		constexpr u32 MESH_OFF_NORM_COUNT_LARGE = 0x1C;

		// Material
		constexpr u32 MTRL_OFF_NAME           = 0x00;
		constexpr u32 MTRL_NAME_LEN           = 12;
		constexpr u32 MTRL_OFF_FLAGS          = 0x10;
		constexpr u32 MTRL_OFF_SCALE_VALUE    = 0x14;
		constexpr u32 MTRL_OFF_FLAT_RGB       = 0x28;
		constexpr u32 MTRL_FLAT_RGB_LEN       = 3;
		constexpr u32 MTRL_ENV_RGB_LEN        = 12;
		constexpr u32 MTRL_OFF_TEX_INDEX      = 0x34;
		constexpr u32 MTRL_OFF_SECONDARY_TEX  = 0x37;
		constexpr u32 MTRL_OFF_FLAT_SHADE     = 0x3C;

		// Embedded palette (v<10)
		constexpr u32 PALETTE_ENTRY_COUNT = 256;
		constexpr u32 PALETTE_ENTRY_SIZE  = 4;
		constexpr u32 PALETTE_SIZE        = PALETTE_ENTRY_COUNT * PALETTE_ENTRY_SIZE;

		// Hardpoint
		constexpr u32   HPNT_ENTRY_SIZE = 12;
		constexpr float HPNT_INT_SCALE  = 256.0f;

		// BSP plane
		constexpr u32   PLANE_ENTRY_SIZE   = 8;
		constexpr u32   PLANE_OFF_NX       = 0x00;
		constexpr u32   PLANE_OFF_NY       = 0x02;
		constexpr u32   PLANE_OFF_NZ       = 0x04;
		constexpr u32   PLANE_OFF_D        = 0x06;
		constexpr float PLANE_NORMAL_SCALE = 16384.0f;

		// Collision volume
		constexpr u32   VOL_OFF_BBOX_X_MIN  = 0x08;
		constexpr u32   VOL_OFF_BBOX_X_MAX  = 0x0C;
		constexpr u32   VOL_OFF_BBOX_Y_MIN  = 0x10;
		constexpr u32   VOL_OFF_BBOX_Y_MAX  = 0x14;
		constexpr u32   VOL_OFF_BBOX_Z_MIN  = 0x18;
		constexpr u32   VOL_OFF_BBOX_Z_MAX  = 0x1C;
		constexpr u32   VOL_OFF_INLINE_NX   = 0x20;
		constexpr u32   VOL_OFF_INLINE_NY   = 0x24;
		constexpr u32   VOL_OFF_INLINE_NZ   = 0x28;
		constexpr u32   VOL_OFF_INLINE_D    = 0x2C;
		constexpr u32   VOL_OFF_PLANE_COUNT = 0x48; 
		constexpr float VOL_BBOX_SCALE      = 256.0f;
		constexpr float VOL_INLINE_SCALE    = 256.0f;

		// 3di Format structs
		struct Texture {
			std::string name;
			std::string mask_name;
			std::string filename;
			u16 width;
			u16 height;
			u32 data_size;
			u16 sequence;
			u8  hdr_flags;
			u32 flags32;
			std::vector<u8> pixels;
			u8 palette[PALETTE_SIZE];
		};

		struct Material {
			std::string name;
			u32  flags;
			u8   tex_idx;
			u8   frame_count;
			u8   constant_alpha;
			u8   flat_rgb[MTRL_FLAT_RGB_LEN];
			u8   secondary_tex_idx;
			float uv_scale_u;
			float uv_scale_v;
		};

		struct Triangle {
			u16 flags;
			i32 u[3];
			i32 v[3];
			i16 vi[3];
			i16 ni[3];
			u32 mat_idx;
		};

		struct Plane {
			float nx, ny, nz, d;
		};

		struct CollisionVolume {
			std::array<float, 6> bbox;        // {xmin, ymin, zmin, xmax, ymax, zmax}
			u32                  plane_start; // index into pf.planes
			u32                  plane_count;
		};

		struct Hardpoint {
			std::string name;
			i32         pos[3] = {};
		};

		struct Vertex {
			i16 x;
			i16 y;
			i16 z;
			u16 mesh_index;
		};

		struct Normal {
			i16 nx;
			i16 ny;
			i16 nz;
			i16 flags;
		};

		//========================================================================
		// Version table
		//========================================================================

		struct VersionTable {
			u8   min_version;
			u8   max_version; 

			// File header
			u32  file_hdr_size;
			bool has_portals;

			// Texture entry header
			u32  tex_hdr_size;           // 36, 52, or 80
			u32  tex_name_len;           // 12 or 32 (v10 uses 32-byte filenames)
			bool tex_embedded;           // true = has pixel+palette data after header
			bool tex_has_name2;
			u32  tex_off_data_size;
			u32  tex_off_sequence;       // sequence u16 (immediately precedes tex_off_flags)
			u32  tex_off_flags;
			u32  tex_off_width;
			u32  tex_off_height;

			// LOD header
			u32  lod_hdr_size;             // 176, 184, 192, or 232
			u32  lod_off_face_color_count; // 0 = field absent (pre-v10)
			u32  lod_off_mesh_count;
			u32  lod_off_hpnt_count;
			u32  lod_off_mat_count;
			u32  lod_off_plane_count;      // 0 = field absent (pre-v8)
			u32  lod_off_volume_count;     // 0 = field absent (pre-v5)

			// Data entry sizes
			u32  face_size;              // 72 or 80
			u32  face_off_material;      // material u32 at end of face entry: 0x44 (v<10) or 0x4C (v10)
			u32  face_color_size;        // 0 or 12
			u32  mesh_size;              // 88, 104, 112, or 120
			u32  material_size;          // 76, 120, or 128
			u32  colvol_size;            // 0, 72, 80, or 140

			// Material entry per-version offsets (0 = field absent)
			u32  mat_off_frame;          // pre-v10: 0x1F; v10: 0x1E
			u32  mat_off_timer;          // pre-v10: 0x1E; v10: 0x1F
			u32  mat_off_constant_alpha; // 0 for v<8, 0x41 for v8+
			u32  mat_off_env_rgb;        // 0 for v<10, 0x28 for v10
			u32  mat_off_uv_scale_u;     // 0 for v<10, 0x58 for v10
			u32  mat_off_uv_scale_v;     // 0 for v<10, 0x5C for v10
		};

		// Face field offsets
		// v2-v8: u at +4, v at +16, vi at +28, ni at +34, mat at +68
		// v10:   u at +4, v at +16, vi at +28, ni at +34, mat at +76 (shifted by 8)
		constexpr VersionTable VERSION_TABLES[] = {
			{
				.min_version = 2, .max_version = 2,
				.file_hdr_size = 124, .has_portals = false,
				.tex_hdr_size = 36, .tex_name_len = 12, .tex_embedded = true, .tex_has_name2 = false,
				.tex_off_data_size = 0x10, .tex_off_sequence = 0x14, .tex_off_flags = 0x16, .tex_off_width = 0x18, .tex_off_height = 0x1A,
				.lod_hdr_size = 176,
				.lod_off_face_color_count = 0, .lod_off_mesh_count = 0x98, .lod_off_hpnt_count = 0xA0, .lod_off_mat_count = 0xA8, .lod_off_plane_count = 0, .lod_off_volume_count = 0,
				.face_size = 72, .face_off_material = 0x44, .face_color_size = 0, .mesh_size = 88, .material_size = 76, .colvol_size = 0,
				.mat_off_frame = 0x1F, .mat_off_timer = 0x1E, .mat_off_constant_alpha = 0, .mat_off_env_rgb = 0, .mat_off_uv_scale_u = 0, .mat_off_uv_scale_v = 0,
			},
			{
				.min_version = 3, .max_version = 4,
				.file_hdr_size = 124, .has_portals = false,
				.tex_hdr_size = 36, .tex_name_len = 12, .tex_embedded = true, .tex_has_name2 = false,
				.tex_off_data_size = 0x10, .tex_off_sequence = 0x14, .tex_off_flags = 0x16, .tex_off_width = 0x18, .tex_off_height = 0x1A,
				.lod_hdr_size = 176,
				.lod_off_face_color_count = 0, .lod_off_mesh_count = 0x98, .lod_off_hpnt_count = 0xA0, .lod_off_mat_count = 0xA8, .lod_off_plane_count = 0, .lod_off_volume_count = 0,
				.face_size = 72, .face_off_material = 0x44, .face_color_size = 0, .mesh_size = 104, .material_size = 76, .colvol_size = 0,
				.mat_off_frame = 0x1F, .mat_off_timer = 0x1E, .mat_off_constant_alpha = 0, .mat_off_env_rgb = 0, .mat_off_uv_scale_u = 0, .mat_off_uv_scale_v = 0,
			},
			{
				.min_version = 5, .max_version = 6,
				.file_hdr_size = 124, .has_portals = false,
				.tex_hdr_size = 36, .tex_name_len = 12, .tex_embedded = true, .tex_has_name2 = false,
				.tex_off_data_size = 0x10, .tex_off_sequence = 0x14, .tex_off_flags = 0x16, .tex_off_width = 0x18, .tex_off_height = 0x1A,
				.lod_hdr_size = 184,
				.lod_off_face_color_count = 0, .lod_off_mesh_count = 0x98, .lod_off_hpnt_count = 0xA0, .lod_off_mat_count = 0xA8, .lod_off_plane_count = 0, .lod_off_volume_count = 0xB0,
				.face_size = 72, .face_off_material = 0x44, .face_color_size = 0, .mesh_size = 112, .material_size = 76, .colvol_size = 72,
				.mat_off_frame = 0x1F, .mat_off_timer = 0x1E, .mat_off_constant_alpha = 0, .mat_off_env_rgb = 0, .mat_off_uv_scale_u = 0, .mat_off_uv_scale_v = 0,
			},
			{
				.min_version = 7, .max_version = 7,
				.file_hdr_size = 124, .has_portals = false,
				.tex_hdr_size = 52, .tex_name_len = 12, .tex_embedded = true, .tex_has_name2 = true,
				.tex_off_data_size = 0x1C, .tex_off_sequence = 0x20, .tex_off_flags = 0x22, .tex_off_width = 0x24, .tex_off_height = 0x26,
				.lod_hdr_size = 184,
				.lod_off_face_color_count = 0, .lod_off_mesh_count = 0x98, .lod_off_hpnt_count = 0xA0, .lod_off_mat_count = 0xA8, .lod_off_plane_count = 0, .lod_off_volume_count = 0xB0,
				.face_size = 72, .face_off_material = 0x44, .face_color_size = 0, .mesh_size = 112, .material_size = 120, .colvol_size = 140,
				.mat_off_frame = 0x1F, .mat_off_timer = 0x1E, .mat_off_constant_alpha = 0, .mat_off_env_rgb = 0, .mat_off_uv_scale_u = 0, .mat_off_uv_scale_v = 0,
			},
			{
				.min_version = 8, .max_version = 9,
				.file_hdr_size = 124, .has_portals = false,
				.tex_hdr_size = 52, .tex_name_len = 12, .tex_embedded = true, .tex_has_name2 = true,
				.tex_off_data_size = 0x1C, .tex_off_sequence = 0x20, .tex_off_flags = 0x22, .tex_off_width = 0x24, .tex_off_height = 0x26,
				.lod_hdr_size = 192,
				.lod_off_face_color_count = 0, .lod_off_mesh_count = 0x98, .lod_off_hpnt_count = 0xA0, .lod_off_mat_count = 0xA8, .lod_off_plane_count = 0xB0, .lod_off_volume_count = 0xB8,
				.face_size = 72, .face_off_material = 0x44, .face_color_size = 0, .mesh_size = 112, .material_size = 120, .colvol_size = 80,
				.mat_off_frame = 0x1F, .mat_off_timer = 0x1E, .mat_off_constant_alpha = 0x41, .mat_off_env_rgb = 0, .mat_off_uv_scale_u = 0, .mat_off_uv_scale_v = 0,
			},
			{
				.min_version = 10, .max_version = 12,
				.file_hdr_size = 160, .has_portals = true,
				.tex_hdr_size = 80, .tex_name_len = 16, .tex_embedded = false, .tex_has_name2 = true,
				.tex_off_data_size = 0x24, .tex_off_sequence = 0x28, .tex_off_flags = 0x2A, .tex_off_width = 0x2C, .tex_off_height = 0x2E,
				.lod_hdr_size = 232,
				.lod_off_face_color_count = 0x98, .lod_off_mesh_count = 0xA0, .lod_off_hpnt_count = 0xA8, .lod_off_mat_count = 0xB0, .lod_off_plane_count = 0xB8, .lod_off_volume_count = 0xC0,
				.face_size = 80, .face_off_material = 0x4C, .face_color_size = 12, .mesh_size = 120, .material_size = 128, .colvol_size = 80,
				.mat_off_frame = 0x1E, .mat_off_timer = 0x1F, .mat_off_constant_alpha = 0x41, .mat_off_env_rgb = 0x28, .mat_off_uv_scale_u = 0x58, .mat_off_uv_scale_v = 0x5C,
			},
		};

		constexpr int NUM_VERSIONS = sizeof(VERSION_TABLES) / sizeof(VERSION_TABLES[0]);

		struct Parsed3di {

			// Metadata
			std::string model_name;
			u8          version    = 0;

			// LOD
			u32         lod_count  = 0;
			u32         lod_picked = 0;

			// Materials + textures + binding
			std::vector<Material> materials;
			std::vector<Texture>  textures;
			std::vector<int>      mat_to_tex_idx;  // -1 = no texture bound
			std::vector<Triangle> triangles;

			// Vertex
			std::vector<Vertex> vertices;
			std::vector<Normal> normals;

			// Mesh
			u32                 mesh_count = 0;

			// Collision
			std::vector<Plane>           planes;
			std::vector<CollisionVolume> volumes;

			// Hardpoints
			std::vector<Hardpoint>   hardpoints;
		};


		//========================================================================
		// Version resolution
		//========================================================================
		const VersionTable* resolve_version(u8 version) {
			for(int i = 0; i < NUM_VERSIONS; ++i) {

				if(version >= VERSION_TABLES[i].min_version && version <= VERSION_TABLES[i].max_version)
					return &VERSION_TABLES[i];

			}

			return nullptr;
		}

		//========================================================================
		// Reade a face entry and decode into a Triangle struct
		// Note that vi/ni are still mesh-relative
		//========================================================================
		Triangle read_face(const u8* p, const VersionTable* version) {
			Triangle tri = {};

			tri.flags = io::read_u16(p + FACE_OFF_FLAGS);

			memcpy(tri.u, p + FACE_OFF_U, FACE_UV_TRIPLE_SIZE);
			memcpy(tri.v, p + FACE_OFF_V, FACE_UV_TRIPLE_SIZE);

			tri.vi[0] = (i16)io::read_u16(p + FACE_OFF_VI + 0);
			tri.vi[1] = (i16)io::read_u16(p + FACE_OFF_VI + 2);
			tri.vi[2] = (i16)io::read_u16(p + FACE_OFF_VI + 4);
			tri.ni[0] = (i16)io::read_u16(p + FACE_OFF_NI + 0);
			tri.ni[1] = (i16)io::read_u16(p + FACE_OFF_NI + 2);
			tri.ni[2] = (i16)io::read_u16(p + FACE_OFF_NI + 4);

			tri.mat_idx = io::read_u32(p + version->face_off_material);

			return tri;
		}

		std::vector<int> resolve_mat_to_tex_idx(const Parsed3di& pf);

		//========================================================================
		// Parse texture entires
		//========================================================================
		bool parse_textures(
			const u8* data,
			size_t& pos,
			u32 tex_count,
			size_t file_size,
			const VersionTable& version,
			Parsed3di& pf
		) {

			pf.textures.resize(tex_count);

			for(u32 i = 0; i < tex_count; ++i) {

				if(pos + version.tex_hdr_size > file_size) {
					nlog::error("unexpected EOF in texture %u header", i);
					return false;
				}

				const u8* tex_hdr = data + pos;
				Texture& tex = pf.textures[i];

				tex.name      = io::sanitize_name((const char*)tex_hdr, version.tex_name_len);
				tex.mask_name = version.tex_has_name2 ? io::sanitize_name((const char*)tex_hdr + version.tex_name_len, version.tex_name_len) : "";
				tex.filename  = tex.name;
				tex.data_size = io::read_u32(tex_hdr + version.tex_off_data_size);
				tex.sequence  = io::read_u16(tex_hdr + version.tex_off_sequence);
				tex.width     = io::read_u16(tex_hdr + version.tex_off_width);
				tex.height    = io::read_u16(tex_hdr + version.tex_off_height);
				tex.hdr_flags = tex_hdr[version.tex_off_flags];

				tex.flags32 = (pf.version >= 10)
					? io::read_u32(tex_hdr + version.tex_off_flags)
					: (u32)tex.hdr_flags;

				pos += version.tex_hdr_size;

				if(version.tex_embedded) {
					u32 data_size_bytes = tex.data_size;

					if(pos + data_size_bytes + PALETTE_SIZE > file_size) {
						nlog::error("unexpected EOF in texture %u data", i);
						return false;
					}

					tex.pixels.assign(data + pos, data + pos + data_size_bytes);
					pos += data_size_bytes;

					memcpy(tex.palette, data + pos, PALETTE_SIZE);

					// DF1 (v2-v6) stores RGBA.
					// DF2 (v7+) stores palette as BGRA
					if(pf.version >= 7) {

						for(u32 j = 0; j < PALETTE_ENTRY_COUNT; ++j) {
							u8 tmp = tex.palette[j * PALETTE_ENTRY_SIZE];
							tex.palette[j * PALETTE_ENTRY_SIZE]     = tex.palette[j * PALETTE_ENTRY_SIZE + 2];
							tex.palette[j * PALETTE_ENTRY_SIZE + 2] = tmp;
						}

					}
					pos += PALETTE_SIZE;
				}
			}

			return true;
		}

		//========================================================================
		// Resolve each triangle's vi/ni from mesh-relative to global indices
		//========================================================================
		void resolve_triangle_indices(
			const u8* lod_data,
			size_t mesh_start,
			const VersionTable& version,
			u32 mesh_count,
			u32 tri_count,
			Parsed3di& pf
		) {

			u32 v_base = 0;
			u32 n_base = 0;
			u32 tri_idx = 0;
			
			u32 norm_count_off = (version.mesh_size >= 120) ? MESH_OFF_NORM_COUNT_LARGE : MESH_OFF_NORM_COUNT_SMALL;

			for(u32 m = 0; m < mesh_count; ++m) {

				const u8* mesh_entry = lod_data + mesh_start + m * version.mesh_size;

				u32 mesh_vert_count = io::read_u32(mesh_entry + MESH_OFF_VERT_COUNT);
				u32 mesh_tri_count  = io::read_u32(mesh_entry + MESH_OFF_FACE_COUNT);
				u32 mesh_norm_count = io::read_u32(mesh_entry + norm_count_off);

				for(u32 tri_i = 0; tri_i < mesh_tri_count && tri_idx < tri_count; ++tri_i, ++tri_idx) {
					pf.triangles[tri_idx].vi[0] += (i16)v_base;
					pf.triangles[tri_idx].vi[1] += (i16)v_base;
					pf.triangles[tri_idx].vi[2] += (i16)v_base;
					pf.triangles[tri_idx].ni[0] += (i16)n_base;
					pf.triangles[tri_idx].ni[1] += (i16)n_base;
					pf.triangles[tri_idx].ni[2] += (i16)n_base;
				}

				v_base += mesh_vert_count;
				n_base += mesh_norm_count;

			}

		}

		//========================================================================
		// Parse material entries
		//========================================================================
		void parse_materials(
			const u8* lod_data,
			size_t mat_start,
			u32 material_count,
			const VersionTable& version,
			Parsed3di& pf
		) {

			pf.materials.assign(material_count, {});

			for(u32 m = 0; m < material_count; ++m) {

				const u8* mat_entry = lod_data + mat_start + m * version.material_size;
				Material& mat = pf.materials[m];

				mat.name              = io::sanitize_name((const char*)mat_entry + MTRL_OFF_NAME, MTRL_NAME_LEN);
				mat.flags             = io::read_u32(mat_entry + MTRL_OFF_FLAGS);
				mat.frame_count       = mat_entry[version.mat_off_frame];
				mat.flat_rgb[0]       = mat_entry[MTRL_OFF_FLAT_RGB + 0];
				mat.flat_rgb[1]       = mat_entry[MTRL_OFF_FLAT_RGB + 1];
				mat.flat_rgb[2]       = mat_entry[MTRL_OFF_FLAT_RGB + 2];
				mat.tex_idx           = mat_entry[MTRL_OFF_TEX_INDEX];
				mat.secondary_tex_idx = mat_entry[MTRL_OFF_SECONDARY_TEX];

				mat.constant_alpha = version.mat_off_constant_alpha ? mat_entry[version.mat_off_constant_alpha] : 0;

				mat.uv_scale_u = 0.0f;
				mat.uv_scale_v = 0.0f;
				
				if(version.mat_off_uv_scale_u) {
					memcpy(&mat.uv_scale_u, mat_entry + version.mat_off_uv_scale_u, 4);
					memcpy(&mat.uv_scale_v, mat_entry + version.mat_off_uv_scale_v, 4);
				}

			}

		}

		//========================================================================
		// Read i16 fixed-point vertex array (8B each: x, y, z, mesh_index)
		//========================================================================
		void parse_vertices(const u8* lod_data, size_t& lod_data_pos, u32 count, Parsed3di& pf) {

			pf.vertices.assign(count, {});

			for(u32 i = 0; i < count; ++i) {
				const u8* p = lod_data + lod_data_pos + i * VERT_ENTRY_SIZE;
				pf.vertices[i].x          = (i16)io::read_u16(p + VERT_OFF_X);
				pf.vertices[i].y          = (i16)io::read_u16(p + VERT_OFF_Y);
				pf.vertices[i].z          = (i16)io::read_u16(p + VERT_OFF_Z);
				pf.vertices[i].mesh_index = io::read_u16(p + VERT_OFF_MESH_INDEX);
			}

			lod_data_pos += count * VERT_ENTRY_SIZE;
		}

		//========================================================================
		// Read i16 normal array (8B each: nx, ny, nz, flags)
		//========================================================================
		void parse_normals(const u8* lod_data, size_t& lod_data_pos, u32 count, Parsed3di& pf) {

			pf.normals.assign(count, {});

			for(u32 i = 0; i < count; ++i) {
				const u8* p = lod_data + lod_data_pos + i * NORM_ENTRY_SIZE;
				pf.normals[i].nx    = (i16)io::read_u16(p + NORM_OFF_NX);
				pf.normals[i].ny    = (i16)io::read_u16(p + NORM_OFF_NY);
				pf.normals[i].nz    = (i16)io::read_u16(p + NORM_OFF_NZ);
				pf.normals[i].flags = (i16)io::read_u16(p + NORM_OFF_FLAGS);
			}

			lod_data_pos += count * NORM_ENTRY_SIZE;
		}

		//========================================================================
		// Decode plane + volume arrays.
		//========================================================================
		void parse_collision(
			const u8* lod_data,
			size_t& lod_data_pos,
			u32 plane_count,
			u32 volume_count,
			const VersionTable& version,
			Parsed3di& pf
		) {

			size_t plane_section_size  = plane_count * PLANE_ENTRY_SIZE;
			size_t volume_section_size = volume_count * version.colvol_size;
			size_t plane_offset  = lod_data_pos;
			size_t volume_offset = lod_data_pos + plane_section_size;

			// Helper to fill a CollisionVolume bbox (minX, maxX, ...).
			auto read_bbox = [](const u8* v) -> std::array<float, 6> {

				i32 x_min = (i32)io::read_u32(v + VOL_OFF_BBOX_X_MIN);
				i32 x_max = (i32)io::read_u32(v + VOL_OFF_BBOX_X_MAX);
				i32 y_min = (i32)io::read_u32(v + VOL_OFF_BBOX_Y_MIN);
				i32 y_max = (i32)io::read_u32(v + VOL_OFF_BBOX_Y_MAX);
				i32 z_min = (i32)io::read_u32(v + VOL_OFF_BBOX_Z_MIN);
				i32 z_max = (i32)io::read_u32(v + VOL_OFF_BBOX_Z_MAX);

				return {
					x_min / VOL_BBOX_SCALE, y_min / VOL_BBOX_SCALE, z_min / VOL_BBOX_SCALE,
					x_max / VOL_BBOX_SCALE, y_max / VOL_BBOX_SCALE, z_max / VOL_BBOX_SCALE,
				};

			};

			if(volume_count > 0 && plane_count > 0 && version.colvol_size >= 80) {

				pf.planes.resize(plane_count);

				for(u32 i = 0; i < plane_count; ++i) {

					const u8* p = lod_data + plane_offset + i * PLANE_ENTRY_SIZE;

					pf.planes[i].nx = (i16)io::read_u16(p + PLANE_OFF_NX) / PLANE_NORMAL_SCALE;
					pf.planes[i].ny = (i16)io::read_u16(p + PLANE_OFF_NY) / PLANE_NORMAL_SCALE;
					pf.planes[i].nz = (i16)io::read_u16(p + PLANE_OFF_NZ) / PLANE_NORMAL_SCALE;

					pf.planes[i].d  = (float)(i16)io::read_u16(p + PLANE_OFF_D); // raw vert-space distance

				}

				pf.volumes.resize(volume_count);
				u32 cursor = 0;
				for(u32 i = 0; i < volume_count; ++i) {

					const u8* v = lod_data + volume_offset + i * version.colvol_size;

					pf.volumes[i].bbox        = read_bbox(v);
					pf.volumes[i].plane_start = cursor;
					pf.volumes[i].plane_count = io::read_u32(v + VOL_OFF_PLANE_COUNT);

					cursor += pf.volumes[i].plane_count;
				}

			} else if(volume_count > 0 && plane_count == 0 && version.colvol_size == 72) {

				pf.planes.resize(volume_count);
				pf.volumes.resize(volume_count);

				for(u32 i = 0; i < volume_count; ++i) {
					const u8* v = lod_data + volume_offset + i * version.colvol_size;

					pf.volumes[i].bbox        = read_bbox(v);
					pf.volumes[i].plane_start = i;
					pf.volumes[i].plane_count = 1;

					i32 nx = (i32)io::read_u32(v + VOL_OFF_INLINE_NX);
					i32 ny = (i32)io::read_u32(v + VOL_OFF_INLINE_NY);
					i32 nz = (i32)io::read_u32(v + VOL_OFF_INLINE_NZ);
					i32 d  = (i32)io::read_u32(v + VOL_OFF_INLINE_D);

					pf.planes[i].nx = nx / VOL_INLINE_SCALE;
					pf.planes[i].ny = ny / VOL_INLINE_SCALE;
					pf.planes[i].nz = nz / VOL_INLINE_SCALE;
					pf.planes[i].d  = d  / VOL_INLINE_SCALE;
				}

			}

			lod_data_pos += plane_section_size + volume_section_size;
		}

		//========================================================================
		// Decode hardpoints
		//========================================================================
		void parse_hardpoints(const u8* hp_data, u32 count, Parsed3di& pf) {

			pf.hardpoints.assign(count, {});

			for(u32 i = 0; i < count; ++i) {
				const u8* hp = hp_data + i * HPNT_ENTRY_SIZE;
				pf.hardpoints[i].name = "hardpoint_" + std::to_string(i);

				memcpy(&pf.hardpoints[i].pos[0], hp,     4);
				memcpy(&pf.hardpoints[i].pos[1], hp + 4, 4);
				memcpy(&pf.hardpoints[i].pos[2], hp + 8, 4);
			}
		}

		//========================================================================
		// Parse anim frame counts from materials and propagate frame flags
		//========================================================================
		void parse_anim_frames(Parsed3di& pf) {

			if(pf.version < 10)
				return;

			const u32 ANIM_MASK = 0x4000u | 0x8000u | 0x0020u | 0x001F0000u | 0x1C000000u;

			const size_t mat_count = pf.materials.size();
			for(size_t m = 0; m < mat_count; ++m) {

				if(!(pf.materials[m].flags & 0x4000))
					continue;

				u8 frame_count = pf.materials[m].frame_count;

				if(frame_count < 2)
					continue;

				u32 inherit = pf.materials[m].flags & ANIM_MASK;

				for(size_t k = 1; k < frame_count && m + k < mat_count; ++k)
					pf.materials[m + k].flags |= inherit;

			}
		}

		//========================================================================
		// Find the offset of the Nth LOD header
		//========================================================================
		bool find_nth_lod(const u8* data, size_t& pos, size_t file_size, u32 lod_idx, const VersionTable& version) {

			for(u32 i = 0; i < lod_idx; ++i) {

				if(pos + version.lod_hdr_size > file_size)
					return false;

				u32 skip_size = io::read_u32(data + pos + LOD_OFF_DATA_SIZE);
				pos += version.lod_hdr_size + skip_size;
			}

			return true;
		}

		//========================================================================
		// Parse resolved LOD 
		//========================================================================
		bool parse_lod(const u8* data, size_t& pos, size_t file_size, const VersionTable& version, Parsed3di& pf, int lod_idx) {

			u32 find_lod = (u32)lod_idx;

			if(find_lod >= pf.lod_count) {
				nlog::warn("--lod=%u not found (model has %u LODs), using default LOD 0", find_lod, pf.lod_count);
				find_lod = 0;
			}

			pf.lod_picked = find_lod;

			if(!find_nth_lod(data, pos, file_size, find_lod, version)) {
				nlog::error("LOD %u not found", find_lod);
				return false;
			}

			if(pos + version.lod_hdr_size > file_size) {
				nlog::error("unexpected EOF in LOD header");
				return false;
			}

			const u8* lod_hdr = data + pos;
			u32 lod_data_size   = io::read_u32(lod_hdr + LOD_OFF_DATA_SIZE);
			u32 vertex_count     = io::read_u32(lod_hdr + LOD_OFF_VERT_COUNT);
			u32 normal_count     = io::read_u32(lod_hdr + LOD_OFF_NORM_COUNT);
			u32 face_count       = io::read_u32(lod_hdr + LOD_OFF_FACE_COUNT);
			u32 face_color_count = version.lod_off_face_color_count ? io::read_u32(lod_hdr + version.lod_off_face_color_count) : 0;
			u32 mesh_count       = io::read_u32(lod_hdr + version.lod_off_mesh_count);
			u32 hardpoint_count  = io::read_u32(lod_hdr + version.lod_off_hpnt_count);
			u32 material_count   = io::read_u32(lod_hdr + version.lod_off_mat_count);
			u32 plane_count      = version.lod_off_plane_count  ? io::read_u32(lod_hdr + version.lod_off_plane_count)  : 0;
			u32 volume_count     = version.lod_off_volume_count ? io::read_u32(lod_hdr + version.lod_off_volume_count) : 0;
			pos += version.lod_hdr_size;

			pf.mesh_count = mesh_count;

			if(face_count == 0)
				return true;

			u32 expected_size = vertex_count * VERT_ENTRY_SIZE + normal_count * NORM_ENTRY_SIZE +
								face_count * version.face_size +
								face_color_count * version.face_color_size +
								mesh_count * version.mesh_size +
								hardpoint_count * HPNT_ENTRY_SIZE +
								material_count * version.material_size +
								plane_count * PLANE_ENTRY_SIZE +
								volume_count * version.colvol_size;

			if(expected_size != lod_data_size) {
				nlog::warn("lod data size field says %u, computed %u -- using computed", lod_data_size, expected_size);
				lod_data_size = expected_size;
			}

			if(pos + lod_data_size > file_size) {
				nlog::error("unexpected EOF in lod data");
				return false;
			}

			const u8* lod_data = data + pos;
			size_t lod_data_pos = 0;

			parse_vertices(lod_data, lod_data_pos, vertex_count, pf);
			parse_normals(lod_data, lod_data_pos, normal_count, pf);

			pf.triangles.assign(face_count, {});
			for(u32 i = 0; i < face_count; ++i)
				pf.triangles[i] = read_face(lod_data + lod_data_pos + i * version.face_size, &version);
			lod_data_pos += face_count * version.face_size;

			lod_data_pos += face_color_count * version.face_color_size;

			resolve_triangle_indices(lod_data, lod_data_pos, version, mesh_count, face_count, pf);

			lod_data_pos += mesh_count * version.mesh_size;
			size_t hardpoint_offset = lod_data_pos;
			lod_data_pos += hardpoint_count * HPNT_ENTRY_SIZE;

			parse_collision(lod_data, lod_data_pos, plane_count, volume_count, version, pf);

			parse_materials(lod_data, lod_data_pos, material_count, version, pf);
			parse_anim_frames(pf);
			parse_hardpoints(lod_data + hardpoint_offset, hardpoint_count, pf);

			pos += lod_data_size;
			return true;
		}

		//========================================================================
		// Parse 3di
		//========================================================================
		bool parse_3di(std::span<const u8> bytes, Parsed3di& pf, int lod_idx) {
			const u8* data = bytes.data();
			long file_size = (long)bytes.size();

			if(file_size < 4) {
				nlog::error("3DI file too small");
				return false;
			}

			pf.version = data[T3DI_OFF_VERSION];
			const VersionTable* version = resolve_version(pf.version);

			if(!version) {
				nlog::error("unsupported 3DI version %d", pf.version);
				return false;
			}

			pf.model_name = io::sanitize_name((const char*)data + T3DI_OFF_NAME, T3DI_NAME_LEN);
			if(pf.model_name.empty())
				pf.model_name = "model";

			pf.lod_count = io::read_u32(data + T3DI_OFF_LOD_COUNT);

			size_t pos = version->file_hdr_size;

			if(version->has_portals) {
				u32 portal_count = io::read_u32(data + T3DI_OFF_PORTAL_COUNT);
				pos += portal_count * PORTAL_ENTRY_SIZE;
			}

			if(pos + 4 > (size_t)file_size) {
				nlog::error("unexpected EOF");
				return false;
			}

			u32 tex_count = io::read_u32(data + pos);
			pos += 4;

			if(!parse_textures(data, pos, tex_count, (size_t)file_size, *version, pf))
				return false;

			if(!parse_lod(data, pos, (size_t)file_size, *version, pf, lod_idx))
				return false;

			// NULLOBJ models (face_count == 0)
			if(pf.triangles.empty())
				return true;

			pf.mat_to_tex_idx = resolve_mat_to_tex_idx(pf);
			return true;
		}


		//========================================================================
		// Materials the engine would skip rendering
		//=======================================================================
		bool is_nodraw(const Parsed3di& pf, u32 mat_idx) {

			if(mat_idx >= pf.materials.size())
				return true;

			if(pf.materials[mat_idx].flags & 0x0800)
				return true;

			if(pf.version >= 7
				&& !pf.textures.empty()
				&& (pf.materials[mat_idx].flags & 0x0001)
				&& pf.materials[mat_idx].tex_idx == 0xFF
			) return true;

			return false;
		}

		//========================================================================
		// Materials we want to exclude
		//======================================================================
		bool is_tex_excluded(
			const Parsed3di& pf, 
			const model::TextureExclusions& tex_ex, 
			filters::Scope scope, 
			u32 mat_idx
		) {

			if(mat_idx >= pf.mat_to_tex_idx.size())
				return false;

			int tex_idx = pf.mat_to_tex_idx[mat_idx];

			if(tex_idx >= 0)
				return (size_t)tex_idx < tex_ex.drop_texture.size()
					&& tex_ex.drop_texture[tex_idx] != 0;

			std::string name = io::strip_ext(pf.materials[mat_idx].name);
			return filters::is_effect(scope, name)
				|| filters::is_shadow(scope, name);
		}

		// Material > texture binding
		std::vector<int> resolve_mat_to_tex_idx(const Parsed3di& pf) {

			u32 tex_count = (u32)pf.textures.size();

			std::map<u8, int> seq_to_tex;

			if(pf.version < 10) {
				for(u32 t = 0; t < tex_count; ++t) {
					u8 seq_byte = (u8)(pf.textures[t].sequence & 0xFF);

					if(seq_to_tex.find(seq_byte) == seq_to_tex.end())
						seq_to_tex[seq_byte] = (int)t;

				}
			}

			std::vector<int> mat_to_tex_idx((u32)pf.materials.size(), -1);

			for(u32 m = 0; m < (u32)pf.materials.size(); ++m) {

				if(tex_count > 0 && !(pf.materials[m].flags & 0x8001))
					continue;

				bool secondary = (pf.materials[m].flags & 0x8000) && !(pf.materials[m].flags & 0x0001);
				u8 key = secondary ? pf.materials[m].secondary_tex_idx : pf.materials[m].tex_idx;

				if(pf.version >= 10 && key != 0xFF && key < tex_count) {
					int idx = (int)key;

					if(!secondary) {
						std::string mat_base = io::strip_ext(io::to_lower(pf.materials[m].name));
						std::string tex_base = io::strip_ext(io::to_lower(pf.textures[idx].name));

						if(!mat_base.empty() && mat_base == tex_base) {
							mat_to_tex_idx[m] = idx;
							continue;
						}

						mat_to_tex_idx[m] = idx;
					} else {
						mat_to_tex_idx[m] = idx;
						continue;
					}
				}

				if(pf.version < 10 && key != 0xFF) {
					auto it = seq_to_tex.find(key);
					if(it != seq_to_tex.end()) {
						mat_to_tex_idx[m] = it->second;
						continue;
					}
				}

				if(mat_to_tex_idx[m] >= 0)
					continue;

				std::string mat_name = io::to_lower(pf.materials[m].name);
				if(mat_name.empty())
					continue;

				std::string mat_name_base = io::strip_ext(mat_name);
				for(u32 t = 0; t < tex_count; ++t) {

					std::string tname = io::to_lower(pf.textures[t].name);
					std::string tname_base = io::strip_ext(tname);

					if(tname == mat_name || tname_base == mat_name_base ||
						io::to_lower(pf.textures[t].mask_name) == mat_name) {
						mat_to_tex_idx[m] = (int)t;
						break;
					}

				}

				if(mat_to_tex_idx[m] < 0) {
					for(u32 t = 0; t < tex_count; ++t) {
						std::string tname = io::to_lower(io::strip_ext(pf.textures[t].name));

						if(!tname.empty() && (tname.find(mat_name_base) == 0 || mat_name_base.find(tname) == 0)) {
							mat_to_tex_idx[m] = (int)t;
							break;
						}

					}

				}

			}

			return mat_to_tex_idx;
		}

		//========================================================================
		// v10 duplication key resolver. Tdo3 should probably use this too....
		//========================================================================
		std::vector<texture::DedupKey> resolve_dedup_keys(const Parsed3di& pf) {

			std::vector<texture::DedupKey> keys(pf.textures.size());

			for(size_t i = 0; i < pf.textures.size(); ++i) {

				const Texture& tex = pf.textures[i];
				texture::DedupKey& k = keys[i];

				k.name1_lc = io::to_lower(tex.name);
				k.name2_lc = io::to_lower(tex.mask_name);

				if(pf.version >= 10) {
					k.flag_bits = (u8)(tex.flags32 & 0x108);
					k.off36     = tex.data_size;
				}
			}

			return keys;
		}

		//========================================================================
		// 2-byte-per-pixel source. Decode to 32bpp BGRA with edge-bleed
		//========================================================================
		void decode_explicit_rgba(const Texture& src, texture::Image& img) {

			const u32 frame_pixels = (u32)src.width * src.height;
			std::vector<u8> bgra((size_t)frame_pixels * 4);
			std::vector<i32> queue;
			queue.reserve(frame_pixels);

			for(u32 p = 0; p < frame_pixels; ++p) {

				u8 idx   = src.pixels[p * 2];
				u8 alpha = src.pixels[p * 2 + 1];

				if(idx == 0)
					alpha = 0;

				bgra[p*4 + 0] = src.palette[idx * 4 + 2];
				bgra[p*4 + 1] = src.palette[idx * 4 + 1];
				bgra[p*4 + 2] = src.palette[idx * 4 + 0];
				bgra[p*4 + 3] = alpha;

				if(alpha != 0)
					queue.push_back((i32)p);
			}

			std::vector<u8> filled(frame_pixels, 0);
			for(auto pix_idx : queue)
				filled[pix_idx] = 1;

			tga::bleed_rgb(bgra.data(), filled, queue, (int)src.width, (int)src.height);

			img.format = texture::Format::Bgra;
			img.width  = src.width;
			img.height = src.height;
			img.pixels = std::move(bgra);
		}

		//========================================================================
		// 8-bit-per-pixel paletted source
		//========================================================================
		void decode_paletted(const Texture& src, texture::Image& img) {

			const u32 frame_pixels = (u32)src.width * src.height;
			const bool ratio2 = (src.data_size == frame_pixels * 2);
			u32 frame_count = ratio2 ? 1 : src.data_size / frame_pixels;

			if(frame_count == 0) 
				frame_count = 1;

			img.format = texture::Format::Paletted;
			img.width  = src.width;
			img.height = src.height;
			palette::compact_rgb(src.palette, img.palette.data());

			if(ratio2) {
				img.pixels.resize(frame_pixels);
				for(u32 p = 0; p < frame_pixels; ++p)
					img.pixels[p] = src.pixels[p * 2];
				return;
			}

			img.pixels.assign(src.pixels.begin(), src.pixels.begin() + (size_t)frame_pixels);

			for(u32 tri = 1; tri < frame_count; ++tri) {

				size_t off = (size_t)tri * frame_pixels;

				if(off + frame_pixels > src.pixels.size())
					break;

				img.frames.emplace_back(src.pixels.begin() + off, src.pixels.begin() + off + frame_pixels);
			}
		}

		//========================================================================
		// v10 mask: read a sibling paletted file and produce a per-pixel
		// brightness map ((r+g+b) * 85 / 256 LUT) 
		//========================================================================
		bool bake_mask_brightness(
			const std::string& input_dir,
			const std::string& mask_filename,
			texture::MaskBrightness& out
		) {

			texture::Image mask;

			if(!texture::parse(mask, input_dir, mask_filename))
				return false;

			if(mask.format != texture::Format::Paletted)
				return false;

			out.width  = mask.width;
			out.height = mask.height;
			out.pixels.resize((size_t)out.width * out.height);

			u8 lut[256];
			for(int i = 0; i < 256; ++i) {
				u32 sum = (u32)mask.palette[i*3] + mask.palette[i*3+1] + mask.palette[i*3+2];
				lut[i] = (u8)((85u * sum) >> 8);
			}

			for(size_t i = 0; i < out.pixels.size(); ++i)
				out.pixels[i] = lut[mask.pixels[i]];

			return true;
		}

		//========================================================================
		// v10: read the image from disk via texture::parse, then
		// optionally add mask_brightness from name2 if provided and mode=Mask
		//========================================================================
		bool load_external(const Texture& src, const std::string& input_dir, texture::Image& img) {

			std::string canonical_name = img.name;
			texture::TextureSpec spec  = img.spec;

			if(!texture::parse(img, input_dir, src.filename)) {
				img = {};
				img.name = canonical_name;
				return false;
			}

			img.name = canonical_name;
			img.spec = spec;

			if(spec.mode == texture::AlphaMode::Mask && !src.mask_name.empty()) {

				texture::MaskBrightness mb;

				if(bake_mask_brightness(input_dir, src.mask_name, mb))
					img.spec.mask_brightness = std::move(mb);
				else
					img.spec.mode = texture::AlphaMode::Preserve;

			}

			return true;
		}

		//========================================================================
		// Texture build: decode each source Texture into a texture::Image
		//========================================================================
		struct TextureBuildResult {
			std::vector<texture::Image> textures;
			int missing = 0;
		};

		TextureBuildResult build_textures(
			Parsed3di& pf,
			const std::string& input_dir,
			const std::vector<texture::TextureSpec>& specs,
			const model::TextureExclusions& tex_ex
		) {

			TextureBuildResult out;
			out.textures.reserve(pf.textures.size());

			for(size_t i = 0; i < pf.textures.size(); ++i) {

				const Texture& src = pf.textures[i];
				texture::Image img;
				img.name = io::strip_ext(src.name.empty() ? src.mask_name : src.name);
				img.spec = specs[i];

				const bool dropped = (i < tex_ex.drop_texture.size() && tex_ex.drop_texture[i]);
				const bool no_name = img.name.empty();
				const bool no_dims = (pf.version < 10 && (src.width == 0 || src.height == 0));

				if(dropped || no_name || no_dims) {
					out.textures.push_back(std::move(img));
					continue;
				}

				if(pf.version >= 10) {
					if(!load_external(src, input_dir, img))
						++out.missing;
					out.textures.push_back(std::move(img));
					continue;
				}

				const u32  frame_pixels = (u32)src.width * src.height;
				const bool ratio2       = (src.data_size == frame_pixels * 2);

				if(specs[i].mode == texture::AlphaMode::Explicit && ratio2)
					decode_explicit_rgba(src, img);
				else
					decode_paletted(src, img);

				out.textures.push_back(std::move(img));
			}

			return out;
		}

		//========================================================================
		// Per-material MTL "d" scalar
		//   constant-alpha  - v8+ flat-fill 0x0080 + !TEXTURED, byte mat+65
		//   ALPHA_50        - v<7 textured 0x0080 + 0x0001 fixed 50% blend
		//   brightness blend- v<10 mat 0x8000 / v10 tex hdr_flags 0x08
		//========================================================================
		std::vector<float> build_material_opacity(const Parsed3di& pf) {

			std::vector<float> opacity(pf.materials.size(), 1.0f);

			for(u32 m = 0; m < (u32)pf.materials.size(); ++m) {

				const Material& mat = pf.materials[m];
				u32 flags = mat.flags;
				double d = 1.0;

				if((flags & 0x0080) && !(flags & 0x0001) && pf.version >= 8) {
					u8 alpha = mat.constant_alpha;
					d = (alpha != 0 ? (double)alpha : 128.0) / 255.0;
				}

				if(pf.version < 7 && (flags & 0x0080) && (flags & 0x0001))
					d = std::min(d, 0.5);

				bool brightness_blend = (pf.version < 10 && (flags & 0x8000))
					|| (pf.version >= 10 && pf.mat_to_tex_idx[m] >= 0
						&& (pf.textures[pf.mat_to_tex_idx[m]].hdr_flags & 0x08));

				if(brightness_blend)
					d = std::min(d, 0.99);

				opacity[m] = (float)d;
			}

			return opacity;
		}

		//========================================================================
		// Register one Material per pf.materials entry
		//========================================================================
		void build_materials(
			const Parsed3di& pf,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			std::vector<float> mat_opacity = build_material_opacity(pf);

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

				// blend_tint = material uses flat_rgb as a per-material tint
				// applied to the texture at draw time.
				bool flat_fill  = (!pf.textures.empty() && !(src.flags & 0x8001));
				bool blend_tint = ((src.flags & 0x8000) && !(src.flags & 0x0001));
				double d_shade   = (pf.version == 5 && (src.flags & 0x0040)) ? (1.0 - 16.0 / 255.0) : 1.0;

				if(flat_fill || blend_tint) {
					mat.color[0] = (float)(src.flat_rgb[0] / 255.0 * d_shade);
					mat.color[1] = (float)(src.flat_rgb[1] / 255.0 * d_shade);
					mat.color[2] = (float)(src.flat_rgb[2] / 255.0 * d_shade);
				} else {
					float v = (float)(0.8 * d_shade);
					mat.color[0] = mat.color[1] = mat.color[2] = v;
				}

				mat.opacity = mat_opacity[i];

				int tex_idx = pf.mat_to_tex_idx[i];

				if(tex_idx >= 0 && (size_t)tex_idx < ctx.textures.filenames.size()) {

					std::string filename = ctx.textures.filenames[tex_idx];

					// v<10 fallback: write_all couldn't writes a file (excluded /
					// missing data) but the material still needs a name
					if(filename.empty() && pf.version < 10) {

						const Texture& tex = pf.textures[tex_idx];

						std::string base = tex.name.empty() ? ("tex_" + std::to_string(tex_idx)) : tex.name;
						bool animated = (tex.width > 0 && tex.height > 0 && tex.data_size > (u32)tex.width * tex.height);

						std::string ext = ctx.opts.raw ? ".pcx" : ".tga";

						filename = animated ? (base + "_f0" + ext) : (base + ext);

					}

					if(!ctx.opts.raw && io::has_ext_ci(filename, ".pcx"))
						filename = io::strip_ext(filename) + ".tga";

					if(!filename.empty()) {
						mat.texture = filename;

						if(io::extension(filename) == ".tga")
							mat.alpha = filename;

					}
				}
				// TEXTURED but no binding. Dark-gray Kd so they're
				// visibly distinct from flat-fill materials
				else if(src.flags & 0x0001) {
					mat.color[0] = mat.color[1] = mat.color[2] = 0.3f;
				}

				model.materials.push_back(mat);
			}
		}

		//========================================================================
		// Build meshes, one per pf.mesh_count
		//========================================================================
		void build_meshes(
			const Parsed3di& pf,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			model.meshes.resize(pf.mesh_count);
			for(u32 mesh_idx = 0; mesh_idx < pf.mesh_count; ++mesh_idx)
				model.meshes[mesh_idx].group_name = "mesh_" + std::to_string(mesh_idx);

			std::vector<std::map<i32, u32>> v_remap(pf.mesh_count);
			std::vector<std::map<i32, u32>> n_remap(pf.mesh_count);

			std::vector<std::array<double, 2>> mat_uv_scale(pf.materials.size(), {1.0, 1.0});
			for(size_t m = 0; m < pf.materials.size(); ++m) {
				if(pf.materials[m].uv_scale_u != 0.0f) mat_uv_scale[m][0] = pf.materials[m].uv_scale_u;
				if(pf.materials[m].uv_scale_v != 0.0f) mat_uv_scale[m][1] = pf.materials[m].uv_scale_v;
			}

			auto add_position = [&](u32 mesh_idx, i32 src_vi) -> u32 {
				auto it = v_remap[mesh_idx].find(src_vi);
				if(it != v_remap[mesh_idx].end())
					return it->second;

				u32 idx = (u32)model.meshes[mesh_idx].positions.size();
				const Vertex& vert = pf.vertices[src_vi];

				model.meshes[mesh_idx].positions.push_back({(float)vert.x, (float)vert.y, (float)vert.z});
				v_remap[mesh_idx][src_vi] = idx;
				return idx;
			};

			auto add_normal = [&](u32 mesh_idx, i32 src_ni) -> u32 {
				auto it = n_remap[mesh_idx].find(src_ni);
				if(it != n_remap[mesh_idx].end())
					return it->second;

				u32 idx = (u32)model.meshes[mesh_idx].normals.size();
				const Normal& norm = pf.normals[src_ni];

				model.meshes[mesh_idx].normals.push_back({
					norm.nx / NORM_INT_SCALE,
					norm.ny / NORM_INT_SCALE,
					norm.nz / NORM_INT_SCALE
				});
				n_remap[mesh_idx][src_ni] = idx;
				return idx;
			};

			for(u32 tri_idx = 0; tri_idx < (u32)pf.triangles.size(); ++tri_idx) {

				if(tri_idx < ctx.geometry_exclusions.size() && ctx.geometry_exclusions[tri_idx])
					continue;

				const Triangle& tri = pf.triangles[tri_idx];

				if(tri.mat_idx >= (u32)pf.materials.size())
					continue;

				if(tri.vi[0] == tri.vi[1] || tri.vi[1] == tri.vi[2] || tri.vi[0] == tri.vi[2])
					continue;

				if((u32)tri.vi[0] >= (u32)pf.vertices.size()
				|| (u32)tri.vi[1] >= (u32)pf.vertices.size()
				|| (u32)tri.vi[2] >= (u32)pf.vertices.size())
					continue;

				u32 mesh_idx = pf.vertices[tri.vi[0]].mesh_index;

				if(mesh_idx >= pf.mesh_count)
					continue;

				auto& mesh = model.meshes[mesh_idx];
				const double scale_u = mat_uv_scale[tri.mat_idx][0];
				const double scale_v = mat_uv_scale[tri.mat_idx][1];

				u32 pos_i[3]  = { add_position(mesh_idx, tri.vi[0]), add_position(mesh_idx, tri.vi[1]), add_position(mesh_idx, tri.vi[2]) };
				u32 norm_i[3] = { add_normal(mesh_idx, tri.ni[0]),   add_normal(mesh_idx, tri.ni[1]),   add_normal(mesh_idx, tri.ni[2])   };

				u32 uv_base = (u32)mesh.uvs.size();
				for(u32 c = 0; c < 3; ++c) {
					mesh.uvs.push_back({
						(float)((tri.u[c] / (double)FACE_UV_SCALE) * scale_u),
						(float)(1.0 - (tri.v[c] / (double)FACE_UV_SCALE) * scale_v)
					});
				}

				model::Face face_out = {};

				for(u32 c = 0; c < 3; ++c) {
					face_out.v[c] = {
						(i32)pos_i[c],
						(i32)norm_i[c],
						(i32)(uv_base + c)
					};
				}

				face_out.material = tri.mat_idx;
				mesh.faces.push_back(face_out);
			}
		}

		//========================================================================
		// Build collision mesh from BSP volumes + planes
		//========================================================================
		bool build_collision(const Parsed3di& pf, model::Mesh& out) {

			out.group_name = pf.model_name + "_collision";

			if(pf.volumes.empty() || pf.planes.empty())
				return false;

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

			return !out.faces.empty();
		}

		//========================================================================
		// Convert decoded hardpoints to IR. Engine /256 fixed-point conversion.
		//========================================================================
		bool build_hardpoints(const Parsed3di& pf, model::Model& model) {

			for(const auto& src : pf.hardpoints) {
				model::Hardpoint h;
				h.name = src.name;
				h.pos  = {
					src.pos[0] / HPNT_INT_SCALE,
					src.pos[1] / HPNT_INT_SCALE,
					src.pos[2] / HPNT_INT_SCALE,
				};
				model.hardpoints.push_back(std::move(h));
			}

			return !model.hardpoints.empty();
		}

		//========================================================================
		// Materials + per-mesh geometry
		//========================================================================
		void build_model(
			const Parsed3di& pf,
			size_t /*lod_idx*/,
			const model::BuildContext& ctx,
			model::Model& model
		) {

			model.source_name = std::format("NovaLogic {} (3DI v{})", pf.model_name, pf.version);
			model.format_tag  = "3di_v" + std::to_string(pf.version);

			build_materials(pf, ctx, model);
			build_meshes(pf, ctx, model);
		}

		//========================================================================
		// Scope version byte > TagScope
		//========================================================================
		filters::Scope resolve_filter_scope(u8 version) {

			if(version <= 6)
				return filters::Scope::T3di_v2_v5;

			if(version == 7)
				return filters::Scope::T3di_v7;

			if(version <= 9)
				return filters::Scope::T3di_v8;

			return filters::Scope::T3di_v10;
		}

		//========================================================================
		// Texture exclusions
		//========================================================================
		model::TextureExclusions collect_texture_exclusions(
			const Parsed3di& pf,
			filters::Scope scope,
			const model::ConvertOptions& opts
		) {

			model::TextureExclusions tex_ex;
			tex_ex.drop_texture.assign(pf.textures.size(), 0);

			if(opts.raw)
				return tex_ex;

			if(!opts.effects) {
				for(size_t i = 0; i < pf.textures.size(); ++i) {
					const std::string& name = pf.textures[i].name;
					if(filters::is_effect(scope, name)
					|| filters::is_shadow(scope, name))
						tex_ex.drop_texture[i] = 1;
				}
			}

			return tex_ex;
		}

		//========================================================================
		// Geometry exclusions (per-LOD)
		// is_nodraw       - HIDDEN flag / unresolvable v10 texture
		// is_tex_excluded - material's bound texture was tagged Shadow or Effect
		//========================================================================
		std::vector<u8> collect_geometry_exclusions(
			const Parsed3di& pf,
			size_t /*lod_idx*/,
			const model::TextureExclusions& tex_ex,
			filters::Scope scope,
			const model::ConvertOptions& opts
		) {

			std::vector<u8> drop((u32)pf.triangles.size(), 0);

			if(opts.raw)
				return drop;

			bool effect_only_model = false;

			if(!opts.effects) {

				u32 visible = 0;

				for(u32 i = 0; i < (u32)pf.triangles.size(); ++i) {
					const Triangle& tri = pf.triangles[i];

					if(tri.mat_idx >= (u32)pf.materials.size())
						continue;

					if(is_nodraw(pf, tri.mat_idx))
						continue;

					if(is_tex_excluded(pf, tex_ex, scope, tri.mat_idx))
						continue;

					++visible;
				}

				if(visible == 0 && (u32)pf.triangles.size() > 0)
					effect_only_model = true;
			}

			for(u32 i = 0; i < (u32)pf.triangles.size(); ++i) {

				const Triangle& tri = pf.triangles[i];

				if(tri.mat_idx >= (u32)pf.materials.size() || is_nodraw(pf, tri.mat_idx)) {
					drop[i] = 1;
					continue;
				}

				if(!opts.effects && !effect_only_model && is_tex_excluded(pf, tex_ex, scope, tri.mat_idx))
					drop[i] = 1;

			}

			return drop;
		}

		//========================================================================
		// Per texture alpha context to store the resolved alpha triggers for each texture
		//========================================================================
		struct AlphaContext {
			bool flag_8000     = false; // any binding material has flag 0x8000
			bool flag_0004     = false; // any binding material has flag 0x0004 (DF1 alt-CLUT decal)
			bool flag_a50      = false; // textured mat 0x0080 routes to fixed-50%-blend (ALPHA_50 v5)
			std::array<u8, 3> tint = {  // v10 flat_rgb tint for brightness-blended textures (RGB=Tint)
				0xFF, // r
				0xFF, // g
				0xFF  // b
			};
		};
		  
		bool is_grayscale(const Texture& tex) {

			u8 used[PALETTE_ENTRY_COUNT] = {};

			for(u8 p : tex.pixels)
				used[p] = 1;

			for(u32 k = 0; k < PALETTE_ENTRY_COUNT; ++k) {

				if(!used[k])
					continue;

				const u8* entry = tex.palette + k * PALETTE_ENTRY_SIZE;

				if(entry[0] != entry[1] || entry[1] != entry[2])
					return false;

			}

			return true;
		}

		std::vector<AlphaContext> build_alpha_context(const Parsed3di& pf) {

			const u32 tex_count = (u32)pf.textures.size();
			std::vector<AlphaContext> ctx(tex_count);
			std::vector<u8> tint_color(tex_count, 0);

			for(u32 m = 0; m < (u32)pf.materials.size(); ++m) {

				int t = pf.mat_to_tex_idx[m];
				if(t < 0 || (u32)t >= tex_count)
					continue;

				u32 flags = pf.materials[m].flags;

				if(flags & 0x8000) 
					ctx[t].flag_8000 = true;

				if(flags & 0x0004) 
					ctx[t].flag_0004 = true;

				if((flags & 0x0080) && (flags & 0x0001)) 
					ctx[t].flag_a50 = true;

				if(!tint_color[t]) {

					ctx[t].tint = { 
						pf.materials[m].flat_rgb[0], 
						pf.materials[m].flat_rgb[1], 
						pf.materials[m].flat_rgb[2] 
					};

					tint_color[t] = 1;
				}
			}

			return ctx;
		}

		//========================================================================
		// AlphaModes:
		//   Opaque    - default (no alpha)
		//   IndexZero - palette idx 0 transparent (v<10 hdr_flags 0x20; v7 mat 0x8000; v<7 ALPHA_50)
		//   IndexAlpha- v<7 mat 0x8000/0x0004 brightness, or v8/v9 mat 0x8000 (RGB=Black)
		//   Explicit  - v8+ mat 0x8000 + 2bpp data (idx + alpha bytes)
		//   Luminance - v10 hdr_flags 0x08, RGB=Tint (first-binding mat flat_rgb)
		//   Mask      - v10 name2 set, alpha = sibling tex brightness
		//   Preserve  - v10 default (preserve external 32bpp TGA alpha)
		//========================================================================
		texture::TextureSpec resolve_alpha_mode(const Parsed3di& pf, u32 tex_idx, const AlphaContext& ctx) {

			texture::TextureSpec spec;
			const Texture& tex = pf.textures[tex_idx];

			// v10 32bpp + Luminance(0x08) / Mask(name2)
			if(pf.version >= 10) {

				spec.mode = texture::AlphaMode::Preserve;

				if(tex.hdr_flags & 0x08) {
					spec.mode    = texture::AlphaMode::Luminance;
					spec.rgb     = texture::RgbSource::Tint;
					spec.tint_r  = ctx.tint[0];
					spec.tint_g  = ctx.tint[1];
					spec.tint_b  = ctx.tint[2];
					spec.bleed   = false;
					spec.opacity = 0.99f;
				}
				else if(!tex.mask_name.empty()) {
					spec.mode    = texture::AlphaMode::Mask;
					spec.opacity = 0.99f;
				}
				return spec;
			}

			bool valid_pixels = !tex.pixels.empty() && tex.pixels.size() >= (size_t)tex.width * tex.height;

			if(valid_pixels) {

				bool flag_8000 = ctx.flag_8000;
				bool flag_0004 = !ctx.flag_8000 && ctx.flag_0004 && is_grayscale(tex);

				if(flag_8000 || flag_0004) {

					if(pf.version < 7) {
						spec.mode    = texture::AlphaMode::IndexAlpha;
						spec.rgb     = texture::RgbSource::Black;
						spec.bleed   = false;
						spec.opacity = 0.99f;
						return spec;
					}

					if(flag_8000 && tex.data_size == (u32)tex.width * tex.height * 2) {
						spec.mode = texture::AlphaMode::Explicit;
						return spec;
					}
					if(flag_8000 && pf.version >= 8) {
						spec.mode    = texture::AlphaMode::IndexAlpha;
						spec.rgb     = texture::RgbSource::Black;
						spec.bleed   = false;
						spec.opacity = 0.99f;
						return spec;
					}
					if(flag_8000) {
						spec.mode = texture::AlphaMode::IndexZero;
						return spec;
					}
				}
			}

			// textured mat 0x0080 routes to fixed-50%-blend (ALPHA_50 v5)
			if(pf.version < 7 && ctx.flag_a50) {
				spec.mode = texture::AlphaMode::IndexZero;
				return spec;
			}

			// hdr_flags 0x20 > IndexZero
			if(tex.hdr_flags & 0x20)
				spec.mode = texture::AlphaMode::IndexZero;

			return spec;
		}

		//========================================================================
		// Texture Spec vector builder
		//========================================================================
		std::vector<texture::TextureSpec> texture_specs(
			const Parsed3di& pf,
			const model::TextureExclusions&,
			filters::Scope,
			const model::ConvertOptions& opts
		) {

			const u32 tex_count = (u32)pf.textures.size();

			if(opts.raw)
				return std::vector<texture::TextureSpec>(tex_count);

			std::vector<AlphaContext> ctx = build_alpha_context(pf);
			std::vector<texture::TextureSpec> specs(tex_count);

			for(u32 i = 0; i < tex_count; ++i)
				specs[i] = resolve_alpha_mode(pf, i, ctx[i]);

			return specs;
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

		Parsed3di pf;
		if(!parse_3di(*file, pf, opts.lod))
			return false;

		std::string format_label = std::format("3DI v{}", pf.version);

		nlog::announce(input_basename, format_label,
			std::format("model={}, lod={}/{}", pf.model_name, pf.lod_picked, pf.lod_count));

		io::make_dirs(opts.out_dir);

		filters::Scope scope = resolve_filter_scope(pf.version);
		model::TextureExclusions tex_ex = collect_texture_exclusions(pf, scope, opts);
		auto specs = texture_specs(pf, tex_ex, scope, opts);

		auto tex = build_textures(pf, input_dir, specs, tex_ex);

		if(tex.missing > 0)
			nlog::warn("%d texture files not found", tex.missing);

		auto dedup_keys = resolve_dedup_keys(pf);
		auto write_result = texture::write_all(
			opts.out_dir,
			std::span<const texture::Image>{tex.textures},
			opts.raw,
			std::span<const texture::DedupKey>{dedup_keys}
		);

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
