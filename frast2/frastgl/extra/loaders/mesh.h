#pragma once

#include <vector>
#include <cstdint>
#include <cassert>
#include <stdexcept>

#include <GL/glew.h>
#include <GL/gl.h>

#include "frast2/frastgl/core/shader.h"

/*
 * I'm not going to build a fully automatic shader/object-model.
 * E.g. normally you'd use glGet to get attrib/uniform names, match to what user provided in vertex data,
 * etc.
 *
 * Instead, use simple rules.
 * Support any choice of (verts color uv normal) attributes, but keep them in that order, with increasing location.
 * Support mvp as first uniform, sampler2d binding as second
 *
 */

namespace frast {

	class RenderState;

	struct VertexLayout {

		uint8_t dimsPos = 3;
		uint8_t dimsColor = 0;
		uint8_t dimsUv = 0;
		uint8_t dimsNormal = 0;

		GLuint typePos = GL_FLOAT;
		GLuint typeColor = GL_FLOAT;
		GLuint typeUv = GL_FLOAT;
		GLuint typeNormal = GL_FLOAT;

		inline constexpr int scalarSize(const GLuint& t) const {
			switch (t) {
				case GL_BYTE:
				case GL_UNSIGNED_BYTE: return 1;
				case GL_SHORT:
				case GL_UNSIGNED_SHORT: return 2;
				case GL_UNSIGNED_INT:
				case GL_FLOAT: return 4;
			}
			return 0;
			// throw std::runtime_error("invalid arg to scalarSize");
		}

		inline int byteSize() const {
			int size = 0;
			size += dimsPos * scalarSize(typePos);
			size += dimsColor * scalarSize(typeColor);
			size += dimsUv * scalarSize(typeUv);
			size += dimsNormal * scalarSize(typeNormal);
			return size;
		}
	};

struct MeshData {

	// std::vector<uint8_t> vertsCpuByte;
	// std::vector<uint8_t> indsCpuByte;
	uint32_t nInds=0, nVerts=0;
	GLuint vbo = 0;
	GLuint ibo = 0;
	GLuint texs[3] = {0};

	GLuint indsType;

	GLuint mode = GL_TRIANGLES;


	VertexLayout vertexLayout;

	void uploadVerts(std::vector<uint8_t>&& vertsCpuByte, VertexLayout v);
	void uploadInds(std::vector<uint8_t>&& indsCpuByte, GLuint indType);
	void uploadTex(uint8_t* data, int w, int h, GLuint format, int texSlot=0);

	void renderMesh(const RenderState& rs);

};

struct Object {
	GLuint program=0;

	MeshData mesh;
	std::vector<Object> children;

	alignas(16) double model[16];

	// Default initialize model matrix to eye4
	Object();

	// does push stack
	void renderRecursive(const RenderState& rs);

	std::string name;

	void setTransform(const double model[16]);
	void setTransform(const double t[3], const double q[4], double s=1.);

	private:


	void render(RenderState& rs);

	// does not push stack (just multiplies by model matrix)
	// called for children, to not require a large matrix stack.
	void renderRecursiveDoNotPush(RenderState& rs);

};

void get_basicMeshWithTex_shader(Shader& out);
void get_basicMeshNoTex_shader(Shader& out);





}

