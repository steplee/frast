#pragma once

#include <cstdint>

namespace frast {

constexpr int	   MAX_LVLS				  = 26;

// Note: WebMercator the size of the map is actually 2x this.
constexpr double WebMercatorMapScale = 20037508.342789248;
// Level 0 holds 2*WebMercatorScale, Level 1 half that, and so on.
constexpr double WebMercatorCellSizes[MAX_LVLS] = {
	40075016.685578495, 20037508.342789248, 10018754.171394624, 5009377.085697312,	2504688.542848656,
	1252344.271424328,	626172.135712164,	313086.067856082,	156543.033928041,	78271.5169640205,
	39135.75848201025,	19567.879241005125, 9783.939620502562,	4891.969810251281,	2445.9849051256406,
	1222.9924525628203, 611.4962262814101,	305.7481131407051,	152.87405657035254, 76.43702828517627,
	38.218514142588134, 19.109257071294067, 9.554628535647034,	4.777314267823517,	2.3886571339117584,
	1.1943285669558792};
constexpr float WebMercatorCellSizesf[MAX_LVLS] = {
	40075016.685578495, 20037508.342789248, 10018754.171394624, 5009377.085697312,	2504688.542848656,
	1252344.271424328,	626172.135712164,	313086.067856082,	156543.033928041,	78271.5169640205,
	39135.75848201025,	19567.879241005125, 9783.939620502562,	4891.969810251281,	2445.9849051256406,
	1222.9924525628203, 611.4962262814101,	305.7481131407051,	152.87405657035254, 76.43702828517627,
	38.218514142588134, 19.109257071294067, 9.554628535647034,	4.777314267823517,	2.3886571339117584,
	1.1943285669558792};

struct BlockCoordinate {
	uint64_t c;
	inline BlockCoordinate(uint64_t cc) : c(cc) {}
	inline BlockCoordinate(const BlockCoordinate &bc) : c(bc.c) {}
	inline BlockCoordinate(uint64_t z, uint64_t y, uint64_t x) : c(z << 58 | y << 29 | x) {}

	inline uint64_t z() const { return (c >> 58) & 0b111111; }
	inline uint64_t y() const { return (c >> 29) & 0b11111111111111111111111111111; }
	inline uint64_t x() const { return (c)&0b11111111111111111111111111111; }

	inline bool operator==(const BlockCoordinate &other) const { return c == other.c; }
	inline		operator uint64_t() const { return c; }
	inline		operator const uint64_t *() const { return &c; }
	inline		operator void *() const { return (void *)&c; }
};
static_assert(sizeof(BlockCoordinate) == 8);

}
