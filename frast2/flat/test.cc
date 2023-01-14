
#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <fmt/ostream.h>
#include <unistd.h>
#include <atomic>
#include <unordered_set>
#include <fcntl.h>

#include "writer.h"

using namespace frast;

TEST_CASE( "SimpleGrowKeys", "[flatwriter]" ) {
	fmt::print(" - Running SimpleGrowKeys test.\n");
	fmt::print(" ----------------------------------------------------------------------------\n");

	const std::string fname = "test.it";
	unlink(fname.c_str());

	EnvOptions opts;
	FlatEnvironment e(fname, opts);

	e.beginLevel(5);
	// e.beginLevel(6);

	e.meta()->levelSpecs[5].keysLength = e.meta()->levelSpecs[5].keysCapacity;
	e.meta()->levelSpecs[5].valsLength = e.meta()->levelSpecs[5].valsCapacity;
	for (int i=0; i<e.meta()->levelSpecs[5].valsLength; i++) reinterpret_cast<uint8_t*>(e.getValues(5))[i] = i % 256;
	for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++) e.getKeys(5)[i] = i;
	for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++) e.getK2vs(5)[i] = i/2;

	e.growLevelKeys();

	// for (int i=0; i<e.meta()->levelSpecs[5].valsLength; i++) reinterpret_cast<uint8_t*>(e.getValues(5))[i] = i % 256;
	// for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++) e.getKeys(5)[i] = i;
	// for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++) e.getK2vs(5)[i] = i;

	e.growLevelKeys();

	for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++)
		REQUIRE(e.getKeys(5)[i] == i);
	for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++)
		REQUIRE(e.getK2vs(5)[i] == i/2);

	// for (int i=0; i<e.meta()->levelSpecs[5].valsLength; i++) reinterpret_cast<uint8_t*>(e.getValues(5))[i] = i % 256;
	// for (int i=0; i<e.meta()->levelSpecs[5].keysLength/8; i++) e.getKeys(5)[i] = i;
	
	e.printSomeInfo();
}


// Test growing keys and values
TEST_CASE( "MixedGrow", "[flatwriter]" ) {
	fmt::print(" - Running MixedGrow test.\n");
	fmt::print(" ----------------------------------------------------------------------------\n");

	const std::string fname = "test.it";
	unlink(fname.c_str());

	EnvOptions opts;
	FlatEnvironment e(fname, opts);

	e.beginLevel(5);
	auto& spec = e.meta()->levelSpecs[5];

	// Setup. Set lengths as capacities.
	spec.keysLength = spec.keysCapacity;
	spec.valsLength = spec.valsCapacity;
	for (int i=0; i<spec.valsLength; i++) reinterpret_cast<uint8_t*>(e.getValues(5))[i] = i % 256;
	for (int i=0; i<spec.keysLength/8; i++) e.getKeys(5)[i] = i;
	for (int i=0; i<spec.keysLength/8; i++) e.getK2vs(5)[i] = i;

	for (int i=0; i<spec.keysLength/8; i++)
		REQUIRE(static_cast<uint8_t*>(e.getValueFromIdx(5, i).value)[0] == (i%256));

	// Grow and check result each time.
	for (int iter=0; iter<3; iter++) {

		e.growLevelKeys();
		e.growLevelValues();

		spec.keysLength = spec.keysCapacity;
		spec.valsLength = spec.valsCapacity;
		for (int i=0; i<spec.valsLength; i++) reinterpret_cast<uint8_t*>(e.getValues(5))[i] = i % 256;
		for (int i=0; i<spec.keysLength/8; i++) e.getKeys(5)[i] = i;
		for (int i=0; i<spec.keysLength/8; i++) e.getK2vs(5)[i] = i;

		for (int i=0; i<spec.keysLength/8; i++) REQUIRE(e.getKeys(5)[i] == i);
		for (int i=0; i<spec.keysLength/8; i++) REQUIRE(e.getK2vs(5)[i] == i);

		for (int i=0; i<spec.keysLength/8; i++)
			REQUIRE(static_cast<uint8_t*>(e.getValueFromIdx(5, i).value)[0] == (i%256));

		for (int i=0; i<spec.valsLength; i++) reinterpret_cast<uint8_t*>(e.getValues(5))[i] = i % 256;
		for (int i=0; i<spec.keysLength/8; i++) e.getKeys(5)[i] = i;
		for (int i=0; i<spec.keysLength/8; i++) e.getK2vs(5)[i] = i;


	}

	e.printSomeInfo();



}

TEST_CASE( "WriteKeyValue", "[flatwriter]" ) {
	fmt::print(" - Running WriteKeyValue test.\n");
	fmt::print(" ----------------------------------------------------------------------------\n");

	const std::string fname = "test.it";
	unlink(fname.c_str());

	EnvOptions opts;
	FlatEnvironment e(fname, opts);

	e.beginLevel(5);
	auto& spec = e.meta()->levelSpecs[5];

	std::vector<uint64_t> keys { 0, 4, 7, 9, 10, 11, 12, 17, 20};
	std::vector<uint8_t > vals { 0, 4, 7, 9, 10, 11, 12, 17, 20};

	for (int i=0; i<keys.size(); i++) {
		bool stat = e.writeKeyValue(keys[i], &vals[i], 1);
		REQUIRE(not stat);

		for (int j=0; j<i; j++) {
			auto foundVal = e.lookup(5, keys[j]);
			REQUIRE(foundVal.value != nullptr);
			REQUIRE(*static_cast<uint8_t*>(foundVal.value) == vals[j]);
		}

	}

	for (int i=0; i<keys.size(); i++) {
		auto foundVal = e.lookup(5, keys[i]);
		REQUIRE(foundVal.value != nullptr);
		REQUIRE(*static_cast<uint8_t*>(foundVal.value) == vals[i]);
	}

	// Test a few negatives as well.
	REQUIRE(e.lookup(5, 1).value == nullptr);
	REQUIRE(e.lookup(5, -1).value == nullptr);
	REQUIRE(e.lookup(5, 5).value == nullptr);
	REQUIRE(e.lookup(5, 6).value == nullptr);
	REQUIRE(e.lookup(5, 21).value == nullptr);
	REQUIRE(e.lookup(5, 18).value == nullptr);
	REQUIRE(e.lookup(2, 0).value == nullptr);
	REQUIRE(e.lookup(2, 1).value == nullptr);

}
