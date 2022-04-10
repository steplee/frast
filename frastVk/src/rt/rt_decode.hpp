#pragma once

#include "protos/rocktree.pb.h"

#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include "decode/stb_image.h"

namespace rtpb = ::geo_globetrotter_proto_rocktree;

namespace rt {

/*
 * These functions come from my python implementation of the original javascript code.
 * I just sort of translated it, so not particularly clean.
 */

// Will advance the input pointer!
inline int unpackVarInt(uint8_t*& b) {
	uint8_t z = *b;
	uint64_t c = 0;
	uint8_t d = 1;
	while(1) {
		uint8_t e = *b;
		b++;
		c = c + (e&0x7f) * d;
		d <<= 7;
		if ((e & 0x80) == 0) return c;
	}
}


inline bool decode_node_to_tile(
		std::ifstream &ifs,
		DecodedTileData& dtd) {

	rtpb::NodeData nd;
	if (!nd.ParseFromIstream(&ifs)) {
		fmt::print(" - [#decode_node_to_tile] ERROR: failed to parse istream!\n");
		return true;
	}

	if (nd.meshes_size() != 1) {
		fmt::print(" - Warning: there are {} meshes. Only the first will be used (TODO)\n",nd.meshes_size());
		assert(nd.meshes_size() >= 1);
	}
	// assert(nd.meshes_size() == 1);

	auto& mesh = nd.meshes(0);

	// We are goind to pack the vertex buffer such as <pos, uv, normal>
	// pos/uv is 1 byte, uv is 2, so that is 3+2*2+3 = 8 bytes total, per vertex
	int nv = mesh.vertices().length() / 3;
	dtd.vert_buffer_cpu.resize(nv);

	// Decode verts
	const std::string& verts = mesh.vertices();
	uint8_t * v_data = (uint8_t*) verts.data();
	for (int i=0; i<3; i++) {
		uint8_t acc = 0;
		for (int j=0; j<nv; j++) {
			acc = acc + v_data[i*nv+j];
			// (&dtd.vert_buffer_cpu[j].x)[i] = acc;
			// NOTE TODO XXX SWAP Y Z
			if (i==0) dtd.vert_buffer_cpu[j].x = acc;
			if (i==1) dtd.vert_buffer_cpu[j].y = acc;
			if (i==2) dtd.vert_buffer_cpu[j].z = acc;
		}
	}
	// for (int i=0; i<nv; i++) fmt::print(" - vert {} {} {}\n", dtd.vert_buffer_cpu[i].x, dtd.vert_buffer_cpu[i].y, dtd.vert_buffer_cpu[i].z);

	// Uvs
	/*
    n = (len(bites) - 2) // 4
    uvs = np.zeros((n, 2), dtype=np.uint8)
    uv_mod = 1 + np.frombuffer(bites[:4],dtype=np.uint16)
    data = np.frombuffer(bites[4:], dtype=np.uint8)

    if 0:
        uvs = np.stack((
            data[   :n  ] + (data[n*2:n*3] << 8),
            data[n*1:n*2] + (data[n*3:n*4] << 8)), -1) % uv_mod.reshape(1,2).astype(np.uint16)
        uvs = uvs.cumsum(0).astype(np.uint16)
    else:
        uvs = np.zeros((n,2),dtype=np.uint16)
        u, v = 0, 0
        for i in range(n):
            u = (u + data[i  ] + (data[i*3]<<8)) % uv_mod[0]
            v = (v + data[i*2] + (data[i*4]<<8)) % uv_mod[1]
            uvs[i] = u, v

    uvScale = 1.0 / uv_mod.astype(np.float32)
    uvOff = np.array((.5,.5),dtype=np.float32)
    return uvs, uvScale, uvOff



	void unpackTexCoords(std::string packed, uint8_t* vertices, int vertices_len, Vector2f &uv_offset, Vector2f &uv_scale) {	
		auto data = (uint8_t*)packed.data();
		auto count = vertices_len / sizeof(vertex_t);
		assert(count * 4 == (packed.size() - 4) && packed.size() >= 4);
		auto u_mod = 1 + *(uint16_t *)(data + 0);
		auto v_mod = 1 + *(uint16_t *)(data + 2);
		data += 4;
		auto vtx = (vertex_t*)vertices;
		auto u = 0, v = 0;
		for (auto i = 0; i < count; i++) {
			vtx[i].u = u = (u + data[count * 0 + i] + (data[count * 2 + i] << 8)) % u_mod;
			vtx[i].v = v = (v + data[count * 1 + i] + (data[count * 3 + i] << 8)) % v_mod;
		}
		
		uv_offset[0] = 0.5;
		uv_offset[1] = 0.5;
		uv_scale[0] = 1.0 / u_mod;
		uv_scale[1] = 1.0 / v_mod;
	}
	*/

	uint8_t* data = (uint8_t*) mesh.texture_coordinates().data();
	auto u_mod = 1 + *(uint16_t*)(data+0);
	auto v_mod = 1 + *(uint16_t*)(data+2);
	// fmt::print(" - head uv bytes {} {} {} {}\n", data[0], data[1], data[2], data[3]);
	data += 4;
	auto u=0,v=0;
	for (int i=0; i<nv; i++) {
		u = (u + data[nv*0 + i] + (data[nv*2 + i] << 8)) % u_mod;
		v = (v + data[nv*1 + i] + (data[nv*3 + i] << 8)) % v_mod;
		dtd.vert_buffer_cpu[i].u = u;
		dtd.vert_buffer_cpu[i].v = v;
	}


	dtd.uvOffset[0] = 0.5;
	dtd.uvOffset[1] = 0.5;
	dtd.uvScale[0] = 1.0 / u_mod;
	dtd.uvScale[1] = 1.0 / v_mod;

	if (mesh.uv_offset_and_scale_size() == 4) {
		dtd.uvOffset[0] = mesh.uv_offset_and_scale(0);
		dtd.uvOffset[1] = mesh.uv_offset_and_scale(1);
		dtd.uvScale[0] = mesh.uv_offset_and_scale(2);
		dtd.uvScale[1] = mesh.uv_offset_and_scale(3);
	} else {
		dtd.uvOffset[1] -= 1.0 / dtd.uvScale[1];
		dtd.uvScale[1] *= -1.0;
		// dtd.uvOffset[0] = -16384;
		// dtd.uvOffset[1] = -16384;
		// dtd.uvScale[0] = .5 / 16384;
		// dtd.uvScale[1] = .5 / 16384;
	}

	// fmt::print(" - some uvs: ");
	// for (int i=0; i<nv; i++) { fmt::print("{} {},", dtd.vert_buffer_cpu[i].u, dtd.vert_buffer_cpu[i].v); }
	// fmt::print("\n - and uv scale {} {} off {} {}\n", dtd.uvScale[0], dtd.uvScale[1], dtd.uvOffset[0], dtd.uvOffset[1]);
	if (nv >= RtCfg::maxVerts) fmt::print(" - WARNING: nv {} / {}\n", nv, RtCfg::maxVerts);
	// fmt::print(" - uv scale {} {} off {} {}\n", dtd.uvScale[0], dtd.uvScale[1], dtd.uvOffset[0], dtd.uvOffset[1]);




	// Normals
	auto& ns_bites = mesh.normals();
	/*
		def decode_for_normals(bites):
			n = np.frombuffer(bites[:2], dtype=np.uint16)[0]
			s = np.frombuffer(bites[2:3], dtype=np.uint8)[0]
			data = np.frombuffer(bites[3:], dtype=np.uint8)
			out = np.zeros((n,3),dtype=np.uint8)

			def f1(v,s):
				if s <= 4:
					return (v << s) + (v & (1<<s) - 1)
				if s <= 6:
					r = 8 - s
					return (v << s) + (v << l >> r) + (v << l >> r >> r) + (v << l >> r >> r >> r)
				return -(v & 1)

			def f2(c):
				return np.clip(np.round(c), 0, 255).astype(np.uint8)

			for i in range(n):
				a,f = f1(data[i], s) / 255., f1(data[n+i], s) / 255.
				b = a
				c = f
				g = b + c
				h = b - c
				sign = 1

				if not (.5 <= g and 1.5 >= g and -.5 <= h and .5 >= h):
					sign = -1
					if .5 >= g: b,c = .5 - f, .5 - a
					else:
						if 1.5 <= g: b,c = 1.5 - f, 1.5 - a
						else:
							if -.5 >= h: b, c = f - .5, a + .5
							else: b,c = f + .5, a - .5
					g,h = b + c, b - c

				a = min(min(2*g-1, 3-2*g), min(2*h+1, 1-2*h)) * sign
				b,c = 2*b-1, 2*c-1
				m = 127 / np.sqrt(a*a + b*b + c*c)
				out[i,0] = f2(m*a+127)
				out[i,1] = f2(m*b+127)
				out[i,2] = f2(m*c+127)
			return out
	*/
	auto &fns = dtd.tmp_buffer;


	/*
# If @bites & @forNormals are None, must provide @n to initialize with empty normals
		def decode_normal_inds(bites, forNormals, n=0):
			if (bites is not None) and (forNormals is not None):
				data = np.frombuffer(bites, dtype=np.uint8)
				n = data.size
				out = np.zeros((n,4),dtype=np.uint8)
				for i in range(n):
					j = data[i] + (data[n+i]<<8)
					out[i,0:3] = forNormals[j,0:3]
					out[i,3] = 0
			else:
				out = np.ones((n,4),dtype=np.uint8)
				out[:,3] = 0
			return out
	*/


	// Inds
	auto& indices = mesh.indices();
	uint8_t* ptr = (uint8_t*) indices.data();
	uint32_t strip_len = unpackVarInt(ptr);
	dtd.ind_buffer_cpu.resize(strip_len);
	int j=0, zeros=0;
	while (j<strip_len) {
		int v = unpackVarInt(ptr);

		dtd.ind_buffer_cpu[j] = (uint16_t)(zeros - v);

		if (dtd.ind_buffer_cpu[j] >= nv) {
			fmt::print(" - ind {}/{} was invalid, pointed to vert {} / {}\n",j,strip_len, dtd.ind_buffer_cpu[j],nv);
		}

		if (v==0) zeros += 1;
		j += 1;
	}
	// check what was written
	// fmt::print(" - n verts {}\n", nv);
	// for (int i=0; i<strip_len; i++) fmt::print(" - ind {}\n", dtd.ind_buffer_cpu[i]);


	// Texture
	dtd.texSize[0] = dtd.texSize[1] = dtd.texSize[2] = 0;
	if (mesh.texture_size() > 0) {
		auto& tex = mesh.texture(0);
		if (tex.format() != rtpb::Texture::JPG) {
			printf(" - texture had non-JPG format, which is not supported.\n");
		} else {
			int dc = 4;
			dtd.texSize[0] = tex.height();
			dtd.texSize[1] = tex.width();
			dtd.texSize[2] = dc;
			if (dtd.texSize[0] > RtCfg::maxTextureEdge)
				printf(" - texture had larger size then allowed : %u / %u\n", (uint32_t)dtd.texSize[0], (uint32_t)RtCfg::maxTextureEdge);
			if (dtd.texSize[1] > RtCfg::maxTextureEdge)
				printf(" - texture had larger size then allowed : %u / %u\n", (uint32_t)dtd.texSize[1], (uint32_t)RtCfg::maxTextureEdge);

			// TODO: Decode jpeg using STB lib
			// dtd.img_buffer_cpu.resize(dtd.texSize[0]*dtd.texSize[1]*dtd.texSize[2]);
			dtd.img_buffer_cpu.resize(dtd.texSize[0]*dtd.texSize[1]*dtd.texSize[2], 255);

			// STBIDEF stbi_uc *stbi_load_from_memory   (stbi_uc           const *buffer, int len   , int *x, int *y, int *channels_in_file, int desired_channels);
			int w,h,c;
			uint8_t* tmp = stbi_load_from_memory((const uint8_t*)tex.data(0).data(), tex.data(0).length(), &w,&h,&c, dc);
			if (tmp == nullptr) {
				fmt::print(" [decode] Warning: failed to decode jpeg of size {} {} {}!\n", dtd.texSize[0],dtd.texSize[1],dtd.texSize[2]);
			} else {
				memcpy(dtd.img_buffer_cpu.data(), tmp, w*h*dc);
				stbi_image_free(tmp);
				if (w!=tex.width() or h!=tex.height()) {
					fmt::print(" [decode] Warning: decoded size did not match pb size: {} {} vs {} {} {}\n", dtd.texSize[0], dtd.texSize[1], h,w);
				}
			}


			// Copy RGB -> RGBA
		}

	}

	for (int i=0; i<16; i++) dtd.modelMat[i] = nd.matrix_globe_from_mesh(i);

	// Signify that we don't have the data (it is in the bulk metadata)
	dtd.metersPerTexel = -1;

	return false;
}


}
