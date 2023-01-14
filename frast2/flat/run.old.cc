
namespace frast {

// Contains a pointer, so never stored in a file.
struct KeyValue {
	uint64_t key;
	void* value;
	uint32_t len;
};

class SuperBlock {
	public:
		static constexpr int lgEdgeLen = 3;
		static constexpr int EdgeLen = 8;
		static constexpr int N = EdgeLen * EdgeLen;

		BlockCoordinate baseCoord;

	private:
		uint32_t localOffset[N];
		uint32_t sizes[N];

};

class BlockedEnvironment : public ArenaEnvironment {

	KeyValue& get(BlockCoordinate& bc);

};

KeyValue& BlockedEnvironment::get(BlockCoordinate& bc) {
	KeyValue out;

	int idxInSb = (bc.y() % 8) * 8 + bc.x() % 8;
	int idxOfSb = (bc.y() / 8) * 8 + bc.x() / 8;
	SuperBlock sb;

	out.key = bc.c;
	out.value = (void*) ((char*)dataPointer + sbOffset + sb.localOffset[idxInSb]);
	out.len = sb.sizes[idxInSb];
}






class Writer {
	public:
		Writer(const std::string& file);
		
	private:
};

};
