
#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <fmt/ostream.h>
#include <unistd.h>
#include <atomic>
#include <unordered_set>
#include <fcntl.h>

#include "run.h"

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
		REQUIRE(static_cast<uint8_t*>(e.getValueFromIdx(5, i))[0] == (i%256));

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
			REQUIRE(static_cast<uint8_t*>(e.getValueFromIdx(5, i))[0] == (i%256));

		for (int i=0; i<spec.valsLength; i++) reinterpret_cast<uint8_t*>(e.getValues(5))[i] = i % 256;
		for (int i=0; i<spec.keysLength/8; i++) e.getKeys(5)[i] = i;
		for (int i=0; i<spec.keysLength/8; i++) e.getK2vs(5)[i] = i;


	}

	e.printSomeInfo();



}
