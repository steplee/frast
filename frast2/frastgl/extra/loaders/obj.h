#pragma once

#include "mesh.h"


namespace frast {

class ObjLoader {

	public:
	ObjLoader(const std::string& path);

	inline Object& getRoot() { return root; }

	private: Object root;
};




}
