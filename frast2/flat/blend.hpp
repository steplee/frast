#include <algorithm>
#include <numeric>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace {

	// FIXME:
	// This is a vitally important function.
	// It currently takes the weighted arithmetic mean of all pixels in the input images, where the weight
	// is zero when the pixel is black, and grows to one when the sum of the channels is >25.
	//
	// WARNING:
	// This means if to tiles overlap, they will be blended equally, which might not be what you want.
	//
	// WARNING:
	// There is also a clear border artifact a lot of times, which is unacceptable.
	//
	// TODO: Consider:
	//        - Running a pyramidal weighting to fix the border artifacts
	//        - A first-come-first serve contribution to the final image (or last-come) (i.e. replace like GL_ONE_MINUS_SRC_ALPHA)
	//
	inline cv::Mat blend_imgs_avg(std::vector<cv::Mat>& imgs) {
		// fmt::print("blending {} images\n", imgs.size());

		if (imgs.size() == 0) return {};
		if (imgs.size() == 1) return imgs[0];

		// int c = std::transform_reduce(imgs.begin(),imgs.end(), 0, std::max<int>, [](const cv::Mat& i) { return i.channels(); });
		int in_c = std::transform_reduce(imgs.begin(),imgs.end(), 0, [](int a, int b) { return std::max(a,b); }, [](const cv::Mat& i) { return i.channels(); });
		int h = imgs[0].rows, w = imgs[0].cols;
		assert(in_c == 1 or in_c == 3); // Only 1 or 3 channels supported

		int tmp_c = in_c + 1;
		int tmp_type = in_c==1 ? CV_32SC2 : CV_32SC4;
		int out_type = in_c==1 ? CV_8UC1 : CV_8UC3;
		cv::Mat out(h,w,out_type,cv::Scalar{0});
		cv::Mat img_a32(h,w,tmp_type,cv::Scalar{0});

		int32_t* img_a32_data = (int32_t*)img_a32.data;

		for (auto& img : imgs) {
			// cv::Mat img_a;
			// int in_type = img.type();
			int this_c = img.channels();
			// int tmp_type = in_type == CV_8UC1 ? CV_8UC2 : CV_8UC4;
			// cv::cvtColor(img, img_a, tmp_type);
			// img_a.convertTo(img_a32, cv_type);

			int hh=img.rows, ww=img.cols;
			for (int y=0; y<hh; y++) {
				for (int x=0; x<ww; x++) {
					int32_t v = 0;
					if (this_c == 1) v = ((int32_t)img.data[y*ww+x])*3;
					else             v = ((int32_t)img.data[y*ww*3+x*3+0])
									   + ((int32_t)img.data[y*ww*3+x*3+1])
									   + ((int32_t)img.data[y*ww*3+x*3+2]);

					int32_t weight = std::min(v*10,255);

					if (tmp_c == 4 and this_c == 3)
						for (int i=0; i<tmp_c-1; i++) img_a32_data[y*w*tmp_c+x*tmp_c+i] += ((int32_t)img.data[y*ww*this_c+x*this_c+i])*weight;
					else if (tmp_c == 4 and this_c == 1)
						for (int i=0; i<tmp_c-1; i++) img_a32_data[y*w*tmp_c+x*tmp_c+i] += ((int32_t)img.data[y*ww*this_c+x*this_c+0])*weight;
					else if (tmp_c == 2 and this_c == 3)
						img_a32_data[y*w*tmp_c+x*tmp_c+0] += (
							((int32_t)img.data[y*w*this_c+x*this_c+0]) +
							((int32_t)img.data[y*w*this_c+x*this_c+1]) +
							((int32_t)img.data[y*w*this_c+x*this_c+2]) )*weight/3;
					else if (tmp_c == 2 and this_c == 1)
						img_a32_data[y*w*tmp_c+x*tmp_c+0] += ((int32_t)img.data[y*ww*this_c+x*this_c+0])*weight;
					else assert(false);

					img_a32_data[y*w*tmp_c+x*tmp_c+(tmp_c-1)] += weight;
				}
			}

		}

		// Blend.
		for (int y=0; y<h; y++) {
			for (int x=0; x<w; x++) {
				int32_t weight = img_a32_data[y*w*tmp_c+x*tmp_c+(tmp_c-1)];
				if (weight == 0) continue;
				// fmt::print("w {} ", weight);
				if (tmp_c == 4) {
					int32_t r = img_a32_data[y*w*tmp_c+x*tmp_c+0] / weight;
					int32_t g = img_a32_data[y*w*tmp_c+x*tmp_c+1] / weight;
					int32_t b = img_a32_data[y*w*tmp_c+x*tmp_c+2] / weight;
					// Should never need these clamps.
					// fmt::print(" rgb {} {} {}\n", r,g,b);
					// out.data[y*w*3+x*3+0] = std::max(std::min(r,255), 0);
					// out.data[y*w*3+x*3+1] = std::max(std::min(g,255), 0);
					// out.data[y*w*3+x*3+2] = std::max(std::min(b,255), 0);
					out.data[y*w*3+x*3+0] = r;
					out.data[y*w*3+x*3+1] = g;
					out.data[y*w*3+x*3+2] = b;
				} else {
					int32_t v = img_a32_data[y*w*tmp_c+x*tmp_c+0] / weight;
					out.data[y*w*1+x*1+0] = v;
				}
			}
		}

		return out;
	}






	/*
	// This needs work. I don't recommend using it.
	inline cv::Mat blend_imgs_first(std::vector<cv::Mat>& imgs) {
		// fmt::print("blending {} images\n", imgs.size());

		if (imgs.size() == 0) return {};
		if (imgs.size() == 1) return imgs[0];

		// int c = std::transform_reduce(imgs.begin(),imgs.end(), 0, std::max<int>, [](const cv::Mat& i) { return i.channels(); });
		int in_c = std::transform_reduce(imgs.begin(),imgs.end(), 0, [](int a, int b) { return std::max(a,b); }, [](const cv::Mat& i) { return i.channels(); });
		int h = imgs[0].rows, w = imgs[0].cols;
		assert(in_c == 1 or in_c == 3); // Only 1 or 3 channels supported

		int tmp_c = in_c + 1;
		int tmp_type = in_c==1 ? CV_32SC2 : CV_32SC4;
		int out_type = in_c==1 ? CV_8UC1 : CV_8UC3;
		cv::Mat out(h,w,out_type,cv::Scalar{0});
		cv::Mat img_a32(h,w,tmp_type,cv::Scalar{0});

		int32_t* img_a32_data = (int32_t*)img_a32.data;

		for (auto& img : imgs) {
			int this_c = img.channels();

			int hh=img.rows, ww=img.cols;
			cv::Mat weight_img(hh,ww,CV_8U,cv::Scalar{0});
			cv::Mat blurred_img = img.clone();
			for (int y=0; y<hh; y++) {
				for (int x=0; x<ww; x++) {
					int32_t v = 0;
					if (this_c == 1) v = ((int32_t)img.data[y*ww+x])*3;
					else             v = ((int32_t)img.data[y*ww*3+x*3+0])
									   + ((int32_t)img.data[y*ww*3+x*3+1])
									   + ((int32_t)img.data[y*ww*3+x*3+2]);
					int32_t weight = std::min(v*1,255);

					((uint8_t*)weight_img.data)[y*ww+x] = weight;
				}
			}

			cv::Mat weight_img0 = weight_img;
			int erosion_size = 1;
			cv::Mat element = cv::getStructuringElement( cv::MORPH_RECT,
                       cv::Size( 2*erosion_size + 1, 2*erosion_size+1 ),
                       cv::Point( erosion_size, erosion_size ) );
			cv::erode( weight_img0, weight_img0, element );
			cv::erode( weight_img0, weight_img0, element );


			// cv::GaussianBlur(weight_img, weight_img
			cv::pyrDown(weight_img, weight_img);
			cv::pyrUp(weight_img, weight_img);
			cv::pyrDown(blurred_img, blurred_img);
			cv::pyrUp(blurred_img, blurred_img);

			for (int y=0; y<hh; y++) {
				for (int x=0; x<ww; x++) {
					int32_t weight1 = ((uint8_t*)weight_img.data)[y*ww+x];
					// Bad: might as well use floats throughout. But I don't see a way to do this pyramidding without floats.
					if (this_c == 3) {
						blurred_img.data[y*w*3+x*3+0] = (static_cast<float>(blurred_img.data[y*w*3+x*3+0]) / weight1) * 255.f;
						blurred_img.data[y*w*3+x*3+1] = (static_cast<float>(blurred_img.data[y*w*3+x*3+1]) / weight1) * 255.f;
						blurred_img.data[y*w*3+x*3+2] = (static_cast<float>(blurred_img.data[y*w*3+x*3+2]) / weight1) * 255.f;
					} else {
						blurred_img.data[y*w+x] = (static_cast<float>(blurred_img.data[y*w+x]) / weight1) * 255.f;
					}
				}
			}

			for (int y=0; y<hh; y++) {
				for (int x=0; x<ww; x++) {
					int32_t v = 0;
					int32_t ww0 = 4;
					int32_t weight0 = ww0 * (int32_t)(((uint8_t*)weight_img0.data)[y*ww+x]);
					int32_t weight1 = ((uint8_t*)weight_img.data)[y*ww+x];
					int32_t weight = weight0+weight1;

					int32_t old_weight = img_a32_data[y*w*tmp_c+x*tmp_c+(tmp_c-1)];
					weight = weight - old_weight;
					weight = std::max(std::min(weight,5*255), 0);
					int32_t app_weight0 = weight*4/5;
					int32_t app_weight1 = weight*1/5;

					if (tmp_c == 4 and this_c == 3)
						for (int i=0; i<tmp_c-1; i++) img_a32_data[y*w*tmp_c+x*tmp_c+i] += ((int32_t)img.data[y*ww*this_c+x*this_c+i])*app_weight0;
					else if (tmp_c == 4 and this_c == 1)
						for (int i=0; i<tmp_c-1; i++) img_a32_data[y*w*tmp_c+x*tmp_c+i] += ((int32_t)img.data[y*ww*this_c+x*this_c+0])*app_weight0;
					else if (tmp_c == 2 and this_c == 3)
						img_a32_data[y*w*tmp_c+x*tmp_c+0] += (
							((int32_t)img.data[y*w*this_c+x*this_c+0]) +
							((int32_t)img.data[y*w*this_c+x*this_c+1]) +
							((int32_t)img.data[y*w*this_c+x*this_c+2]) )*app_weight0/3;
					else if (tmp_c == 2 and this_c == 1)
						img_a32_data[y*w*tmp_c+x*tmp_c+0] += ((int32_t)img.data[y*ww*this_c+x*this_c+0])*app_weight0;
					else assert(false);

					if (tmp_c == 4 and this_c == 3)
						for (int i=0; i<tmp_c-1; i++) img_a32_data[y*w*tmp_c+x*tmp_c+i] += ((int32_t)blurred_img.data[y*ww*this_c+x*this_c+i])*app_weight1;
					else if (tmp_c == 4 and this_c == 1)
						for (int i=0; i<tmp_c-1; i++) img_a32_data[y*w*tmp_c+x*tmp_c+i] += ((int32_t)blurred_img.data[y*ww*this_c+x*this_c+0])*app_weight1;
					else if (tmp_c == 2 and this_c == 3)
						img_a32_data[y*w*tmp_c+x*tmp_c+0] += (
							((int32_t)blurred_img.data[y*w*this_c+x*this_c+0]) +
							((int32_t)blurred_img.data[y*w*this_c+x*this_c+1]) +
							((int32_t)blurred_img.data[y*w*this_c+x*this_c+2]) )*app_weight1/3;
					else if (tmp_c == 2 and this_c == 1)
						img_a32_data[y*w*tmp_c+x*tmp_c+0] += ((int32_t)blurred_img.data[y*ww*this_c+x*this_c+0])*app_weight1;
					else assert(false);

					img_a32_data[y*w*tmp_c+x*tmp_c+(tmp_c-1)] += weight;
				}
			}

		}

		// Blend.
		for (int y=0; y<h; y++) {
			for (int x=0; x<w; x++) {
				int32_t weight = img_a32_data[y*w*tmp_c+x*tmp_c+(tmp_c-1)];
				if (weight == 0) continue;
				// fmt::print("w {} ", weight);
				if (tmp_c == 4) {
					int32_t r = img_a32_data[y*w*tmp_c+x*tmp_c+0] / weight;
					int32_t g = img_a32_data[y*w*tmp_c+x*tmp_c+1] / weight;
					int32_t b = img_a32_data[y*w*tmp_c+x*tmp_c+2] / weight;
					out.data[y*w*3+x*3+0] = r;
					out.data[y*w*3+x*3+1] = g;
					out.data[y*w*3+x*3+2] = b;
				} else {
					int32_t v = img_a32_data[y*w*tmp_c+x*tmp_c+0] / weight;
					out.data[y*w*1+x*1+0] = v;
				}
			}
		}

		return out;
	}
	*/

	// Not good.
	inline cv::Mat blend_imgs_first(std::vector<cv::Mat>& imgs) {
		if (imgs.size() == 0) return {};
		if (imgs.size() == 1) return imgs[0];

		// int c = std::transform_reduce(imgs.begin(),imgs.end(), 0, std::max<int>, [](const cv::Mat& i) { return i.channels(); });
		int in_c = std::transform_reduce(imgs.begin(),imgs.end(), 0, [](int a, int b) { return std::max(a,b); }, [](const cv::Mat& i) { return i.channels(); });
		int h = imgs[0].rows, w = imgs[0].cols;
		assert(in_c == 1 or in_c == 3); // Only 1 or 3 channels supported

		int tmp_c = in_c + 1;
		int tmp_type = in_c==1 ? CV_32SC2 : CV_32SC4;
		int out_type = in_c==1 ? CV_8UC1 : CV_8UC3;
		cv::Mat out(h,w,out_type,cv::Scalar{0});
		cv::Mat img_a32(h,w,tmp_type,cv::Scalar{0});

		int32_t* img_a32_data = (int32_t*)img_a32.data;

		for (auto& img : imgs) {
			int this_c = img.channels();
			int hh=img.rows, ww=img.cols;
			for (int y=0; y<hh; y++) {
				for (int x=0; x<ww; x++) {
					int32_t v = 0;
					if (this_c == 1) v = ((int32_t)img.data[y*ww+x])*3;
					else             v = ((int32_t)img.data[y*ww*3+x*3+0])
									   + ((int32_t)img.data[y*ww*3+x*3+1])
									   + ((int32_t)img.data[y*ww*3+x*3+2]);

					int32_t weight = std::min(v*10,255*4);
					int32_t old_weight = img_a32_data[y*w*tmp_c+x*tmp_c+(tmp_c-1)];
					weight = std::min(std::max(weight,0),255*4);

					if (tmp_c == 4 and this_c == 3)
						for (int i=0; i<tmp_c-1; i++) img_a32_data[y*w*tmp_c+x*tmp_c+i] += ((int32_t)img.data[y*ww*this_c+x*this_c+i])*weight;
					else if (tmp_c == 4 and this_c == 1)
						for (int i=0; i<tmp_c-1; i++) img_a32_data[y*w*tmp_c+x*tmp_c+i] += ((int32_t)img.data[y*ww*this_c+x*this_c+0])*weight;
					else if (tmp_c == 2 and this_c == 3)
						img_a32_data[y*w*tmp_c+x*tmp_c+0] += (
							((int32_t)img.data[y*w*this_c+x*this_c+0]) +
							((int32_t)img.data[y*w*this_c+x*this_c+1]) +
							((int32_t)img.data[y*w*this_c+x*this_c+2]) )*weight/3;
					else if (tmp_c == 2 and this_c == 1)
						img_a32_data[y*w*tmp_c+x*tmp_c+0] += ((int32_t)img.data[y*ww*this_c+x*this_c+0])*weight;
					else assert(false);

					img_a32_data[y*w*tmp_c+x*tmp_c+(tmp_c-1)] += weight;
				}
			}

		}

		// Blend.
		for (int y=0; y<h; y++) {
			for (int x=0; x<w; x++) {
				int32_t weight = img_a32_data[y*w*tmp_c+x*tmp_c+(tmp_c-1)];
				if (weight == 0) continue;
				// fmt::print("w {} ", weight);
				if (tmp_c == 4) {
					int32_t r = img_a32_data[y*w*tmp_c+x*tmp_c+0] / weight;
					int32_t g = img_a32_data[y*w*tmp_c+x*tmp_c+1] / weight;
					int32_t b = img_a32_data[y*w*tmp_c+x*tmp_c+2] / weight;
					out.data[y*w*3+x*3+0] = r;
					out.data[y*w*3+x*3+1] = g;
					out.data[y*w*3+x*3+2] = b;
				} else {
					int32_t v = img_a32_data[y*w*tmp_c+x*tmp_c+0] / weight;
					out.data[y*w*1+x*1+0] = v;
				}
			}
		}

		return out;
	}


}
