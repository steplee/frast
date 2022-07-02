#include "frastVk/utils/eigen.h"

#include <iostream>

#include "rt.h"
using namespace rt;

#include "protos/rocktree.pb.h"
namespace rtpb = geo_globetrotter_proto_rocktree;

RtTile RocktreeLoader::parseOne(const std::string& path) {
	RtTile out;

	std::ifstream ifs(path);
	rtpb::NodeMetadata nd;
	if (!nd.ParseFromIstream(&ifs)) {
		std::cout << " - failed to parse " << path << "\n";
	}

	return out;
}



int main(int argc, char** argv) {

	RocktreeLoader rtp;

	if (argc != 2) {
		fmt::print(" - Usage:\n\t ./runRt <dir>\n\n");
		return 1;
	}

	std::string file = argv[1];

	rtp.parseOne(file);

	return 0;
}
