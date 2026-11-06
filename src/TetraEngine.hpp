// TetraEngine.hpp
// The Tetrapods sequencer brain, with no VCV code in it so it can be tested on
// its own (test/engine_test.cpp).
//
// A scale is built from two tetrachords: four notes inside the frame of a
// fourth. The LOWER tetrachord starts on the root, the UPPER one starts a
// fifth above (disjunct, like a major scale) or on the last note of the lower
// one (conjunct). Each of the 8 notes can be switched on or off.
//
// Four tracks walk over the notes that are on. Every clock a track picks its
// next note along a PATH and outputs it as V/oct plus a gate.
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace tp {

static const int NUM_ROWS = 2;      // 0 = lower tetrachord, 1 = upper
static const int ROW_NOTES = 4;
static const int NUM_NOTES = NUM_ROWS * ROW_NOTES;
static const int NUM_TRACKS = 4;
static const int NUM_PATTERNS = 8;

// --- genera: the four notes of a tetrachord, in cents above its first note ---
struct Genus { const char* name; const char* shortName; float cents[ROW_NOTES]; };
static const int NUM_GENERA = 10;
static const Genus GENERA[NUM_GENERA] = {
	{"Major (tone, tone, semitone)",              "MAJ",  {0, 200, 400, 500}},
	{"Minor (tone, semitone, tone)",              "MIN",  {0, 200, 300, 500}},
	{"Phrygian (semitone, tone, tone)",           "PHR",  {0, 100, 300, 500}},
	{"Lydian (three tones, tritone frame)",       "LYD",  {0, 200, 400, 600}},
	{"Hijaz (semitone, augmented 2nd, semitone)", "HIJ",  {0, 100, 400, 500}},
	{"Chromatic (semitone, semitone, minor 3rd)", "CHR",  {0, 100, 200, 500}},
	{"Rast (tone, 3/4 tone, 3/4 tone)",           "RAST", {0, 200, 350, 500}},
	{"Bayati (3/4 tone, 3/4 tone, tone)",         "BAY",  {0, 150, 300, 500}},
	{"Saba (3/4, 3/4, semitone: diminished 4th)", "SABA", {0, 150, 300, 400}},
	{"Enharmonic (quarter, quarter, major 3rd)",  "ENH",  {0, 50, 100, 500}},
};

enum Join { JOIN_DISJUNCT = 0, JOIN_CONJUNCT = 1 };
enum Source { SRC_LOWER = 0, SRC_UPPER = 1, SRC_BOTH = 2 };
enum Path { PATH_FWD = 0, PATH_REV, PATH_PENDULUM, PATH_HOPSCOTCH, PATH_CONVERGE, PATH_RANDOM, PATH_DRUNK, NUM_PATHS };
static const char* const PATH_NAMES[NUM_PATHS] = {
	"Forward", "Backward", "Pendulum", "Hopscotch (back to the first note between each)",
	"Converge (outside in)", "Random (locked until Reseed)", "Drunk (random walk)"};
static const char* const PATH_SHORT[NUM_PATHS] = {"FWD", "REV", "PEND", "HOP", "CONV", "RAND", "DRNK"};

// What a pattern slot remembers
struct Pattern {
	bool on[NUM_ROWS][ROW_NOTES];
	int genus[NUM_ROWS];
	bool rev[NUM_ROWS];
	int join;
	void init() {
		for (int r = 0; r < NUM_ROWS; r++) {
			for (int k = 0; k < ROW_NOTES; k++) on[r][k] = true;
			genus[r] = 0;
			rev[r] = false;
		}
		join = JOIN_DISJUNCT;
	}
};

// small deterministic hash -> [0, 1)
static inline float hash01(uint32_t a, uint32_t b, uint32_t c) {
	uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
	h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
	return (h >> 8) * (1.f / 16777216.f);
}

struct Track {
	int path = PATH_FWD;
	int source = SRC_BOTH;
	int octave = 0;          // -2..+2
	bool chord = false;      // false = arp (one note per clock), true = chord
	int step = 0;            // next position along the path
	bool fresh = true;       // true until the first clock after a reset
	int drunk = 0;           // position for the Drunk path
	uint32_t count = 0;      // clocks since reset (drives Random)
};

// The result of one clock on one track
struct StepOut {
	int voices = 0;              // 0 = nothing to play (no notes on)
	float volts[NUM_NOTES];      // V/oct, 0 V = C4
	int note[NUM_NOTES];         // which of the 8 notes (row * 4 + slot)
	bool endOfCycle = false;     // the path just came back to its start
	int position = 0;            // index along the active notes (for display)
};

struct TetraEngine {
	Pattern pat[NUM_PATTERNS];
	int cur = 0;                 // current pattern slot
	Track track[NUM_TRACKS];
	uint32_t seed = 1;

	// per row, set from the panel every sample
	float spread[NUM_ROWS] = {0.f, 0.f};   // 0..3 octaves
	float leap[NUM_ROWS] = {0.f, 0.f};     // 0..1 how many notes leap
	float rootSemis = 0.f;                 // root offset in semitones from C

	TetraEngine() {
		for (int i = 0; i < NUM_PATTERNS; i++) pat[i].init();
	}

	Pattern& p() { return pat[cur]; }
	const Pattern& p() const { return pat[cur]; }

	// cents of note (row, slot) above the root, before any leaps
	float noteCents(int row, int slot) const {
		const Pattern& q = p();
		float c = GENERA[q.genus[row]].cents[slot];
		if (row == 1) c += q.join == JOIN_DISJUNCT ? 700.f : GENERA[q.genus[0]].cents[3];
		return c;
	}

	// how many octaves note (row, slot) leaps up, from the seeded leap pattern
	int leapOctaves(int row, int slot) const {
		if (spread[row] <= 0.f || leap[row] <= 0.f) return 0;
		float chance = hash01(seed, 11u + row, slot);
		if (chance >= leap[row]) return 0;
		float size = hash01(seed, 23u + row, slot);
		return std::max(1, (int) std::ceil(size * spread[row] - 1e-4f));
	}

	// the notes a track plays, in play order. Returns how many.
	int activeNotes(int source, int* out) const {
		const Pattern& q = p();
		int n = 0;
		for (int row = 0; row < NUM_ROWS; row++) {
			if (source == SRC_LOWER && row == 1) continue;
			if (source == SRC_UPPER && row == 0) continue;
			for (int k = 0; k < ROW_NOTES; k++) {
				int slot = q.rev[row] ? ROW_NOTES - 1 - k : k;
				if (q.on[row][slot]) out[n++] = row * ROW_NOTES + slot;
			}
		}
		return n;
	}

	// length of one cycle of a path over n notes
	static int pathLength(int path, int n) {
		if (n <= 1) return 1;
		switch (path) {
			case PATH_PENDULUM: return 2 * n - 2;
			case PATH_HOPSCOTCH: return 2 * (n - 1);
			default: return n;
		}
	}

	// index into the active notes for position s along a path
	int pathIndex(int t, int path, int n, int s) {
		if (n <= 1) return 0;
		Track& tr = track[t];
		switch (path) {
			case PATH_REV: return n - 1 - s;
			case PATH_PENDULUM: return s < n ? s : 2 * n - 2 - s;
			case PATH_HOPSCOTCH: return (s % 2 == 0) ? 0 : (s + 1) / 2;
			case PATH_CONVERGE: return (s % 2 == 0) ? s / 2 : n - 1 - s / 2;
			case PATH_RANDOM: return std::min(n - 1, (int) (hash01(seed, 101u + t, s) * n));
			case PATH_DRUNK: {
				if (tr.fresh) { tr.drunk = 0; return 0; }
				float r = hash01(seed, 211u + t, tr.count);
				int d = tr.drunk + (r < 0.5f ? -1 : 1);
				if (d < 0) d = 1;            // bounce off the ends
				if (d >= n) d = n - 2;
				tr.drunk = std::max(0, std::min(n - 1, d));
				return tr.drunk;
			}
			default: return s;
		}
	}

	float volts(int note, int octave) const {
		int row = note / ROW_NOTES, slot = note % ROW_NOTES;
		float c = noteCents(row, slot) + 1200.f * (leapOctaves(row, slot) + octave);
		return (rootSemis * 100.f + c) / 1200.f;
	}

	// advance track t by one clock
	StepOut clock(int t) {
		StepOut o;
		Track& tr = track[t];
		int act[NUM_NOTES];
		int n = activeNotes(tr.source, act);
		if (tr.fresh) { tr.step = 0; tr.count = 0; }
		if (n == 0) {
			tr.fresh = false;
			tr.count++;
			return o;
		}
		int len = pathLength(tr.path, n);
		if (tr.step >= len) tr.step = 0;          // notes were switched off
		int idx = pathIndex(t, tr.path, n, tr.step);
		idx = std::max(0, std::min(n - 1, idx));
		o.position = idx;

		if (!tr.chord) {
			o.voices = 1;
			o.note[0] = act[idx];
			o.volts[0] = volts(act[idx], tr.octave);
		}
		else {
			// all notes, sorted low to high; the lowest idx notes go up an octave (inversions)
			for (int i = 0; i < n; i++) { o.note[i] = act[i]; o.volts[i] = volts(act[i], tr.octave); }
			for (int i = 0; i < n; i++)
				for (int j = i + 1; j < n; j++)
					if (o.volts[j] < o.volts[i]) { std::swap(o.volts[i], o.volts[j]); std::swap(o.note[i], o.note[j]); }
			for (int i = 0; i < idx; i++) o.volts[i] += 1.f;
			o.voices = n;
		}

		tr.fresh = false;
		tr.count++;
		tr.step++;
		if (tr.step >= len) { tr.step = 0; o.endOfCycle = true; }
		return o;
	}

	void reset() {
		for (int t = 0; t < NUM_TRACKS; t++) track[t].fresh = true;
	}

	void reseed(uint32_t s) { seed = s ? s : 1; }
};

}  // namespace tp
