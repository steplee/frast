#pragma once

#include <fmt/core.h>

namespace frast {

	// This is thrown when rasterIo cannot find a level in the dataset to read tiles from.
	struct NoValidLevelError : public std::runtime_error {
		float res;
		uint32_t baseLevel;

		inline NoValidLevelError(float res, uint32_t baseLevel)
			: res(res), baseLevel(baseLevel), std::runtime_error(fmt::format("NoValidLevelError(res={}, baseLvl={})", res,baseLevel))
		{ }
	};

	// Thrown when rasterIo fails to pick a level.
	struct InvalidLevelError : public std::runtime_error {
		int32_t lvl;

		inline InvalidLevelError(int32_t lvl_)
			: lvl(lvl_), std::runtime_error(fmt::format("InvalidLevelError(lvl={})", lvl_))
		{ }
	};

	// This is thrown when rasterIo determines it needs to sample too large than some specified
	// constant number of tiles.
	//
	// This would happen when you do not use overviews, for example, and rasterIo
	// would need to sample say 100x100 256^2px tiles to form the filtered result.
	//
	struct SampleTooLargeError : public std::runtime_error {
		uint32_t w, h;

		inline SampleTooLargeError(uint32_t ww, uint32_t hh)
			: w(ww), h(hh), std::runtime_error(fmt::format("SampleTooLargeError(w={}, h={})", ww,hh))
		{ }
	};

	struct BadFileError : public std::runtime_error {
		const std::string file;
		int code;

		inline BadFileError(const std::string& file, int code=0)
			: file(file), code(code), std::runtime_error(fmt::format("BadFileError(f={}, c={})", file, code))
		{}
	};

}
