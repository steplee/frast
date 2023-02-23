#include "mesh.h"
#include "frast2/frastgl/core/render_state.h"
#include "frast2/frastgl/shaders/basicMesh.h"
#include <fmt/core.h>

#include "../../utils/eigen.h"


namespace frast {

void MeshData::uploadInds(std::vector<uint8_t>&& indsCpuByte, GLuint il) {
	indsType = il;

	if (ibo == 0) glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indsCpuByte.size(), indsCpuByte.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	int indSize = il == GL_UNSIGNED_BYTE ? 1 : GL_UNSIGNED_SHORT ? 2 : 4;
	nInds = indsCpuByte.size() / indSize;
}

void MeshData::uploadVerts(std::vector<uint8_t>&& vertsCpuByte, VertexLayout vl) {
	vertexLayout = vl;
	assert(vertsCpuByte.size() % vl.byteSize() == 0);

	if (vbo == 0) glGenBuffers(1, &vbo);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	// TODO: Use subbufferdata
	glBufferData(GL_ARRAY_BUFFER, vertsCpuByte.size(), vertsCpuByte.data(), GL_STATIC_DRAW);
	nVerts = vertsCpuByte.size() / vl.byteSize();
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// if (keepCpuBuffer) {
		// this->vertsCpuByte = std::move(vertsCpuByte);
		// this->indsCpuByte = std::move(indsCpuByte);
	// }
}

void MeshData::uploadTex(uint8_t* data, int w, int h, GLuint format, int texSlot) {
	assert (texSlot < 3);
	GLuint& tex = texs[texSlot];

	if (tex == 0) {
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	// TODO: Use subimage if same size
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w,h, 0, format, GL_UNSIGNED_BYTE, data);
	glBindTexture(GL_TEXTURE_2D, 0);
}



// TODO: Handle shaders (textures/uniforms)
void MeshData::renderMesh(const RenderState& rs) {
	// fmt::print(" - render mesh {} inds\n", nInds);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	if (ibo) {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	}

	if (texs[0]) {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texs[0]);
	}

	const auto& vl = vertexLayout;
	uint64_t idx = 0, offset = 0;
	if (vl.dimsPos) glVertexAttribPointer(idx++, vl.dimsPos, vl.typePos, GL_FALSE, vl.byteSize(), (void*)offset), offset += vl.dimsPos*4;
	if (vl.dimsColor) glVertexAttribPointer(idx++, vl.dimsColor, vl.typeColor, GL_FALSE, vl.byteSize(), (void*)offset), offset += vl.dimsColor*4;
	if (vl.dimsUv) glVertexAttribPointer(idx++, vl.dimsUv, vl.typeUv, GL_FALSE, vl.byteSize(), (void*)offset), offset += vl.dimsUv*4;
	if (vl.dimsNormal) glVertexAttribPointer(idx++, vl.dimsNormal, vl.typeNormal, GL_FALSE, vl.byteSize(), (void*)offset), offset += vl.dimsNormal*4;

	// fmt::print("vl: {} attribs, stride {}\n", idx, vl.byteSize());

	for (int i=0; i<idx; i++) glEnableVertexAttribArray(i);


	if (ibo) {
		glDrawElements(mode, nInds, indsType, 0);
	} else {
		glDrawArrays(mode, 0, nVerts);
	}

	for (int i=0; i<idx; i++) glDisableVertexAttribArray(i);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}









	Object::Object() {
		for (int i=0; i<16; i++) model[i] = (i % 5) == 0;
	}



	void Object::renderRecursive(const RenderState& rs_) {
		// rs.mstack.push(model, false);
		RenderState rs {rs_};
		rs.pushModel(model);
		render(rs);
		for (auto& child : children) child.renderRecursiveDoNotPush(rs);
	}

	void Object::renderRecursiveDoNotPush(RenderState& rs) {
		// rs.mstack.push(model, true);
		rs.pushModel(model);
		render(rs);
		for (auto& child : children) child.renderRecursiveDoNotPush(rs);
	}

	void Object::render(RenderState& rs) {
		if (program != 0) glUseProgram(program);

		/*
		float mvpf[16];
		rs.computeMvpf(mvpf);
		glUniformMatrix4fv(0, 1, true, mvpf); // mvp

		float zplus[3] = {
			(float)rs.viewInv()[0*4+2],
			(float)rs.viewInv()[1*4+2],
			(float)rs.viewInv()[2*4+2],
		};
		glUniform3fv(8,1,zplus);
		fmt::print(" - zplus {} {} {}\n", zplus[0], zplus[1], zplus[2]);
		*/

		float proj[16], mv[16];
		rs.copyProjf(proj);
		rs.copyModelViewf(mv);
		glUniformMatrix4fv(0, 1, true, proj);
		glUniformMatrix4fv(1, 1, true, mv);

		if (haveColor) {
			glUniform4fv(2, 1, color);
		}


		mesh.renderMesh(rs);
	}

	void Object::setColor(const float c[4]) {
		haveColor = true;
		for (int i=0; i<4; i++) color[i] = c[i];
	}
	void Object::setTransform(const double model_[16]) {
		using namespace Eigen;
		Map<Matrix<double,4,4,RowMajor>> M ( model );
		M = Map<const Matrix<double,4,4,RowMajor>> ( model_ );
	}
	void Object::setTransform(const double t[3], const double q[4], double s) {
		using namespace Eigen;
		Map<const Vector3d> tt{t};
		Map<const Quaterniond> qq{q};

		Map<Matrix<double,4,4,RowMajor>> M ( model );
		M.topLeftCorner<3,3>() = qq.toRotationMatrix();
		M.topRightCorner<3,1>() = tt;
		M.row(3) << 0,0,0,1./s;
	}




void get_basicMeshWithTex_shader(Shader& out) {
	out = std::move(Shader{basicMeshWithTex_vsrc, basicMeshWithTex_fsrc});
}
void get_basicMeshNoTex_shader(Shader& out) {
	out = std::move(Shader{basicMeshNoTex_vsrc, basicMeshNoTex_fsrc});
}

}
