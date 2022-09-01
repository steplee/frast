#include "trace.h"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <fmt/ostream.h>

namespace {
	RowMatrix4f inv_isometry(Eigen::Ref<const RowMatrix4f> A) {
		RowMatrix4f B;
		B.topLeftCorner<3,3>() = A.topLeftCorner<3,3>().transpose();
		B.topRightCorner<3,1>() = A.topLeftCorner<3,3>().transpose() * -A.topRightCorner<3,1>();
		B.row(3) << 0,0,0,1;
		return B;
	}

	Vector3f compute_cell_normal( Ref<const Vector3f> a, Ref<const Vector3f> b, Ref<const Vector3f> c) {
		return (b-a).normalized().cross((c-a).normalized());
	}

	float ray_tri_xsect(const Ray& ray,
			Ref<const Vector3f> n,
			Ref<const Vector3f> a,
			Ref<const Vector3f> b,
			Ref<const Vector3f> c, bool DBG ) {

		Map<const Vector3f> p0 { ray.p };
		Map<const Vector3f> d  { ray.d };

		float t = -(p0 - a).dot(n) / d.dot(n);
		if (DBG) fmt::print(" - got t {}\n", t);
		if (t < 0) return 0;

		Vector3f u = p0 + t * d;

		// Rotate as to zero out the Z axis
		Matrix<float, 3, 2, RowMajor> R;
		// R.col(2) = n;
		R.col(0) = Vector3f::UnitX();
		R.col(1) = n.cross(R.col(0)).normalized();
		R.col(0) = R.col(1).cross(n).normalized();

		Matrix<float, 4, 3, RowMajor> inPts;
		inPts.row(0) = a.transpose();
		inPts.row(1) = b.transpose();
		inPts.row(2) = c.transpose();
		inPts.row(3) = u.transpose();
		Matrix<float, 4, 2, RowMajor> pts = inPts * R;

		// https://en.wikipedia.org/wiki/Barycentric_coordinate_system#Vertex_approach
		float two_det = pts(0,0) * (pts(1,1)-pts(2,1)) + pts(1,0) * (pts(2,1)-pts(0,1)) + pts(2,0) * (pts(0,1)-pts(1,1));
		RowMatrix3f M; M <<
			pts(1,0)*pts(2,1) - pts(2,0)*pts(1,1), pts(1,1)-pts(2,1), pts(2,0)-pts(1,0),
			pts(2,0)*pts(0,1) - pts(0,0)*pts(2,1), pts(2,1)-pts(0,1), pts(0,0)-pts(2,0),
			pts(0,0)*pts(1,1) - pts(1,0)*pts(0,1), pts(0,1)-pts(1,1), pts(1,0)-pts(0,0);

		Vector3f lamb = M * Vector3f { 1, pts(3,0), pts(3,1) } / two_det;

		if (DBG) fmt::print(" - got lamb {}\n", lamb.transpose());
		if ( (lamb.array()<0).any() ) return 0;
		return t;
	}

	bool hitLight(const Ray& ray) {
		Map<const Vector3f> p0 { ray.p };
		Map<const Vector3f> d  { ray.d };

		Vector3f n { 0, 0, 1 };
		Vector3f q { 0, 0, 2 };
		float t = -(p0 - q).dot(n) / d.dot(n);

		return t > 0;
	}

	Vector3f reflect_exact( Ref<const Vector3f> d, Ref<const Vector3f> n) {
		return d - 2.f * d.dot(n) * n;
	}


}


Raytracer::Raytracer() {
	cam.wh << w,h;
	cam.fxy << 300.f, 300.f;
	cam.cxy << w / 2.f, h / 2.f;
	cam.V <<
		1, 0, 0, 0,
		0, -1, 0, 0,
		0, 0, -1, 1,
		0, 0, 0, 1; // Z = +1, oriented to look down.

	acc.allocateCpu(h,w);
}

RenderResult Raytracer::render() {
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

	fmt::print(" - Eye: {}\n", t.transpose());

	for (int y=0; y<h; y++) {
		for (int x=0; x<w; x++) {
			Vector3f r = R * ((Array2f{x,y} - cam.cxy).array() / cam.fxy).matrix().homogeneous();
			Map<Vector3f>(rays(y,x).d) = r;
			Map<Vector3f>(rays(y,x).p) = t;

			// if ((y % 32 == 0) and (x % 32 == 0)) fmt::print(" - ray {} {} => {}\n", x,y, r.transpose());
		}
	}
	fmt::print(" - Made {} rays\n", h*w);
}

void Raytracer::trace() {
	// For each ray
	//		For each tile
	//			For each triangle
	//				If there is intersection and it is closer than last
	//					Set as xsect

	struct Intersection {
		int tileIndex,
			indexIndex; };

	struct HitPayload {
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
		Vector3f rad;
	};

	float invSamplesPerPixel = 1.f / cfg.samplesPerPixel;

	for (int y=0; y<h; y++) {
		for (int x=0; x<w; x++) {

			Ray& ray = rays(y,x);
			HitPayload payloads[cfg.depth];
			int di = 0;
			// bool DBG = y==256 and x==256;
			// bool DBG = y > 300;
			bool DBG = (y >= 255 and y <= 257) and (x > 254 and x <= 258);
			// bool DBG = false;
			// if (DBG) fmt::print(" - {} {} ray {} {} {}\n", x,y, ray.d[0], ray.d[1], ray.d[2]);

			for (di=0; di<cfg.depth; di++) {

				// If we hit a light, we can stop now
				// TODO: Actually should do this last, since it is behind scene
				if (hitLight(ray)) {
					payloads[di].rad.setConstant(1.f);
					break;
				}

				float min_t = 9999999.f;
				Intersection min_xsect { -1, -1 };

				for (int ti=0; ti<geom.size(); ti++) {
					auto& tile = geom[ti];
					for (int ii=0; ii<tile.indices.h; ii+=3) {
						int ia = tile.indices[ii  ];
						int ib = tile.indices[ii+1];
						int ic = tile.indices[ii+2];
						Vector3f va { tile.verts(ia).x, tile.verts(ia).y, tile.verts(ia).z };
						Vector3f vb { tile.verts(ib).x, tile.verts(ib).y, tile.verts(ib).z };
						Vector3f vc { tile.verts(ic).x, tile.verts(ic).y, tile.verts(ic).z };
						Vector3f cn = compute_cell_normal(va,vb,vc);
						if (float t = ray_tri_xsect(ray, cn, va, vb, vc, DBG); t > 0) {
							if (t < min_t) {
								min_t = t;
								min_xsect = { ti, ii };

								if (DBG) {
									Map<Vector3f> rd { ray.d };
									Map<Vector3f> ro { ray.p };
									fmt::print(" - pixel {} {} hit with t {}, at {}\n",
											x,y, t, (ro+t*rd).transpose());
								}
							}
						}
					}
				}

				if (min_xsect.tileIndex == -1) {
					// if (DBG) fmt::print(" - MISS (d {}) {} {} ray {} {} {}\n", di, x,y, ray.d[0], ray.d[1], ray.d[2]);
					// Nothing hit, terminate
					payloads[di].rad.setZero();
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

					payloads[di].rad.setConstant(.8f); // TODO: sample texture

					Map<Vector3f> rd { ray.d };
					Map<Vector3f> ro { ray.p };
					ro = ro + rd * min_t;
					rd = reflect_exact(rd, n);
				}

			}

			// Walk backward, accumulating color.
			int reachedDepth = di;
			Vector3f color = payloads[di--].rad;
			while (di >= 0) {
				color.array() *= payloads[di].rad.array();
				di--;
			}

			// Splat
			// this->acc(y,x) += color * invSamplesPerPixel;
			this->acc(y,x) += color;
			// if ((color.array() > 0).any()) fmt::print(" - ray ({} {}) (d {}) (col {})\n", x,y, reachedDepth, color.transpose());
		}
	}

	constexpr char chars[] = " .-:*#@";
	for (int y=0; y<h; y+=16) {
		for (int x=0; x<w; x+=16) {
			int val = (acc(y,x)(0) * 6.f);
			printf("%c", chars[val]);
		}
		printf("\n");
	}

	fmt::print(" - Traced {} * {} rays\n", h*w, cfg.depth);
}

RenderResult Raytracer::finalizeResult() {
	RenderResult res;
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
