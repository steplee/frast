#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <fmt/ostream.h>

#include "flat_writer.h"

using namespace frast;


TEST_CASE( "FlatWriter", "[flatwriter]" ) {

	EnvOptions opts;
	// opts.anon = true;
	opts.mapSize = (1<<30) / 4; // 256 MB
	// ArenaEnvironment env("", opts);


	{

		// FlatWriter(const std::string& path, const EnvOptions& opts);
		FlatWriter fw("tmpFw", opts);

		auto it = fw.getNextKeyRef(5);
		it.k = BlockCoordinate{5,1,2};
		it.byteSize = 1024;
		it.byteOffset = fw.getValueBufferOffset(it.byteSize);
	}
	

	{
		// FlatWriter(const std::string& path, const EnvOptions& opts);
		FlatWriter fw("tmpFw", opts);

		auto it = fw.getNextKeyRef(5);
		it.k = BlockCoordinate{5,1,2};
		it.byteSize = 1024;
		it.byteOffset = fw.getValueBufferOffset(it.byteSize);
	}



}
