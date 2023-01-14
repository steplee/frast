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


class TreeEnv : public PagedEnvironment {
	public:
	TreeEnvironment(const std::string& path, const EnvOptions& opts);

	private:

}

TreeEnvironment::TreeEnvironment(const std::string& path, const EnvOptions& opts)
	: PagedEnvironment(path, opts) {
}




struct MyKey {
	BlockCoordinate bc;
};
struct MyValue {
	uint64_t size_ = 0;
	void* data_ = nullptr;
	inline void* data() { return data_; }
	inline size_t size() { return size_; }
};

class TreeWriter {


	public:


	private:


	using MyBPTree = BPTree<MyKey, MyValue,4>;


};

}
