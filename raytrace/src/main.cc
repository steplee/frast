
#include "trace.h"
#include "decode/decode.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
namespace {
	void write_image(const std::string& name, const Buffer<uint8_t>& buf) {
		assert(buf.w > 0);
		assert(buf.h > 0);
		int channels = 1;
		stbi_write_jpg(name.c_str(), buf.w, buf.h, channels, buf.ptr, 90);
	}

	void write_image(const std::string& name, const Buffer<byte3>& buf) {
		assert(buf.w > 0);
		assert(buf.h > 0);
		int channels = 3;
		stbi_write_jpg(name.c_str(), buf.w, buf.h, channels, buf.ptr, 90);
	}

	template <class T> constexpr T square(const T& x) { return x*x; }

	void createFakeTile(std::vector<Tile>& outs) {
		Tile out;

		out.verts.allocateCpu(3);
		out.indices.allocateCpu(3);
		float S = .97f;
		out.verts(0).x = -.5 * S;
		out.verts(0).y = -.5 * S;
		out.verts(0).z =  .0;
		out.verts(1).x =  .5 * S;
		out.verts(1).y = -.5 * S;
		out.verts(1).z =  .0;
		out.verts(2).x =  .0;
		out.verts(2).y =  .5 * S;
		out.verts(2).z =  -.1;
		// out.verts(2).z =  -.0;

		out.indices(0) = 0;
		out.indices(1) = 2;
		out.indices(2) = 1;

		out.verts(0).u = 0;
		out.verts(0).v = 0;
		out.verts(1).u = 1;
		out.verts(1).v = 0;
		out.verts(2).u = .5;
		out.verts(2).v = 1;

		int w=256, h=256;
		out.pixels.allocateCpu(h,w);
		for (int y=0; y<h; y++)
		for (int x=0; x<w; x++) {
			float r = square(sin(x * .05f));
			float g = square(sin(y * .09f));
			out.pixels(y,x).r = static_cast<uint8_t>(r * 255.9f);
			out.pixels(y,x).g = static_cast<uint8_t>(g * 255.9f);
			out.pixels(y,x).b = static_cast<uint8_t>(0);
			out.pixels(y,x).rough1 = 244;
		}

		outs.push_back(std::move(out));
	}
}


int main() {

	Raytracer rt;

	std::vector<Tile> tiles;

#if 0
	createFakeTile(tiles);
	rt.setGeometry(std::move(tiles));
#else
	auto info = load_to(tiles, "/data/gearth/naipAoisWgs/node/205371506163424164");
	// auto info = load_to(tiles, "/data/gearth/naipAoisWgs/node/21427063616252");
	// auto info = load_to(tiles, "/data/gearth/naipAoisWgs/node/205370525342524");
	rt.setGeometry(std::move(tiles));
	rt.setViewMatrix(info.recommendView.cast<float>());
#endif


	auto res = rt.render();

	write_image("out.jpg", res.rgb);

	return 0;
}
