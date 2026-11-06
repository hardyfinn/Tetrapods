// engine_test.cpp: checks for TetraEngine.hpp. No VCV needed.
//   c++ -std=c++11 -O2 -o test/engine_test test/engine_test.cpp && test/engine_test
#include "../src/TetraEngine.hpp"
#include <cstdio>
#include <vector>

using namespace tp;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

static std::vector<int> run(TetraEngine& e, int t, int steps, std::vector<bool>* eoc = nullptr) {
	std::vector<int> v;
	for (int i = 0; i < steps; i++) {
		StepOut o = e.clock(t);
		v.push_back(o.voices ? o.note[0] : -1);
		if (eoc) eoc->push_back(o.endOfCycle);
	}
	return v;
}

int main() {
	// major scale from two major tetrachords, disjunct
	{
		TetraEngine e;
		float expect[8] = {0, 200, 400, 500, 700, 900, 1100, 1200};
		for (int i = 0; i < 8; i++) {
			StepOut o = e.clock(0);
			CHECK(o.voices == 1);
			CHECK(near(o.volts[0], expect[i] / 1200.f));
			CHECK(o.endOfCycle == (i == 7));
		}
		// wraps
		CHECK(near(e.clock(0).volts[0], 0.f));
	}
	// conjunct: upper starts on the 4th, shares the note
	{
		TetraEngine e;
		e.p().join = JOIN_CONJUNCT;
		e.p().genus[0] = 1;   // minor
		float expect[8] = {0, 200, 300, 500, 500, 700, 900, 1000};
		for (int i = 0; i < 8; i++) CHECK(near(e.clock(0).volts[0], expect[i] / 1200.f));
	}
	// quarter tones and root
	{
		TetraEngine e;
		e.p().genus[0] = 6;  // rast
		e.rootSemis = 2.f;   // D
		e.track[0].source = SRC_LOWER;
		float expect[4] = {200, 400, 550, 700};
		for (int i = 0; i < 4; i++) CHECK(near(e.clock(0).volts[0], expect[i] / 1200.f));
	}
	// paths over 4 notes
	{
		TetraEngine e;
		e.track[0].source = SRC_LOWER;
		e.track[0].path = PATH_HOPSCOTCH;
		std::vector<bool> eoc;
		std::vector<int> v = run(e, 0, 6, &eoc);
		std::vector<int> want = {0, 1, 0, 2, 0, 3};
		CHECK(v == want);
		CHECK(eoc[5] && !eoc[4]);
		e.reset();
		e.track[0].path = PATH_CONVERGE;
		CHECK(run(e, 0, 4) == (std::vector<int>{0, 3, 1, 2}));
		e.reset();
		e.track[0].path = PATH_PENDULUM;
		CHECK(run(e, 0, 7) == (std::vector<int>{0, 1, 2, 3, 2, 1, 0}));
		e.reset();
		e.track[0].path = PATH_REV;
		CHECK(run(e, 0, 4) == (std::vector<int>{3, 2, 1, 0}));
	}
	// row reverse and switched-off notes
	{
		TetraEngine e;
		e.p().rev[0] = true;
		e.p().on[1][1] = false;
		CHECK(run(e, 0, 7) == (std::vector<int>{3, 2, 1, 0, 4, 6, 7}));
		// all off: nothing plays, no crash
		for (int r = 0; r < 2; r++) for (int k = 0; k < 4; k++) e.p().on[r][k] = false;
		CHECK(e.clock(0).voices == 0);
		e.p().on[0][2] = true;
		StepOut o = e.clock(0);
		CHECK(o.voices == 1 && o.note[0] == 2);
	}
	// random path is locked until reseed, and stays in range; drunk moves by one
	{
		TetraEngine e;
		e.track[0].path = PATH_RANDOM;
		std::vector<int> a = run(e, 0, 16);
		e.reset();
		std::vector<int> b = run(e, 0, 16);
		CHECK(a == b);
		e.reseed(12345); e.reset();
		std::vector<int> c = run(e, 0, 16);
		CHECK(a != c);
		for (int x : c) CHECK(x >= 0 && x < 8);
		e.track[1].path = PATH_DRUNK;
		std::vector<int> d = run(e, 1, 64);
		CHECK(d[0] == 0);
		for (size_t i = 1; i < d.size(); i++) CHECK(std::abs(d[i] - d[i - 1]) == 1);
	}
	// leaps: none at zero, whole octaves only, change with the seed
	{
		TetraEngine e;
		e.spread[0] = 2.f; e.leap[0] = 0.f;
		for (int k = 0; k < 4; k++) CHECK(e.leapOctaves(0, k) == 0);
		e.leap[0] = 1.f;
		for (int k = 0; k < 4; k++) { int l = e.leapOctaves(0, k); CHECK(l >= 1 && l <= 2); }
		e.spread[0] = 3.f; e.leap[0] = 0.5f;
		int total = 0;
		for (uint32_t s = 1; s < 200; s++) { e.reseed(s); for (int k = 0; k < 4; k++) total += e.leapOctaves(0, k) > 0; }
		CHECK(total > 300 && total < 500);   // about half of 796
		CHECK(e.leapOctaves(1, 0) == 0);     // upper row untouched
	}
	// chord mode: all notes, low to high, inversions by path position
	{
		TetraEngine e;
		e.track[0].chord = true;
		e.track[0].source = SRC_LOWER;
		StepOut o = e.clock(0);
		CHECK(o.voices == 4);
		CHECK(near(o.volts[0], 0.f) && near(o.volts[3], 500.f / 1200.f));
		o = e.clock(0);   // first inversion: root up an octave
		CHECK(near(o.volts[0], 1.f));
		CHECK(near(o.volts[1], 200.f / 1200.f));
	}
	// tracks are independent; octave offset
	{
		TetraEngine e;
		e.track[1].octave = -1;
		e.clock(0); e.clock(0);
		StepOut o = e.clock(1);
		CHECK(near(o.volts[0], -1.f));
	}
	// patterns
	{
		TetraEngine e;
		e.pat[3].genus[0] = 2;
		e.cur = 3;
		e.track[0].source = SRC_LOWER;
		e.clock(0);
		CHECK(near(e.clock(0).volts[0], 100.f / 1200.f));
	}
	printf(fails ? "%d FAILED\n" : "all engine tests passed\n", fails);
	return fails ? 1 : 0;
}
