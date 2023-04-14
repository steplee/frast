#pragma once

#include <optional>
#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>

namespace frast {
namespace {

using Str = std::string;

inline bool is_a_number(const Str& s) {
	try {
		std::stod(s);
		return true;
	} catch(...) {
		return false;
	}
}


struct Tlbr {
	double tl[2];
	double br[2];
};

// ad-hoc (zero-config) cli argument parser.
//
// Call some variation of get<T>() to get assigned values.
//
// Note: Keys are not allowed to be numbers, because that would make
// parsing lists of numbers impossible (cannot tell if "-f 1 -1 2" is a
// three-length block assigned to key "f", or if the "-1" is starting a new key)
//

class ArgParser {
	public:

		inline ArgParser(int argc, char** argv) {
			parse(argc, argv);
		}

		inline Str getKeyWithoutDashes(const Str& k) {
			if (k.length() > 2 and k[0] == '-' and k[1] == '-') {
				return k.substr(2);
			}
			else if (k.length() > 1 and k[0] == '-') {
				return k.substr(1);
			} else {
				throw std::runtime_error("invalid key, must start with - or --");
			}
		}

		template <class T>
		inline std::optional<T> get(const Str& k) {

			if (is_a_number(k)) {
				throw std::runtime_error("ArgParser keys are NOT allowed to be numbers.");
			}

			Str kk = getKeyWithoutDashes(k);

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

		inline bool have(const Str& k1) {
			return map.find(getKeyWithoutDashes(k1)) != map.end();
		}
		inline bool have2(const Str& k1, const Str& k2) {
			return have(k1) or have(k2);
		}


	private:
		std::unordered_map<Str, std::vector<Str>> map;

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
					map[k] = {v};
				} else {
					assert(i<argc-1);

					std::vector<Str> vals;

					// Properly parse numbers.
					while (i+1 < argc and (argv[i+1][0] != '-' or is_a_number(argv[i+1]))) {
						Str val{argv[++i]};
						vals.push_back(val);
						// if (i+1<argc) fmt::print(" - next up {}\n", argv[i+1]);
					}

					if (map.find(arg) != map.end()) throw std::runtime_error("duplicate key");
					map[arg] = vals;
				}
			}

			// for (auto kv : map) fmt::print(" - {} : {}\n", kv.first,kv.second);
		}

		template <class T>
		inline T scanAs(const std::vector<Str>& ss) {
			if constexpr(std::is_same_v<std::vector<std::string>,T>) {
				// Scan space-seperated strings.
				return ss;
			}

			if constexpr(std::is_same_v<std::vector<float>,T>) {
				std::vector<float> out;
				for (const auto& s : ss) out.push_back(std::stof(s));
				return out;
			}
			if constexpr(std::is_same_v<std::vector<double>,T>) {
				std::vector<double> out;
				for (const auto& s : ss) out.push_back(std::stod(s));
				return out;
			}
			if constexpr(std::is_same_v<std::vector<int>,T>) {
				std::vector<int> out;
				for (const auto& s : ss) out.push_back(std::stoi(s));
				return out;
			}

			if constexpr(std::is_same_v<Tlbr,T>) {
				// Scan space sperated tlbr.
				Tlbr tlbr;
				/*
				auto result = sscanf(s.c_str(), "%lf %lf %lf %lf", tlbr.tl, tlbr.tl+1, tlbr.br, tlbr.br+1);
				assert(result==4);
				*/
				assert(ss.size() == 4);
				tlbr.tl[0] = std::stod(ss[0]);
				tlbr.tl[1] = std::stod(ss[1]);
				tlbr.br[0] = std::stod(ss[2]);
				tlbr.br[1] = std::stod(ss[3]);
				if (tlbr.tl[0] > tlbr.br[0]) std::swap(tlbr.tl[0], tlbr.br[0]);
				if (tlbr.tl[1] > tlbr.br[1]) std::swap(tlbr.tl[1], tlbr.br[1]);
				return tlbr;
			}

			// All of these are scalars.
			if (ss.size() != 1) {
				throw std::runtime_error(fmt::format("You asked ArgParser to scan a scalar value, but the parsed size of the value-set was {}",ss.size()));
			}
			const Str& s = ss[0];

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

}
}
