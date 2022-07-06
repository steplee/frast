#include "gt.h"

//
// Implemented non-template qualified functions here.
//


////////////////////////////
// GtOrientedBoundingBox
////////////////////////////

float GtOrientedBoundingBox::computeSse(const GtUpdateCameraData& gtcd, float geoError) const {

	/*
	 gtcd:
			RowMatrix4f mvp;
			Vector3f zplus;
			Vector3f eye;
			Vector2f wh;
			float two_tan_half_fov_y;
	*/


	// Attempt 1: Checking if any OBB corner lies in frustum (after transforming it)
	/*
	Vector3f ctr_ = (gtcd.mvp * ctr.homogeneous()).hnormalized();
	// fmt::print(" - proj ctr {}\n", ctr_.transpose());
	Eigen::AlignedBox<float,3> cube { Vector3f{-1,-1,-.000001f}, Vector3f{1,1,5.f} }; // lower near-plane and increase far-plane to make more robust!
	// fmt::print(" - ctr' ({}) is in NDC box\n", ctr_.transpose());
	if (!cube.contains(ctr_)) {
		Eigen::Matrix<float,8,3,Eigen::RowMajor> corners;
		// for (int i=0; i<8; i++) corners.row(i) << i%2, (i/2)%2, (i/4);
		for (int i=0; i<8; i++) corners.row(i) << (float)((i%4)==1 or (i%4)==2), (i%4)>=2, (i/4);
		RowMatrix3f R = q.inverse().toRotationMatrix();
		// corners = ((corners * 2 - 1).rowwise() * R.transpose() * extents).rowwise() + ctr;
		// corners = (((((corners.array() * 2 - 1).matrix() * R.transpose()).array().rowwise() * extents.transpose()).matrix().rowwise() + ctr.transpose()).rowwise().homogeneous() * gtcd.mvp.transpose()).rowwise().hnormalized();
		corners = (((((corners.array() * 2 - 1).array().rowwise() * extents.transpose()).matrix() * R.transpose()).rowwise() + ctr.transpose()).rowwise().homogeneous() * gtcd.mvp.transpose()).rowwise().hnormalized();
		bool anyInside = false;
		for (int i=0; i<8; i++) if (cube.contains(corners.row(i).transpose())) {
			anyInside = true;
			// fmt::print(" - corner {} ({}) is in NDC box\n", i, corners.row(i));
			break;
		}

		// Neither the center is in the box, nor any corner. Therefore the SSE is zero.
		return 0;
	}
	*/

	// Attempt 2: Actual frustum checking
	// https://iquilezles.org/articles/frustumcorrect/
	// Note: This is not actually 100% correct : https://stackoverflow.com/questions/31788925/correct-frustum-aabb-intersection
	//
	// TODO XXX: This does not seem to work: it allows too much when I look into the sky for example
	//
	RowMatrix83f cornersWorld;
	// getEightCorners(cornersWorld);
	for (int i=0; i<8; i++) cornersWorld.row(i) << (float)((i%4)==1 or (i%4)==2), (i%4)>=2, (i/4);
	RowMatrix3f R_world_from_tile = q.inverse().toRotationMatrix();
	cornersWorld = (((cornersWorld.array() * 2 - 1).array().rowwise() * extents.transpose()).matrix() * R_world_from_tile.transpose()).rowwise() + ctr.transpose();

	// RowMatrix83f cornersCamera = (cornersWorld.rowwise().homogeneous() * gtcd.mvp.transpose()).rowwise().hnormalized();
	RowMatrix84f cornersCamera_ = (cornersWorld.rowwise().homogeneous() * gtcd.mvp.transpose());
	// RowMatrix83f cornersCamera = cornersCamera_.topLeftCorner<8,3>().array() / cornersCamera_.col(3).array();
	RowMatrix83f cornersCamera = cornersCamera_.topLeftCorner<8,3>();
	for (int i=0; i<8; i++) cornersCamera.row(i) /= std::max(cornersCamera_(i,3), .00001f);

	{
		/*
		int cnt;
		// Phase 1 :: bb in frustum
		cnt = 0; for (int j=0; j<8; j++) cnt += cornersCamera.block<1,2>(j,0).transpose().dot(Vector2f{ 1.f, 0.f}) >  1.f; if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += cornersCamera.block<1,2>(j,0).transpose().dot(Vector2f{ 1.f, 0.f}) < -1.f; if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += cornersCamera.block<1,2>(j,0).transpose().dot(Vector2f{ 0.f, 1.f}) >  1.f; if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += cornersCamera.block<1,2>(j,0).transpose().dot(Vector2f{ 0.f, 1.f}) < -1.f; if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += cornersCamera.block<1,3>(j,0).transpose().dot(Vector3f{ 0.f, 0.f, 1.f}) < 0.f; if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += cornersCamera.block<1,3>(j,0).transpose().dot(Vector3f{ 0.f, 0.f, 1.f}) > 3.f; if (cnt==8) return 0.f;
		// Phase 2 :: frustum in bb
		cornersWorld.rowwise() -= ctr.transpose();
		cnt = 0; for (int j=0; j<8; j++) cnt += (cornersWorld.block<1,3>(j,0).transpose()).dot(R_world_from_tile.col(0)) >  extents(0); if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += (cornersWorld.block<1,3>(j,0).transpose()).dot(R_world_from_tile.col(0)) < -extents(0); if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += (cornersWorld.block<1,3>(j,0).transpose()).dot(R_world_from_tile.col(1)) >  extents(1); if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += (cornersWorld.block<1,3>(j,0).transpose()).dot(R_world_from_tile.col(1)) < -extents(1); if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += (cornersWorld.block<1,3>(j,0).transpose()).dot(R_world_from_tile.col(2)) >  extents(2); if (cnt==8) return 0.f;
		cnt = 0; for (int j=0; j<8; j++) cnt += (cornersWorld.block<1,3>(j,0).transpose()).dot(R_world_from_tile.col(2)) < -extents(2); if (cnt==8) return 0.f;
		*/

		// Vectorized version of above:
		if (
				   (cornersCamera.col(0).array() >  1.f).all()
				or (cornersCamera.col(0).array() < -1.f).all()
				or (cornersCamera.col(1).array() >  1.f).all()
				or (cornersCamera.col(1).array() < -1.f).all()
				or (cornersCamera.col(2).array() <  0.f).all()
				or (cornersCamera.col(2).array() >  3.f).all()
				) return 0.f;

		cornersWorld = (cornersWorld.rowwise() - ctr.transpose()) * R_world_from_tile;
		if (
				   (cornersWorld.col(0).array() >  extents(0)).all()
				or (cornersWorld.col(0).array() < -extents(0)).all()
				or (cornersWorld.col(1).array() >  extents(1)).all()
				or (cornersWorld.col(1).array() < -extents(1)).all()
				or (cornersWorld.col(2).array() >  extents(2)).all()
				or (cornersWorld.col(2).array() < -extents(2)).all()
				) return 0.f;
	}
	

	// Either center or any corner in box. Now evalauted oriented-box exterior distance to eye.
	// To get that distance I use a the function "sdBox" from IQ's raymarching function collection.
	// https://iquilezles.org/articles/distfunctions/

	// TODO verify this is correct
	Vector3f p = R_world_from_tile.transpose() * (gtcd.eye - ctr);
	Vector3f q = p.cwiseAbs() - extents.matrix();
	float exteriorDistance = q.array().max(0.f).matrix().norm() + std::min(q.maxCoeff(), 0.f);

	// When inside a box, we get a negative number, which ruins the sse. Clamp so that smallest SDF value is one-half a meter.
	constexpr float R1 = (6378137.0f);
	exteriorDistance = std::max(exteriorDistance, .5f / R1);

	float sse = geoError * gtcd.wh(1) / (exteriorDistance * gtcd.two_tan_half_fov_y);
	// fmt::print(" - (sse {}) (extDist {})\n", sse, exteriorDistance);
	// fmt::print(" - (sse {}) (ge {}) (extDist {}) (eye {}) (ctr {} (ex {}))\n", sse, geoError, exteriorDistance, gtcd.eye.transpose(), ctr.transpose(), extents.transpose());
	return sse;
}

void GtOrientedBoundingBox::getEightCorners(RowMatrix83f& out) const {
		for (int i=0; i<8; i++) out.row(i) << (float)((i%4)==1 or (i%4)==2), (i%4)>=2, (i/4);
		// return;

		RowMatrix3f R = q.inverse().toRotationMatrix();
		// RowMatrix3f R (RowMatrix3f::Identity());
		// out = ((((out.array() * 2 - 1).matrix() * R.transpose()).array().rowwise() * extents.transpose()).matrix().rowwise() + ctr.transpose());
		out = (((out.array() * 2 - 1).array().rowwise() * extents.transpose()).matrix() * R.transpose()).rowwise() + ctr.transpose();
}
