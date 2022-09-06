#include "trace.h"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <fmt/ostream.h>

namespace {
	constexpr float _camera_z = 1.f;

	template <class T> constexpr T square(const T& x) { return x*x; }

	RowMatrix4f inv_isometry(Eigen::Ref<const RowMatrix4f> A) {
		RowMatrix4f B;
		B.topLeftCorner<3,3>() = A.topLeftCorner<3,3>().transpose();
		B.topRightCorner<3,1>() = A.topLeftCorner<3,3>().transpose() * -A.topRightCorner<3,1>();
		B.row(3) << 0,0,0,1;
		return B;
	}

	Vector3f compute_cell_normal( Ref<const Vector3f> a, Ref<const Vector3f> b, Ref<const Vector3f> c) {
		return (b-a).cross((c-a)).normalized();
		// return (b-a).normalized().cross((c-a).normalized()).normalized();
		// return (a-b).normalized().cross((c-a).normalized()).normalized();
		// return (a-b).cross((c-a)).normalized();
	}

// #error "some bug in ray_tri_xsect, but it HAS NOT to do with lambda, but the actual intersection or normal"

	// TODO: Use simpler function for xsect test, this one when bary needed
	// float __attribute__((noinline)) ray_tri_xsect(const Ray& ray,
	float ray_tri_xsect(const Ray& ray,
			Ref<const Vector3f> n,
			Ref<const Vector3f> a,
			Ref<const Vector3f> b,
			Ref<const Vector3f> c,
			Ref<Vector3f> lamb, // the output barycentric coords
			bool DBG ) {

		Map<const Vector3f> p0 { ray.p };
		Map<const Vector3f> d  { ray.d };

		float t = -(p0 - a).dot(n) / d.dot(n);
		if (DBG) fmt::print(" - got t {}\n", t);
		if (t < 0) return 0;

		Vector3f u = p0 + t * d;

#if 1
		// Rotate as to zero out the Z axis
		Matrix<float, 3, 2, RowMajor> R;
		if (std::abs(n.dot(Vector3f::UnitX())) > .99)
			R.col(0) = Vector3f::UnitY();
		else
			R.col(0) = Vector3f::UnitX();
		R.col(1) = -n.cross(R.col(0)).normalized();
		R.col(0) = R.col(1).cross(n).normalized();

		Matrix<float, 4, 3, RowMajor> inPts;
		inPts.row(0) = a.transpose();
		inPts.row(1) = b.transpose();
		inPts.row(2) = c.transpose();
		inPts.row(3) = u.transpose();
		Matrix<float, 4, 2, RowMajor> pts = inPts * R;
#else
		Matrix<float, 3, 3, RowMajor> R;
		R.col(2) = n.normalized();
		// if (n.dot(Vector3f::UnitY()) < .99) R.col(1) = -R.col(2).cross(Vector3f::UnitX()).normalized();
		// else R.col(1) = -R.col(2).cross(Vector3f::UnitY()).normalized();
		R.col(1) = -R.col(2).cross(Vector3f::UnitX()).normalized();
		R.col(0) = R.col(1).cross(R.col(2)).normalized();

		Matrix<float, 4, 3, RowMajor> inPts;
		inPts.row(0) = a.transpose();
		inPts.row(1) = b.transpose();
		inPts.row(2) = c.transpose();
		inPts.row(3) = u.transpose();
		Matrix<float, 4, 2, RowMajor> pts = inPts * R.bottomLeftCorner<3,2>();

		// fmt::print(" - inPts:\n{}\n", inPts);
		// fmt::print(" - pts:\n{}\n", inPts*R);
		// fmt::print(" - from R :\n{}\n", R*R.transpose());
#endif

		// https://en.wikipedia.org/wiki/Barycentric_coordinate_system#Vertex_approach

		float two_det = pts(0,0) * (pts(1,1)-pts(2,1)) + pts(1,0) * (pts(2,1)-pts(0,1)) + pts(2,0) * (pts(0,1)-pts(1,1));
		RowMatrix3f M; M <<
			pts(1,0)*pts(2,1) - pts(2,0)*pts(1,1), pts(1,1)-pts(2,1), pts(2,0)-pts(1,0),
			pts(2,0)*pts(0,1) - pts(0,0)*pts(2,1), pts(2,1)-pts(0,1), pts(0,0)-pts(2,0),
			pts(0,0)*pts(1,1) - pts(1,0)*pts(0,1), pts(0,1)-pts(1,1), pts(1,0)-pts(0,0);
		// Vector3f lamb = M * Vector3f { 1, pts(3,0), pts(3,1) } / two_det;
		lamb = M * Vector3f { 1, pts(3,0), pts(3,1) } / two_det;

		if (DBG) fmt::print(" - got lamb {}\n", lamb.transpose());
		if ( (lamb.array()<0).any() ) return 0;
		return t;
	}

	bool hitLight(const Ray& ray) {
		Map<const Vector3f> p0 { ray.p };
		Map<const Vector3f> d  { ray.d };

		// Vector3f n { 0, 0, -1 };
		Vector3f n { 0, 0.70710678, -0.70710678 };
		// Vector3f q { 0, 0, _camera_z+1.f };
		Vector3f q { 0, 0, 4000+1.f };
		float t = -(p0 - q).dot(n) / d.dot(n);

		if (t < 0) return false;

		Vector3f p = p0 + t * d;

		// fmt::print(" - hit light at {}\n", p.transpose())

		// return (p - q).squaredNorm() < 3.;
		// return (p - q).squaredNorm() < square(1000.f);
		return true;
	}

	Vector3f reflect_exact( Ref<const Vector3f> d, Ref<const Vector3f> n) {
		Vector3f o = d - 2.f * d.dot(n) * n;
		// return (o + Vector3f::Random() * .5).normalized();
		return (o + Vector3f::Random() * .1).normalized();
	}

	Vector4f sample_texture(const Buffer<Pixel>& I, Ref<const Vector2f> uv) {
		Matrix<int,2,1> xy = (uv.array() * Array2f { (float)I.w, (float)I.h }).cast<int>();
		xy(0) = xy(0) >= I.w ? I.w-1 : xy(0) < 0 ? 0 : xy(0);
		xy(1) = xy(1) >= I.h ? I.h-1 : xy(1) < 0 ? 0 : xy(1);
		const auto& samp = I(xy(1), xy(0));
		// return Vector4f::Ones();
		// return Vector4f { uv(0), uv(1), 0, 1 };
		return Vector4f {
			(float)samp.r,
			(float)samp.g,
			(float)samp.b,
			(float)samp.rough1 } * (1.f/255.f);
	}


}


Raytracer::Raytracer() {
	cam.wh << w,h;
	cam.fxy << ((float)w)*.8f, ((float)h)*.8f;
	cam.cxy << w / 2.f, h / 2.f;
	cam.V <<
		1, 0, 0, 0,
		0, -1, 0, 0,
		0, 0, -1, _camera_z,
		0, 0, 0, 1;

	acc.allocateCpu(h,w);
}

void Raytracer::setViewMatrix(const RowMatrix4f& view) {
	cam.V = view;
}

RenderResult Raytracer::render() {

	for (int y=0; y<h; y++) {
		for (int x=0; x<w; x++) {
			acc(y,x).setZero();
		}
	}

	for (int i = 0; i < cfg.samplesPerPixel; i++) {
		make_samples();
		trace();
	}

	return finalizeResult();
}

void Raytracer::setGeometry(Geometry&& geom_) {
	this->geom = std::move(geom_);
}

void Raytracer::make_samples() {
	rays.allocateCpu(h,w);

	RowMatrix4f Vinv = inv_isometry(cam.V);
	RowMatrix3f R = Vinv.topLeftCorner<3,3>();
	Vector3f t = Vinv.topRightCorner<3,1>();

	fmt::print(" - R:\n{}\n", R);
	fmt::print(" - Eye: {}\n", t.transpose());

	for (int y=0; y<h; y++) {
		for (int x=0; x<w; x++) {
			Vector3f r = (R * ((Array2f{x,y} - cam.cxy).array() / cam.fxy).matrix().homogeneous());
			// Vector3f r = (R * ((Array2f{x,y} - cam.cxy).array() / cam.fxy).matrix().homogeneous()).normalized();
			// Vector3f r = R * Vector3f { 0, 0, 1 };
			Map<Vector3f>(rays(y,x).d) = r;
			Map<Vector3f>(rays(y,x).p) = t;

			// if ((y % 32 == 0) and (x % 32 == 0)) fmt::print(" - ray {} {} => {}\n", x,y, r.transpose());
		}
	}
	fmt::print(" - Made {} rays\n", h*w);
}


void Raytracer::trace() {
	// For each ray
	//		For di to cfg.depth
	//			For each tile
	//				For each triangle
	//					If there is intersection and it is closer than last
	//						Set as xsect
	//			ElsseIf hit light
	//					stop here
	//			Else hit nothing
	//				stop here
	//		Compute final result

	struct Intersection {
		int tileIndex,
			indexIndex; };

	struct HitPayload {
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
		Vector3f rad;
	};

	RowMatrix4f Vinv = inv_isometry(cam.V);
	RowMatrix3f R = Vinv.topLeftCorner<3,3>();
	Vector3f t = Vinv.topRightCorner<3,1>();

	float invSamplesPerPixel = 1.f / cfg.samplesPerPixel;

	{
			static HitPayload payloads[cfg.depth];
#pragma omp threadprivate(payloads)
#pragma omp parallel for schedule(dynamic,8) num_threads(6)
	for (int y=0; y<h; y++) {
		for (int x=0; x<w; x++) {

			Ray& ray = rays(y,x);
			// NOTE: make_samples not doing anything right now
			Array2f xy = Array2f { x, y } - cam.cxy;
			// Array2f de_focus = Array2f::Random() * (4.0 * xy.matrix().norm() / w + .05); // more defocus at edges
			Array2f de_focus = Array2f::Random() * .3; // equal defocus at edges
			Vector3f r = (R * ((xy+de_focus) / cam.fxy).matrix().homogeneous());
			Map<Vector3f>(ray.d) = r;
			Map<Vector3f>(ray.p) = t;

			bool didTerminateNormally = false;

			int di = 0;
			// bool DBG = y==256 and x==256;
			// bool DBG = y > 300;
			// bool DBG = (y >= 255 and y <= 257) and (x > 254 and x <= 258);
			bool DBG = false;
			// bool DBG = x == w/2 and y == h/2;
			// if (DBG) fmt::print(" - {} {} ray {} {} {}\n", x,y, ray.d[0], ray.d[1], ray.d[2]);

			for (di=0; di<cfg.depth; di++) {

				// Minimal allowed ray distance to object. Prevents hitting the just-hit object again.
				constexpr float T_EPSILON  = .0001f;
				constexpr float T_STEPMULT = .999999;

				float min_t = 9999999.f;
				Intersection min_xsect { -1, -1 };

				for (int ti=0; ti<geom.size(); ti++) {
				// for (int ti=1; ti<2; ti++) {
					auto& tile = geom[ti];
					for (int ii=0; ii<tile.indices.h; ii+=3) {
						int ia = tile.indices[ii  ];
						int ib = tile.indices[ii+1];
						int ic = tile.indices[ii+2];
						// fmt::print(" - ti {}, ii {}, abc {} {} {} (len {}i {}v)\n", ti,ii,ia,ib,ic,tile.indices.h,tile.verts.h);
						Vector3f va { tile.verts(ia).x, tile.verts(ia).y, tile.verts(ia).z };
						Vector3f vb { tile.verts(ib).x, tile.verts(ib).y, tile.verts(ib).z };
						Vector3f vc { tile.verts(ic).x, tile.verts(ic).y, tile.verts(ic).z };
						Vector3f cn = compute_cell_normal(va,vb,vc);
						if (DBG) fmt::print(" - tri normal: {}\n",cn.transpose());
						Vector3f lamb;
						if (float t = ray_tri_xsect(ray, cn, va, vb, vc, lamb, DBG); t > T_EPSILON) {
							if (std::abs(t) < min_t) {
								min_t = std::abs(t);
								min_xsect = { ti, ii };

								if (DBG) {
									Map<Vector3f> rd { ray.d };
									Map<Vector3f> ro { ray.p };
									fmt::print(" - pixel {} {} hit with t {}, at {}, tri {}:{}\n",
											x,y, t, (ro+t*rd).transpose(), ti,ii);
								}
							}
						}
					}
				}

				if (min_xsect.tileIndex == -1) {

					// If we hit a light, we can stop now
					if (hitLight(ray)) {
						payloads[di].rad.setConstant(1.f);
						di++;
						didTerminateNormally = true;
						break;
					}

					// if (DBG) fmt::print(" - MISS (d {}) {} {} ray {} {} {}\n", di, x,y, ray.d[0], ray.d[1], ray.d[2]);
					// Nothing hit, set ambient light, terminate
					payloads[di].rad.setConstant(.7f); // Ambient light
					// payloads[di].rad.setZero();
					didTerminateNormally=true;
					di++;
					break;
				} else {
					// Surface was hit, record hit, reflect, and keep going

					int ti = min_xsect.tileIndex, ii = min_xsect.indexIndex;
					const auto& tile = geom[ti];
					int ia = tile.indices[ii  ];
					int ib = tile.indices[ii+1];
					int ic = tile.indices[ii+2];
					Vector3f va { tile.verts(ia).x, tile.verts(ia).y, tile.verts(ia).z };
					Vector3f vb { tile.verts(ib).x, tile.verts(ib).y, tile.verts(ib).z };
					Vector3f vc { tile.verts(ic).x, tile.verts(ic).y, tile.verts(ic).z };
					Vector3f cn = compute_cell_normal(va,vb,vc);
					Vector3f n = cn; // TODO: use vertex normals


					Vector3f lamb;
					float t = ray_tri_xsect(ray, cn, va, vb, vc, lamb, DBG);
					assert(std::abs(t) == min_t);

					Matrix<float,3,2> uvs; uvs <<
						tile.verts(ia).u, tile.verts(ia).v,
						tile.verts(ib).u, tile.verts(ib).v,
						tile.verts(ic).u, tile.verts(ic).v;
					Vector2f uv = uvs.transpose() * lamb;

					Vector4f surfColor = sample_texture(tile.pixels, uv);
					// if (x == w/2) fmt::print(" - SURF: {} at pixel {} {}, uv {} {}, lamb {}\n", surfColor.transpose(), y, x, uv(0), uv(1), lamb.transpose());
					payloads[di].rad = surfColor.head<3>();
					// payloads[di].rad.setConstant(.8f); // TODO: sample texture
					// payloads[di].rad = Vector3f { n.dot(cam.V.block<1,3>(2,0)) * .5 + .5 , t<0.f ? 0 : 1.f, 0.f };

					Map<Vector3f> rd { ray.d };
					Map<Vector3f> ro { ray.p };
					// ro = ro + rd * min_t * .99999f;
					ro = ro + rd * min_t * T_STEPMULT;
					rd = reflect_exact(rd, n);
					// break;
					// payloads[di].rad = .5+.5*reflect_exact(rd,n).array(); break;
					// payloads[di].rad = hitRay(hit)*.5+.5*reflect_exact(rd,n).array(); break;
				}

			}

			// Walk backward, accumulating color.
			int reachedDepth = di;

			Vector3f color;
			if (didTerminateNormally) color = payloads[--di].rad;
			else color.setConstant(.7f); // ambient color
			if (di >= cfg.depth) di--;

			while (di >= 0) {
				color.array() *= payloads[di].rad.array();
				di--;
			}

			// Splat
			this->acc(y,x) += color * invSamplesPerPixel;
			// this->acc(y,x) += color;
			// if ((color.array() > 0).any()) fmt::print(" - ray ({} {}) (d {}) (col {})\n", x,y, reachedDepth, color.transpose());
		}
	}
	}

	constexpr char chars[] = " .-:*#@";
	int div = 8 * w / 512;
	for (int y=0; y<h; y+=div*3/2) {
		for (int x=0; x<w; x+=div) {
			int val = (acc(y,x)(0) * 6.f);
			val = val < 0 ? 0 : val > 6 ? 6 : val;
			printf("%c", chars[val]);
		}
		printf("\n");
	}

	fmt::print(" - Traced {} * {} rays\n", h*w, cfg.depth);
}

RenderResult Raytracer::finalizeResult() {
	RenderResult res;

	res.rgb.allocateCpu(h, w);
	for (int y=0; y<h; y++) {
		for (int x=0; x<w; x++) {
			byte3 &rgb = res.rgb(y,x);
			Array<uint8_t, 3, 1> val = (acc(y,x) * 255.999f).cwiseMin(255.f).cwiseMax(0.f).cast<uint8_t>();
			rgb[0] = val(0);
			rgb[1] = val(1);
			rgb[2] = val(2);
		}
	}


	return res;
}





/*
Geometry::allocateCpu() {
	isCuda = false;

	assert(positions == 0);

	assert(nv > 0);
	assert(nc > 0);
	assert(no > 0);

	constexpr FS = sizeof(float);
	positions = malloc(SF * nv * 3);

}
*/

/*
void Tile::allocateCpu(int nv, int texWidth, int texHeight) {
	this->numVerts = nv;
	this->texWidth = texWidth;
	this->texHeight = texHeight;

	pixels = malloc(sizeof(Pixel) * texWidth * texHeight);
	verts = malloc(sizeof(Vertex) * numVers);
}

void Tile::deallocateCpu() {
	if (verts) free(verts);
	if (pixels) free(pixels);
	verts = 0;
	pixels = 0;
	numVerts = 0;
	texWidth = texHeight = 0;
}


Tile::~Tile() {
	if (isCuda)
		deallocateCuda();
	else
		deallocateCpu();
}
*/
