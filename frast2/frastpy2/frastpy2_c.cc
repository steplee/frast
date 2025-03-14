#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "frast2/flat/reader.h"

#ifdef FRASTGL
#include "gt_app_wrapper.h"
#endif

namespace py = pybind11;

using namespace frast;

namespace {
void verifyShapeOrThrow(int outh, int outw, int dsetChannels, const py::buffer_info& bi) {
	if (bi.ndim == 2 and dsetChannels != 1)
		throw std::runtime_error("Provided 2d output buffer, but dset had >1 channel.");
	else if (bi.ndim != 2 and bi.ndim != 3) throw std::runtime_error("Provided non 2d or 3d output buffer.");
	if (bi.ndim == 2 or bi.ndim == 3 and bi.shape[2] < dsetChannels)
		throw std::runtime_error("Provided buffer had too few channels for dataset.");

	if (bi.shape[0] < outh)
		throw std::runtime_error("Output buffer too small, width was " + std::to_string(bi.shape[0]) +
								 ", needed >=" + std::to_string(outw));
	if (bi.shape[1] < outw)
		throw std::runtime_error("Output buffer too small, height was " + std::to_string(bi.shape[0]) +
								 ", needed >=" + std::to_string(outw));
}

inline auto get_dtype(int cv_type) {
	if (cv_type == CV_8UC1 or cv_type == CV_8UC3 or cv_type == CV_8UC2 or cv_type == CV_8UC4)
		return py::dtype::of<uint8_t>();
	if (cv_type == CV_16UC1)
		return py::dtype::of<uint16_t>();
	if (cv_type == CV_32FC1)
		return py::dtype::of<float>();
	throw std::runtime_error("invalid cvtype");
}

py::array create_py_image(cv::Mat& img) {

	auto dtype = get_dtype(img.type());
	auto elemSize = dtype.itemsize();

	std::vector<ssize_t> outStrides {
		elemSize*img.channels() * img.cols,
		elemSize*img.channels(),
		elemSize*1 };

	std::vector<ssize_t> outShape {
		img.rows,
		img.cols,
		img.channels() };

	// auto result = py::array(dtype, outShape, outStrides, (uint8_t*) img.data, out);
	auto result = py::array(dtype, outShape, outStrides, (uint8_t*) img.data);
	return result;
}

#ifdef FRASTGL
py::array create_py_image(const Image& img) {

	auto dtype = img.isFloat ? py::dtype::of<float>() : py::dtype::of<uint8_t>();
	auto elemSize = dtype.itemsize();

	std::vector<ssize_t> outStrides {
		elemSize*img.c * img.w,
		elemSize*img.c,
		elemSize*1 };

	std::vector<ssize_t> outShape {
		img.h,
		img.w,
		img.c };

	auto result = py::array(dtype, outShape, outStrides, (uint8_t*) img.data.data());
	return result;
}
#endif


}  // namespace


struct DatasetReaderIterator {
	FlatReaderCached* reader;
	int			   lvl;
	int			   ii = 0;
	int n = 0;
	int channels;

	inline DatasetReaderIterator(FlatReaderCached* reader, int lvl, int channels) : reader(reader), lvl(lvl), channels(channels) {}
	inline void init() {
		n = reader->levelSize(lvl);
	}

	inline ~DatasetReaderIterator() {
	}

	inline py::object next() {

		if (ii >= n)
			throw py::stop_iteration();

		uint64_t key = reader->env.getKeys(lvl)[ii];
		Value val = reader->env.getValueFromIdx(lvl, ii);
		auto mat = decodeValue(val, channels, reader->isTerrain());
		auto arr = create_py_image(mat);

		ii++;
		
		return py::make_tuple(BlockCoordinate(key), arr);
	}
};

struct DatasetReaderIteratorNoImages {
	FlatReaderCached* reader;
	int			   lvl;
	int			   ii = 0;
	int n = 0;

	inline DatasetReaderIteratorNoImages(FlatReaderCached* reader, int lvl) : reader(reader), lvl(lvl) {}
	inline void init() {
		n = reader->levelSize(lvl);
	}

	inline ~DatasetReaderIteratorNoImages() {
	}

	inline auto next() {

		if (ii >= n)
			throw py::stop_iteration();

		uint64_t key = reader->env.getKeys(lvl)[ii];

		ii++;
		
		return BlockCoordinate(key);
	}
};


PYBIND11_MODULE(frastpy2_c, m) {

	m.def("getCellSize", [](int lvl) {
		if (lvl < 0 or lvl >= MAX_LVLS) throw std::runtime_error("Lvl must be >0 and <" + std::to_string(MAX_LVLS));
		return WebMercatorCellSizes[lvl];
	});
	m.attr("WebMercatorMapScale") = WebMercatorMapScale;

	// void dwm_to_iwm(uint32_t iwmTlbr[4], const double wmTlbr[4], int lvl);
	// void iwm_to_dwm(double dwmTlbr[4], const uint32_t iwmTlbr[4], int lvl);
	m.def("dwm_to_iwm", [](py::array_t<uint32_t> tlbr, int lvl) -> py::array_t<double> {
		double data[4];
		iwm_to_dwm(data, tlbr.data(), lvl);
		auto result = py::array(py::dtype::of<double>(), {4},{1}, data);
		return result;
	});
	m.def("iwm_to_dwm", [](py::array_t<double> tlbr, int lvl) -> py::array_t<double> {
		uint32_t data[4];
		dwm_to_iwm(data, tlbr.data(), lvl);
		auto result = py::array(py::dtype::of<uint32_t>(), {4},{1}, data);
		return result;
	});

	py::class_<DatasetReaderIterator>(m, "DatasetReaderIterator")
		.def(py::init<FlatReaderCached*, int, int>())
		.def("__iter__",
			 [](DatasetReaderIterator* it) {
				 it->init();
				 return it;
			 })
		.def("__next__", [](DatasetReaderIterator* it) { return it->next(); });
	py::class_<DatasetReaderIteratorNoImages>(m, "DatasetReaderIteratorNoImages")
		.def(py::init<FlatReaderCached*, int>())
		.def("__iter__",
			 [](DatasetReaderIteratorNoImages* it) {
				 it->init();
				 return it;
			 })
		.def("__next__", [](DatasetReaderIteratorNoImages* it) { return it->next(); });

	py::class_<BlockCoordinate>(m, "BlockCoordinate")
		.def(py::init<uint64_t>())
		.def(py::init<uint64_t, uint64_t, uint64_t>())
		.def("c", [](BlockCoordinate& bc) { return bc.c; })
		.def("z", [](BlockCoordinate& bc) { return bc.z(); })
		.def("y", [](BlockCoordinate& bc) { return bc.y(); })
		.def("x", [](BlockCoordinate& bc) { return bc.x(); });



	py::class_<EnvOptions>(m, "EnvOptions")
		.def(py::init<>())
		.def_readwrite("readonly", &EnvOptions::readonly)
		.def_readwrite("isTerrain", &EnvOptions::isTerrain)
		.def_readwrite("cache", &EnvOptions::cache);

	py::class_<FlatReaderCached>(m, "FlatReaderCached")
		.def(py::init<const std::string&, const EnvOptions&>())

		.def("setMaxRasterIoTiles", [](FlatReaderCached& dset, int n) { dset.setMaxRasterIoTiles(n); })
		.def("iterTiles", [](FlatReaderCached& dset, int lvl, int chans) { return new DatasetReaderIterator(&dset, lvl, chans); })
		.def("iterCoords", [](FlatReaderCached& dset, int lvl) { return new DatasetReaderIteratorNoImages(&dset, lvl); })

		.def("getExistingLevels",
			 [](FlatReaderCached& dset) {
				 std::vector<int> out;
				 for (int i=0; i<30; i++) if (dset.env.haveLevel(i)) out.push_back(i);
				 return out;
			 })

		// .def_readonly("tileSize", &FlatReaderCached::tileSize)
		.def("getRegions",
			 [](FlatReaderCached& dset) {
				 py::list list;
				 for (const auto& r : dset.computeRegionsOnDeepestLevel()) {
					 py::list list_;
					 list_.append(r[0]);
					 list_.append(r[1]);
					 list_.append(r[2]);
					 list_.append(r[3]);
					 list.append(list_);
				 }
				 return list;
			 })

		.def("getTile", [](FlatReaderCached& dset, uint64_t tile, int channels) -> py::object {
				cv::Mat mat = dset.getTile(tile, channels);
				if (mat.empty()) return py::none();
				return create_py_image(mat);
			})

		.def("getTlbr", [](FlatReaderCached& dset, int lvl, py::array_t<uint32_t> tlbr_, int channels) -> py::object {

				if (lvl<0 or lvl>30) throw std::runtime_error("invalid lvl, must be >0, <30");
				if (tlbr_.size() != 4) throw std::runtime_error("tlbr must be length 4.");
				if (tlbr_.ndim() != 1) throw std::runtime_error("tlbr must have one dim.");
				if (tlbr_.strides(0) != 4)
					throw std::runtime_error("tlbr must have stride 4 (be contiguous uint64_t), was: " +
							std::to_string(tlbr_.strides(0)));

				uint32_t* tlbr = const_cast<uint32_t*>(tlbr_.data());
				cv::Mat mat = dset.getTlbr(lvl, tlbr, channels);
				if (mat.empty()) return py::none();

				return create_py_image(mat);
			})

		.def("rasterIo", [](FlatReaderCached& dset, py::array_t<double> tlbrWm_, int outW, int outH, int channels) -> py::object {

				if (tlbrWm_.size() != 4) throw std::runtime_error("tlbr must be length 4.");
				if (tlbrWm_.ndim() != 1) throw std::runtime_error("tlbr must have one dim.");
				if (tlbrWm_.strides(0) != 8)
					throw std::runtime_error("tlbr must have stride 8 (be contiguous uint64_t), was: " +
							std::to_string(tlbrWm_.strides(0)));

				double* tlbrWm = const_cast<double*>(tlbrWm_.data());
				cv::Mat mat = dset.rasterIo(tlbrWm, outW, outH, channels);
				if (mat.empty()) return py::none();

				return create_py_image(mat);
			})
		;

#ifdef FRASTGL
	py::class_<AppConfig>(m, "AppConfig")
		.def(py::init<>())
		.def_readwrite("title", &AppConfig::title)
		.def_readwrite("headless", &AppConfig::headless)
		.def_readwrite("w", &AppConfig::w)
		.def_readwrite("h", &AppConfig::h)
		;

	// No public constructor: use `create_gt_app_config`
	py::class_<GtConfig>(m, "GtConfig")
		;
		// .def(py::init<>());

	py::class_<CameraSpec>(m, "CameraSpec")
		.def(py::init<>())
		.def(py::init<double, double, double>()) // w, h, vfov
		.def("w", [](const CameraSpec& s) { return s.w; })
		.def("h", [](const CameraSpec& s) { return s.h; })
		.def("fx", [](const CameraSpec& s) { return s.fx(); })
		.def("fy", [](const CameraSpec& s) { return s.fy(); })
		;

	py::class_<RenderAction>(m, "RenderAction")
		.def(py::init<>())
		.def_readwrite("want_depth", &RenderAction::want_depth);


	m.def("makeSetCameraFromPosRotSpec", [](
				const py::array_t<float> t,
				const py::array_t<float> R,
				const CameraSpec& spec) {
			auto tt = t.unchecked<1>();
			auto RR = R.unchecked<2>();
			float ivew[16] = {
				RR(0,0), RR(0,1), RR(0,2), tt(0),
				RR(1,0), RR(1,1), RR(1,2), tt(1),
				RR(2,0), RR(2,1), RR(2,2), tt(2),
				0, 0, 0, 1 };
			return SetCameraAction(ivew, spec);
	});

	py::class_<SetCameraAction>(m, "SetCameraAction")
		;
		// .def(py::init<const float*, const CameraSpec&>());
		// .def("__init__", [](const py::array_t<float> a, const CameraSpec& c) {
				// return std::make_unique<SetCameraAction>(a.data(), c);
		// });

	py::class_<RenderResult>(m, "RenderResult")
		.def("getColor", [](const RenderResult& rr) {
				const auto& img = rr.color;
				auto color = create_py_image(img);
				return color;
		});

	m.def("create_gt_app_config", &create_gt_app_config);
	m.def("create_gt_app_config_for_gdal", &create_gt_app_config_for_gdal);

	py::class_<GtWrapperApp>(m, "GtWrapperApp")
		.def(py::init<const AppConfig&, const GtConfig&>())
		.def("setCamera", &GtWrapperApp::external_setCamera)
		.def("askAndWaitForRender", &GtWrapperApp::external_askAndWaitForRender)
		;
#endif


}
