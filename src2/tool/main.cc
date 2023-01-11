#include <optional>
#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/core.h>

#include <convertFlatBlocked/reader.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

using Str = std::string;

using namespace frast;

class ArgParser {
	public:

		inline ArgParser(int argc, char** argv) {
			parse(argc, argv);
		}

		template <class T>
		inline std::optional<T> get(const Str& k) {
			Str kk;
			if (k.length() > 2 and k[0] == '-' and k[1] == '-') {
				kk = k.substr(2);
			}
			else if (k.length() > 1 and k[0] == '-') {
				kk = k.substr(1);
			} else {
				throw std::runtime_error("invalid key, must start with - or --");
			}

			if (map.find(kk) != map.end()) {
				return scanAs<T>(map[kk]);
			}
			return {};
		}

		template <class T>
		inline std::optional<T> get(const Str& k, const T& def) {
			auto r = get<T>(k);
			if (r.has_value()) return r;
			return def;
		}

		template <class ...Choices>
		inline std::optional<std::string> getChoice(const Str& k, Choices... choices_) {

			auto vv = get<Str>(k);
			if (not vv.has_value()) return {};
			auto v = vv.value();

			std::vector<std::string> choices { choices_... };
			for (auto& c : choices) {
				if (v == c) return c;
			}

			throw std::runtime_error("invalid choice");
			// return {};
		}

		template <class T>
		inline std::optional<T> get2(const Str& k1, const Str& k2) {
			auto a = get<T>(k1);
			if (a.has_value()) return a;
			return get<T>(k2);
		}
		template <class T>
		inline std::optional<T> get2(const Str& k1, const Str& k2, const T &def) {
			auto a = get<T>(k1);
			if (a.has_value()) return a;
			return get<T>(k2, def);
		}
		template <class ...Choices>
		inline std::optional<std::string> getChoice2(const Str& k1, const Str& k2, Choices... choices_) {
			auto a = getChoice(k1, choices_...);
			if (a.has_value()) return a;
			return getChoice(k2, choices_...);
		}


	private:
		std::unordered_map<Str, Str> map;

		inline void parse(int argc, char** argv) {
			for (int i=0; i<argc; i++) {
				Str arg{argv[i]};

				if (arg[0] != '-') continue;

				auto kstart = 0;
				while (arg[kstart] == '-') kstart++;
				arg = arg.substr(kstart);

				if (arg.find("=") != std::string::npos) {
					auto f = arg.find("=");
					std::string k = arg.substr(0, f);
					std::string v = arg.substr(f+1);

					if (map.find(k) != map.end()) throw std::runtime_error("duplicate key");
					map[k] = v;
				} else {
					assert(i<argc-1);
					Str val{argv[++i]};

					if (map.find(arg) != map.end()) throw std::runtime_error("duplicate key");
					map[arg] = val;
				}
			}

			for (auto kv : map) fmt::print(" - {} : {}\n", kv.first,kv.second);
		}

		template <class T>
		inline T scanAs(const Str& s) {
			// if constexpr(std::is_same_v<T, int>) {
			if constexpr(std::is_same_v<bool,T>) {
				return not (s == "0" or s == "off" or s == "no" or s == "n" or s == "N" or s == "" or s == "false" or s == "False");
			}
			if constexpr(std::is_integral_v<T>) {
				int64_t i;
				auto result = sscanf(s.c_str(), "%ld", &i);
				assert(result==1);
				return static_cast<T>(i);
			}
			if constexpr(std::is_floating_point_v<T>) {
				double d;
				auto result = sscanf(s.c_str(), "%lf", &d);
				assert(result==1);
				return static_cast<T>(d);
			}
			if constexpr(std::is_same_v<Str,T>) {
				return s;
			}
			throw std::runtime_error("failed to scan str as type T");
		}

};

int main(int argc, char** argv) {

	ArgParser parser(argc, argv);

	int i = parser.get<int>("--hi").value();
	bool o = parser.get<bool>("--opt").value();
	auto action = parser.getChoice2("-a", "--action", "info", "showTiles", "showSample").value();
	fmt::print(" - hi={}\n", i);
	fmt::print(" - opt={}\n", o);

	fmt::print(" - action={}\n", action);

	if (action == "info") {
		std::string path = parser.get2<std::string>("-i", "--input").value();
		EnvOptions opts;
		FlatReader reader(path, opts);
		uint64_t tlbr[4];
		auto lvl = reader.determineTlbr(tlbr);
		fmt::print(" - Tlbr (lvl {}) [{} {} -> {} {}]\n", lvl, tlbr[0], tlbr[1], tlbr[2], tlbr[3]);
	}

	if (action == "showTiles") {
		std::string path = parser.get2<std::string>("-i", "--input").value();
		int chosenLvl = parser.get2<int>("-l", "--level", -1).value();

		EnvOptions opts;
		FlatReader reader(path, opts);

		uint64_t tlbr[4];
		auto deepestLvl = reader.determineTlbr(tlbr);

		int lvl = deepestLvl;
		if (chosenLvl != -1 and chosenLvl < deepestLvl) {
			lvl = chosenLvl;
			int64_t zoom = deepestLvl - chosenLvl;
			for (int i=0; i<4; i++)
				tlbr[i] = tlbr[i] / (1 << zoom);
		}
		fmt::print(" - Tlbr (lvl {}) [{} {} -> {} {}]\n", lvl, tlbr[0], tlbr[1], tlbr[2], tlbr[3]);

		auto &spec = reader.env.getLevelSpec(lvl);
		fmt::print(" - Items {}\n", spec.nitemsUsed());
		uint64_t n = spec.nitemsUsed();
		auto keys = reader.env.getKeys(lvl);
		for (int i=0; i<spec.nitemsUsed(); i++) {
			auto key = keys[i];
			// Value val = reader.env.getValueFromIdx(lvl, i);
			Value val = reader.env.lookup(lvl, key);
			fmt::print(" - item ({:>6d}/{:>6d}) key {} len {}\n", i,n, key, val.len);


			if (val.value != nullptr) {
				cv::_InputArray buf((uint8_t*)val.value, val.len);
				cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);
				cv::imshow("tile", img);
				cv::waitKey(0);
			}


		}
	}

	return 0;
}
