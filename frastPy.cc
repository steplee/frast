#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>


#include "db.h"


namespace py = pybind11;

namespace {
	void verifyShapeOrThrow(int outh, int outw, int dsetChannels, const py::buffer_info& bi) {
		if (bi.ndim == 2 and dsetChannels != 1)
			throw std::runtime_error("Provided 2d output buffer, but dset had >1 channel.");
		else if (bi.ndim != 2 and bi.ndim != 3)
			throw std::runtime_error("Provided non 2d or 3d output buffer.");
		if (bi.ndim == 2 or bi.ndim == 3 and bi.shape[2] < dsetChannels)
			throw std::runtime_error("Provided buffer had too few channels for dataset.");

		if (bi.shape[0] < outh) throw std::runtime_error("Output buffer too small, width was "  + std::to_string(bi.shape[0]) + ", needed >=" + std::to_string(outw));
		if (bi.shape[1] < outw) throw std::runtime_error("Output buffer too small, height was " + std::to_string(bi.shape[0]) + ", needed >=" + std::to_string(outw));
	}
}

PYBIND11_MODULE(frastpy, m) {

	m.def("getCellSize", [](int lvl) {
			if (lvl < 0 or lvl >= MAX_LVLS)
				throw std::runtime_error("Lvl must be >0 and <" + std::to_string(MAX_LVLS));
			return WebMercatorCellSizes[lvl];
	});
	m.attr("WebMercatorScale") = WebMercatorScale;

    py::class_<DatasetReaderOptions>(m, "DatasetReaderOptions")
        .def(py::init<>())
		.def_readwrite("oversampleRation", &DatasetReaderOptions::oversampleRatio)
		.def_readwrite("maxSampleTiles", &DatasetReaderOptions::maxSampleTiles)
		.def_readwrite("forceGray", &DatasetReaderOptions::forceGray)
		.def_readwrite("forceRgb", &DatasetReaderOptions::forceRgb)
		.def_readwrite("nthreads", &DatasetReaderOptions::nthreads);

	// All of the reader functions release the GIL
    py::class_<DatasetReader>(m, "DatasetReader")
        .def(py::init<const std::string&, const DatasetReaderOptions&>())

		// Output buffer is passed in. Returned result is a *view* of it, possibly smaller.
		// Also note: the output stride *may not match* input stride.
		//
		// Note: Parameter if parameter 'safe' is true, if there are *any* missing tiles, None is returned.
		// If 'safe' is false, we may return a partially or entirely blank image.
		.def("fetchBlocks", [](DatasetReader& dset, py::array_t<uint8_t> out, uint64_t lvl, py::array_t<uint64_t> tlbr_, bool safe) -> int {
				AtomicTimerMeasurement g(t_total);
				if (tlbr_.size() != 4) throw std::runtime_error("tlbr must be length 4.");
				if (tlbr_.ndim() != 1) throw std::runtime_error("tlbr must have one dim.");
				if (tlbr_.strides(0) != 8) throw std::runtime_error("tlbr must have stride 8 (be contiguous uint64_t), was: " + std::to_string(tlbr_.strides(0)));
				const uint64_t* tlbr = tlbr_.data();
				int outw = dset.tileSize() * (tlbr[2] - tlbr[0]);
				int outh = dset.tileSize() * (tlbr[3] - tlbr[1]);

				std::vector<ssize_t> outShape;
				py::buffer_info bufIn = out.request();
				verifyShapeOrThrow(outh, outw, dset.channels(), bufIn);

				py::gil_scoped_release release;

				if (bufIn.ndim == 2)
					outShape = {outh, outw};
				else
					outShape = {outh, outw, (ssize_t) dset.channels()};

				std::vector<ssize_t> outStrides;
				outStrides.push_back(dset.channels()*outw);
				outStrides.push_back(dset.channels());
				if (bufIn.ndim == 3) outStrides.push_back(1);
				// WHich one is correct? Do I need to inc_ref() manually()
				//auto result = py::array_t<uint8_t>(outShape, outStrides, (uint8_t*) bufIn.ptr, out.inc_ref());
				auto result = py::array_t<uint8_t>(outShape, outStrides, (uint8_t*) bufIn.ptr, out);

				//Image imgView = Image::view(outh,outw, dset.channels()==3?Image::Format::RGB:dset.channels()==4?Image::Format::RGBN:Image::Format::GRAY, (uint8_t*)bufIn.ptr);
				Image imgView (outh,outw, dset.channels()==3?Image::Format::RGB:dset.channels()==4?Image::Format::RGBN:Image::Format::GRAY, (uint8_t*)bufIn.ptr);

				int nMissing = dset.fetchBlocks(imgView, lvl, tlbr, nullptr);
				//if (safe and nMissing > 0) return py::none();

				return nMissing;
		})


		// Same comment for fetchBlocks() applies here too
		.def("rasterIo", [](DatasetReader& dset, py::array_t<uint8_t> out, py::array_t<double> tlbrWm_) -> py::object {
				AtomicTimerMeasurement g(t_total);
				if (tlbrWm_.size() != 4) throw std::runtime_error("tlbr must be length 4.");
				if (tlbrWm_.ndim() != 1) throw std::runtime_error("tlbr must have one dim.");
				if (tlbrWm_.strides(0) != 8) throw std::runtime_error("tlbr must have stride 8 (be contiguous double), was: " + std::to_string(tlbrWm_.strides(0)));
				const double* tlbr = tlbrWm_.data();
				int outw = out.shape(1), outh = out.shape(0);

				std::vector<ssize_t> outShape;
				py::buffer_info bufIn = out.request();
				verifyShapeOrThrow(outh, outw, dset.channels(), bufIn);

				py::gil_scoped_release release;

				if (bufIn.ndim == 2)
					outShape = {outh, outw};
				else
					outShape = {outh, outw, (ssize_t) dset.channels()};

				std::vector<ssize_t> outStrides;
				outStrides.push_back(dset.channels()*outw);
				outStrides.push_back(dset.channels());
				if (bufIn.ndim == 3) outStrides.push_back(1);
				// WHich one is correct? Do I need to inc_ref() manually()
				//auto result = py::array_t<uint8_t>(outShape, outStrides, (uint8_t*) bufIn.ptr, out.inc_ref());
				auto result = py::array_t<uint8_t>(outShape, outStrides, (uint8_t*) bufIn.ptr, out);

				Image imgView (outh,outw, dset.channels()==3?Image::Format::RGB:dset.channels()==4?Image::Format::RGBN:Image::Format::GRAY, (uint8_t*)bufIn.ptr);

				dset.rasterIo(imgView, tlbr);
				return result;
		})

		.def("rasterIoQuad", [](DatasetReader& dset, py::array_t<uint8_t> out, py::array_t<double> quad) -> py::object {
				AtomicTimerMeasurement g(t_total);
				if (quad.size() != 8) throw std::runtime_error("quad must be length 8.");
				if (quad.ndim() != 1) throw std::runtime_error("quad must have one dim.");
				if (quad.strides(0) != 8) throw std::runtime_error("quad must have stride 8 (be contiguous double), was: " + std::to_string(quad.strides(0)));
				const double* quad_ = quad.data();
				int outw = out.shape(1), outh = out.shape(0);

				std::vector<ssize_t> outShape;
				py::buffer_info bufIn = out.request();
				verifyShapeOrThrow(outh, outw, dset.channels(), bufIn);

				py::gil_scoped_release release;

				if (bufIn.ndim == 2)
					outShape = {outh, outw};
				else
					outShape = {outh, outw, (ssize_t) dset.channels()};

				std::vector<ssize_t> outStrides;
				outStrides.push_back(dset.channels()*outw);
				outStrides.push_back(dset.channels());
				if (bufIn.ndim == 3) outStrides.push_back(1);
				// WHich one is correct? Do I need to inc_ref() manually()
				//auto result = py::array_t<uint8_t>(outShape, outStrides, (uint8_t*) bufIn.ptr, out.inc_ref());
				auto result = py::array_t<uint8_t>(outShape, outStrides, (uint8_t*) bufIn.ptr, out);

				Image imgView (outh,outw, dset.channels()==3?Image::Format::RGB:dset.channels()==4?Image::Format::RGBN:Image::Format::GRAY, (uint8_t*)bufIn.ptr);

				dset.rasterIoQuad(imgView, quad_);
				return result;
		})

		;

}
