#pragma once

#include "../util/types.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace nova3di::model {

	struct Vertex { 
		double x;  
		double y;  
		double z; 
	};
	
	struct Normal { 
		double x;  
		double y;  
		double z; 
	};

	struct UV     {
		double u;
		double v;
	};

	struct Plane {
		float nx;
		float ny;
		float nz;  
		float d;
	};

	struct CollisionVolume {
		u8                   type;
		std::array<float, 6> bbox;
		u32                  plane_start;
		u32                  plane_count;
	};

	struct FaceVertex {
		i32 pos;
		i32 normal;
		i32 uv;
	};

	struct Face {
		FaceVertex v[3];
		u32   material;
		u16   flags;
	};

	struct Mesh {
		std::vector<Vertex> positions;
		std::vector<Normal> normals;
		std::vector<UV>     uvs;
		std::vector<Face>   faces;
		std::string         group_name;
	};

	struct Material {
		std::string name;
		std::string texture;
		std::string alpha;
		std::string emission;

		float color[3] = {1.0f, 1.0f, 1.0f};
		float opacity = 1.0f;

		std::map<std::string, std::string> metadata;  // #nova:... metadata
	};

	struct Hardpoint {
		std::string name;
		Vertex      pos;
	};

	struct Model {
		std::vector<Mesh>      meshes;
		std::vector<Material>  materials;
		std::vector<Hardpoint> hardpoints;
		Mesh                   collision;
		std::string            source_name;
		std::string            format_tag;
		std::map<std::string, std::string> metadata;  // #nova:... metadata
	};

}
