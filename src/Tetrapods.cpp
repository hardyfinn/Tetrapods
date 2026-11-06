// Tetrapods.cpp
// Connects the engine (TetraEngine.hpp) to the panel and draws the display.
#include "plugin.hpp"
#include "TetraEngine.hpp"

using namespace tp;

static const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* ROOT_LABELS[12] = {"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};

// "D", "E -50", ...: nearest note name and how far off it is in cents
static std::string centsName(float cents) {
	int semi = (int) std::lround(cents / 100.f);
	int dev = (int) std::lround(cents - semi * 100.f);
	std::string s = NOTE_NAMES[((semi % 12) + 12) % 12];
	if (dev != 0) s += string::f("%+d", dev);
	return s;
}

struct GateLenQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float v = getValue();
		if (v >= 0.995f) return "tie (legato, gate stays high)";
		return string::f("%.0f%% of the clock", v * 100.f);
	}
};

struct Tetrapods : Module {
	enum ParamId {
		ENUMS(GENUS_PARAM, NUM_ROWS), ENUMS(SPREAD_PARAM, NUM_ROWS), ENUMS(LEAP_PARAM, NUM_ROWS), ENUMS(REV_PARAM, NUM_ROWS),
		ENUMS(NOTE_PARAM, NUM_NOTES),
		ROOT_PARAM, JOIN_PARAM, RESEED_PARAM,
		ENUMS(PAT_PARAM, NUM_PATTERNS), CHAIN_PARAM,
		ENUMS(PATH_PARAM, NUM_TRACKS), ENUMS(SRC_PARAM, NUM_TRACKS), ENUMS(OCT_PARAM, NUM_TRACKS),
		ENUMS(LEN_PARAM, NUM_TRACKS), ENUMS(PROB_PARAM, NUM_TRACKS), ENUMS(MODE_PARAM, NUM_TRACKS),
		PARAMS_LEN
	};
	enum InputId {
		ROOT_INPUT, RESEED_INPUT, PAT_INPUT, NEXT_INPUT, RESET_INPUT,
		ENUMS(CLOCK_INPUT, NUM_TRACKS),
		INPUTS_LEN
	};
	enum OutputId { ENUMS(VOCT_OUTPUT, NUM_TRACKS), ENUMS(GATE_OUTPUT, NUM_TRACKS), EOC_OUTPUT, OUTPUTS_LEN };
	enum LightId {
		ENUMS(NOTE_LIGHT, NUM_NOTES * 2), ENUMS(REV_LIGHT, NUM_ROWS), ENUMS(PAT_LIGHT, NUM_PATTERNS),
		ENUMS(MODE_LIGHT, NUM_TRACKS), ENUMS(GATE_LIGHT, NUM_TRACKS),
		LIGHTS_LEN
	};

	TetraEngine e;
	bool quantiseRoot = true;

	dsp::SchmittTrigger clockTrig[NUM_TRACKS], resetTrig, reseedTrig, nextTrig;
	dsp::BooleanTrigger noteBtn[NUM_NOTES], patBtn[NUM_PATTERNS], reseedBtn;
	dsp::PulseGenerator eocPulse;
	float sinceClock[NUM_TRACKS] = {};
	float period[NUM_TRACKS] = {0.5f, 0.5f, 0.5f, 0.5f};
	float gateLeft[NUM_TRACKS] = {};
	bool tie[NUM_TRACKS] = {};
	int voices[NUM_TRACKS] = {1, 1, 1, 1};
	float held[NUM_TRACKS][NUM_NOTES] = {};

	// for the display and lights
	float flash[NUM_NOTES] = {};
	int lastNote[NUM_TRACKS] = {-1, -1, -1, -1};
	bool needsRecall = true;   // write the current pattern into the panel controls
	int lastPatCv = -1;

	Tetrapods() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> genera;
		for (int g = 0; g < NUM_GENERA; g++) genera.push_back(GENERA[g].name);
		const char* rowName[NUM_ROWS] = {"Lower", "Upper"};
		for (int r = 0; r < NUM_ROWS; r++) {
			configSwitch(GENUS_PARAM + r, 0.f, NUM_GENERA - 1, 0.f, string::f("%s tetrachord genus", rowName[r]), genera);
			configParam(SPREAD_PARAM + r, 0.f, 3.f, 1.f, string::f("%s spread: how far leaping notes jump", rowName[r]), " octaves");
			configParam(LEAP_PARAM + r, 0.f, 1.f, 0.f, string::f("%s leap: how many notes jump up", rowName[r]), "%", 0.f, 100.f);
			configSwitch(REV_PARAM + r, 0.f, 1.f, 0.f, string::f("%s reverse", rowName[r]), {"Off", "On: this row plays top to bottom"});
			for (int k = 0; k < ROW_NOTES; k++)
				configButton(NOTE_PARAM + r * ROW_NOTES + k, string::f("%s note %d on/off", rowName[r], k + 1));
		}
		std::vector<std::string> roots(ROOT_LABELS, ROOT_LABELS + 12);
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root", roots);
		configSwitch(JOIN_PARAM, 0.f, 1.f, 0.f, "Join", {"Disjunct: upper starts a fifth above the root", "Conjunct: upper starts on the lower's last note"});
		configButton(RESEED_PARAM, "Reseed: new leaps and new Random / Drunk paths");
		for (int i = 0; i < NUM_PATTERNS; i++) configButton(PAT_PARAM + i, string::f("Pattern %d", i + 1));
		configParam(CHAIN_PARAM, 1.f, 8.f, 1.f, "Chain: NEXT steps through patterns 1 to", "");
		getParamQuantity(CHAIN_PARAM)->snapEnabled = true;

		std::vector<std::string> paths(PATH_NAMES, PATH_NAMES + NUM_PATHS);
		for (int t = 0; t < NUM_TRACKS; t++) {
			int n = t + 1;
			configSwitch(PATH_PARAM + t, 0.f, NUM_PATHS - 1, 0.f, string::f("Track %d path", n), paths);
			configSwitch(SRC_PARAM + t, 0.f, 2.f, 2.f, string::f("Track %d source", n), {"Lower tetrachord", "Upper tetrachord", "Both (8 notes)"});
			configParam(OCT_PARAM + t, -2.f, 2.f, t == 0 ? -1.f : 0.f, string::f("Track %d octave", n));
			getParamQuantity(OCT_PARAM + t)->snapEnabled = true;
			configParam<GateLenQuantity>(LEN_PARAM + t, 0.02f, 1.f, 0.5f, string::f("Track %d gate length", n));
			configParam(PROB_PARAM + t, 0.f, 1.f, 1.f, string::f("Track %d gate chance", n), "%", 0.f, 100.f);
			configSwitch(MODE_PARAM + t, 0.f, 1.f, 0.f, string::f("Track %d mode", n), {"Arp: one note per clock", "Chord: every note at once (poly), path picks the inversion"});
			configInput(CLOCK_INPUT + t, t == 0 ? "Track 1 clock" : string::f("Track %d clock (unpatched: uses track %d's)", n, n - 1));
			configOutput(VOCT_OUTPUT + t, string::f("Track %d V/oct (poly in chord mode)", n));
			configOutput(GATE_OUTPUT + t, string::f("Track %d gate", n));
		}
		configInput(ROOT_INPUT, "Root transpose (V/oct, adds to the knob)");
		configInput(RESEED_INPUT, "Reseed trigger");
		configInput(PAT_INPUT, "Pattern select (each 1.25 V = next pattern; overrides the buttons)");
		configInput(NEXT_INPUT, "Next pattern trigger (within the chain)");
		configInput(RESET_INPUT, "Reset (the next clock starts every path from the top)");
		configOutput(EOC_OUTPUT, "End of cycle (trigger when track 1's path wraps)");
	}

	void onReset() override {
		for (int i = 0; i < NUM_PATTERNS; i++) e.pat[i].init();
		e.cur = 0;
		e.reset();
		needsRecall = true;
	}

	void selectPattern(int i) {
		if (i == e.cur) return;
		e.cur = i;
		needsRecall = true;
	}

	// pattern -> panel (after a pattern change), panel -> pattern (live edits)
	void syncPattern() {
		Pattern& q = e.p();
		if (needsRecall) {
			for (int r = 0; r < NUM_ROWS; r++) {
				params[GENUS_PARAM + r].setValue(q.genus[r]);
				params[REV_PARAM + r].setValue(q.rev[r] ? 1.f : 0.f);
			}
			params[JOIN_PARAM].setValue(q.join);
			needsRecall = false;
		}
		for (int r = 0; r < NUM_ROWS; r++) {
			q.genus[r] = clamp((int) std::round(params[GENUS_PARAM + r].getValue()), 0, NUM_GENERA - 1);
			q.rev[r] = params[REV_PARAM + r].getValue() > 0.5f;
		}
		q.join = params[JOIN_PARAM].getValue() > 0.5f ? JOIN_CONJUNCT : JOIN_DISJUNCT;
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;

		// pattern selection: CV wins, otherwise buttons and NEXT
		int chain = clamp((int) std::round(params[CHAIN_PARAM].getValue()), 1, NUM_PATTERNS);
		if (inputs[PAT_INPUT].isConnected()) {
			int s = clamp((int) std::floor(inputs[PAT_INPUT].getVoltage() * 0.8f + 1e-3f), 0, NUM_PATTERNS - 1);
			if (s != lastPatCv) { selectPattern(s); lastPatCv = s; }
		}
		else {
			lastPatCv = -1;
			if (nextTrig.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f))
				selectPattern(e.cur + 1 >= chain ? 0 : e.cur + 1);
		}
		for (int i = 0; i < NUM_PATTERNS; i++)
			if (patBtn[i].process(params[PAT_PARAM + i].getValue() > 0.f)) selectPattern(i);
		syncPattern();

		for (int i = 0; i < NUM_NOTES; i++)
			if (noteBtn[i].process(params[NOTE_PARAM + i].getValue() > 0.f))
				e.p().on[i / ROW_NOTES][i % ROW_NOTES] ^= true;

		for (int r = 0; r < NUM_ROWS; r++) {
			e.spread[r] = params[SPREAD_PARAM + r].getValue();
			e.leap[r] = params[LEAP_PARAM + r].getValue();
		}
		float rootCv = inputs[ROOT_INPUT].getVoltage() * 12.f;
		if (quantiseRoot) rootCv = std::round(rootCv);
		e.rootSemis = std::round(params[ROOT_PARAM].getValue()) + rootCv;

		if (reseedBtn.process(params[RESEED_PARAM].getValue() > 0.f) | reseedTrig.process(inputs[RESEED_INPUT].getVoltage(), 0.1f, 1.f))
			e.reseed(random::u32());

		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			e.reset();
		}

		float clockV = 0.f;
		for (int t = 0; t < NUM_TRACKS; t++) {
			Track& tr = e.track[t];
			tr.path = clamp((int) std::round(params[PATH_PARAM + t].getValue()), 0, NUM_PATHS - 1);
			tr.source = clamp((int) std::round(params[SRC_PARAM + t].getValue()), 0, 2);
			tr.octave = clamp((int) std::round(params[OCT_PARAM + t].getValue()), -2, 2);
			tr.chord = params[MODE_PARAM + t].getValue() > 0.5f;

			if (inputs[CLOCK_INPUT + t].isConnected()) clockV = inputs[CLOCK_INPUT + t].getVoltage();
			sinceClock[t] += dt;
			if (clockTrig[t].process(clockV, 0.1f, 1.f)) {
				if (sinceClock[t] < 4.f) period[t] = sinceClock[t];
				sinceClock[t] = 0.f;
				StepOut o = e.clock(t);
				if (t == 0 && o.endOfCycle) eocPulse.trigger(1e-3f);
				if (o.voices > 0) {
					voices[t] = o.voices;
					for (int v = 0; v < o.voices; v++) {
						held[t][v] = o.volts[v];
						flash[o.note[v]] = 1.f;
					}
					lastNote[t] = o.note[0];
					float len = params[LEN_PARAM + t].getValue();
					float prob = params[PROB_PARAM + t].getValue();
					bool play = prob >= 1.f || random::uniform() < prob;
					tie[t] = play && len >= 0.995f;
					gateLeft[t] = play ? std::max(1e-3f, len * period[t]) : 0.f;
				}
				else {
					gateLeft[t] = 0.f;
					tie[t] = false;
					lastNote[t] = -1;
				}
			}
			gateLeft[t] -= dt;
			bool gate = tie[t] || gateLeft[t] > 0.f;

			int ch = voices[t];
			outputs[VOCT_OUTPUT + t].setChannels(ch);
			outputs[GATE_OUTPUT + t].setChannels(ch);
			for (int v = 0; v < ch; v++) {
				outputs[VOCT_OUTPUT + t].setVoltage(held[t][v], v);
				outputs[GATE_OUTPUT + t].setVoltage(gate ? 10.f : 0.f, v);
			}
			lights[GATE_LIGHT + t].setBrightness(gate ? 1.f : 0.f);
		}
		outputs[EOC_OUTPUT].setVoltage(eocPulse.process(dt) ? 10.f : 0.f);

		// lights
		const Pattern& q = e.p();
		for (int i = 0; i < NUM_NOTES; i++) {
			flash[i] = std::max(0.f, flash[i] - dt * 8.f);
			bool on = q.on[i / ROW_NOTES][i % ROW_NOTES];
			lights[NOTE_LIGHT + 2 * i].setBrightness(on ? 0.55f * (1.f - flash[i]) : 0.f);
			lights[NOTE_LIGHT + 2 * i + 1].setBrightness(flash[i]);
		}
		for (int r = 0; r < NUM_ROWS; r++) lights[REV_LIGHT + r].setBrightness(q.rev[r] ? 1.f : 0.f);
		for (int i = 0; i < NUM_PATTERNS; i++)
			lights[PAT_LIGHT + i].setBrightness(i == e.cur ? 1.f : (i < chain ? 0.12f : 0.f));
		for (int t = 0; t < NUM_TRACKS; t++) lights[MODE_LIGHT + t].setBrightness(e.track[t].chord ? 1.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* pats = json_array();
		for (int i = 0; i < NUM_PATTERNS; i++) {
			const Pattern& q = e.pat[i];
			json_t* jp = json_object();
			json_t* on = json_array();
			for (int r = 0; r < NUM_ROWS; r++)
				for (int k = 0; k < ROW_NOTES; k++) json_array_append_new(on, json_boolean(q.on[r][k]));
			json_object_set_new(jp, "on", on);
			json_object_set_new(jp, "genusLower", json_integer(q.genus[0]));
			json_object_set_new(jp, "genusUpper", json_integer(q.genus[1]));
			json_object_set_new(jp, "revLower", json_boolean(q.rev[0]));
			json_object_set_new(jp, "revUpper", json_boolean(q.rev[1]));
			json_object_set_new(jp, "join", json_integer(q.join));
			json_array_append_new(pats, jp);
		}
		json_object_set_new(root, "patterns", pats);
		json_object_set_new(root, "current", json_integer(e.cur));
		json_object_set_new(root, "seed", json_integer(e.seed));
		json_object_set_new(root, "quantiseRoot", json_boolean(quantiseRoot));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* pats = json_object_get(root, "patterns");
		for (int i = 0; i < NUM_PATTERNS && pats; i++) {
			json_t* jp = json_array_get(pats, i);
			if (!jp) continue;
			Pattern& q = e.pat[i];
			q.init();
			json_t* on = json_object_get(jp, "on");
			for (int n = 0; n < NUM_NOTES && on; n++) {
				json_t* v = json_array_get(on, n);
				if (v) q.on[n / ROW_NOTES][n % ROW_NOTES] = json_is_true(v);
			}
			auto geti = [&](const char* k, int lo, int hi) {
				json_t* v = json_object_get(jp, k);
				return v ? clamp((int) json_integer_value(v), lo, hi) : lo;
			};
			q.genus[0] = geti("genusLower", 0, NUM_GENERA - 1);
			q.genus[1] = geti("genusUpper", 0, NUM_GENERA - 1);
			q.join = geti("join", 0, 1);
			q.rev[0] = json_is_true(json_object_get(jp, "revLower"));
			q.rev[1] = json_is_true(json_object_get(jp, "revUpper"));
		}
		json_t* c = json_object_get(root, "current");
		if (c) e.cur = clamp((int) json_integer_value(c), 0, NUM_PATTERNS - 1);
		json_t* s = json_object_get(root, "seed");
		if (s) e.reseed((uint32_t) json_integer_value(s));
		json_t* qr = json_object_get(root, "quantiseRoot");
		if (qr) quantiseRoot = json_is_true(qr);
		needsRecall = true;
	}
};

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
static const NVGcolor D_BG = nvgRGB(0x12, 0x15, 0x16);
static const NVGcolor D_INK = nvgRGB(0xEE, 0xEC, 0xE4);
static const NVGcolor D_SOFT = nvgRGB(0x7E, 0x86, 0x84);
static const NVGcolor D_BOX = nvgRGB(0x24, 0x2A, 0x2B);
static const NVGcolor D_OFF = nvgRGB(0x1A, 0x1E, 0x1F);
static const NVGcolor D_ACCENT = nvgRGB(0x3F, 0xB8, 0xA8);
static const NVGcolor TRACK_COL[NUM_TRACKS] = {
	nvgRGB(0xE8, 0x5D, 0x4A), nvgRGB(0xE8, 0xB8, 0x3A), nvgRGB(0x4F, 0xC0, 0xD8), nvgRGB(0xB4, 0x8C, 0xE8)};

struct TetraDisplay : Widget {
	Tetrapods* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, W, H, mm2px(1.4f));
		nvgFillColor(vg, D_BG);
		nvgFill(vg);
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font || font->handle < 0) { Widget::drawLayer(args, layer); return; }
		nvgFontFaceId(vg, font->handle);

		TetraEngine preview;   // shown in the module browser
		const TetraEngine& e = module ? module->e : preview;
		const Pattern& q = e.p();
		float pad = mm2px(1.4f);

		// header
		nvgFontSize(vg, mm2px(2.3f));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgFillColor(vg, D_ACCENT);
		std::string head = string::f("P%d", e.cur + 1);
		nvgText(vg, pad, pad, head.c_str(), NULL);
		nvgFillColor(vg, D_INK);
		std::string rootName = centsName(e.rootSemis * 100.f);
		head = string::f("ROOT %s   %s + %s   %s", rootName.c_str(), GENERA[q.genus[0]].shortName, GENERA[q.genus[1]].shortName,
			q.join == JOIN_DISJUNCT ? "disjunct" : "conjunct");
		nvgText(vg, pad + mm2px(6.f), pad, head.c_str(), NULL);

		// note grid: upper row on top
		float gridW = W * 0.66f;
		float top = pad + mm2px(3.8f);
		float rowH = (H - top - pad - mm2px(1.0f)) / 2.f;
		float boxW = (gridW - pad - mm2px(0.8f) * 3) / 4.f;
		for (int row = 1; row >= 0; row--) {
			float y = top + (1 - row) * (rowH + mm2px(1.0f));
			for (int k = 0; k < ROW_NOTES; k++) {
				int note = row * ROW_NOTES + k;
				bool on = q.on[row][k];
				float x = pad + k * (boxW + mm2px(0.8f));
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x, y, boxW, rowH, mm2px(0.6f));
				nvgFillColor(vg, on ? D_BOX : D_OFF);
				nvgFill(vg);
				float fl = module ? module->flash[note] : 0.f;
				if (fl > 0.f) {
					nvgStrokeColor(vg, nvgTransRGBAf(D_INK, fl));
					nvgStrokeWidth(vg, 1.2f);
					nvgStroke(vg);
				}
				float cents = e.rootSemis * 100.f + e.noteCents(row, k);
				nvgFontSize(vg, mm2px(2.6f));
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				nvgFillColor(vg, on ? D_INK : D_SOFT);
				nvgText(vg, x + mm2px(1.0f), y + rowH * 0.42f, centsName(cents).c_str(), NULL);
				int lp = e.leapOctaves(row, k);
				if (lp > 0 && on) {
					nvgFontSize(vg, mm2px(1.8f));
					nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
					nvgFillColor(vg, D_ACCENT);
					nvgText(vg, x + boxW - mm2px(0.6f), y + mm2px(0.4f), string::f("+%d", lp).c_str(), NULL);
				}
				// track dots: which tracks played this note last
				for (int t = 0; t < NUM_TRACKS && module; t++) {
					if (module->lastNote[t] != note) continue;
					nvgBeginPath(vg);
					nvgCircle(vg, x + mm2px(1.4f) + t * mm2px(1.9f), y + rowH - mm2px(1.1f), mm2px(0.6f));
					nvgFillColor(vg, TRACK_COL[t]);
					nvgFill(vg);
				}
			}
		}

		// track summary on the right
		float tx = gridW + mm2px(2.0f);
		float lineH = (H - top - pad) / NUM_TRACKS;
		static const char* SRC[3] = {"LOW", "UP", "BOTH"};
		for (int t = 0; t < NUM_TRACKS; t++) {
			const Track& tr = e.track[t];
			float y = top + lineH * (t + 0.5f);
			nvgBeginPath(vg);
			nvgCircle(vg, tx + mm2px(0.8f), y, mm2px(0.8f));
			nvgFillColor(vg, TRACK_COL[t]);
			nvgFill(vg);
			nvgFontSize(vg, mm2px(2.1f));
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, D_INK);
			std::string s = string::f("%-4s %-4s %s %+d", PATH_SHORT[tr.path], SRC[tr.source], tr.chord ? "CHD" : "ARP", tr.octave);
			nvgText(vg, tx + mm2px(2.6f), y, s.c_str(), NULL);
		}
		Widget::drawLayer(args, layer);
	}
};

// ---------------------------------------------------------------------------
// Panel. Positions in mm must match res/make_panel.py
// ---------------------------------------------------------------------------
namespace layout {
static const float ROW_Y[NUM_ROWS] = {59.f, 44.f};     // lower row sits below the upper
static const float GENUS_X = 14.f, NOTE_X0 = 30.f, NOTE_DX = 11.f, SPREAD_X = 79.f, LEAP_X = 93.f, REV_X = 106.f;
static const float PAT_Y = 74.f, PAT_X0 = 12.f, PAT_DX = 10.5f;
static const float TRACK_X[NUM_TRACKS] = {22.f, 61.5f, 101.f, 140.5f};
static const float TR_Y1 = 89.5f, TR_Y2 = 102.5f, TR_Y3 = 116.5f;
}

struct TetrapodsWidget : ModuleWidget {
	TetrapodsWidget(Tetrapods* module) {
		using namespace layout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Tetrapods.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		TetraDisplay* d = createWidget<TetraDisplay>(mm2px(Vec(6.0, 12.0)));
		d->box.size = mm2px(Vec(150.56, 19.0));
		d->module = module;
		addChild(d);

		auto in = [&](float x, float y, int i) { addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, i)); };
		auto out = [&](float x, float y, int o) { addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, o)); };

		for (int r = 0; r < NUM_ROWS; r++) {
			float y = ROW_Y[r];
			addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(GENUS_X, y)), module, Tetrapods::GENUS_PARAM + r));
			for (int k = 0; k < ROW_NOTES; k++) {
				int n = r * ROW_NOTES + k;
				addParam(createLightParamCentered<VCVLightBezel<GreenRedLight>>(mm2px(Vec(NOTE_X0 + k * NOTE_DX, y)), module,
					Tetrapods::NOTE_PARAM + n, Tetrapods::NOTE_LIGHT + 2 * n));
			}
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(SPREAD_X, y)), module, Tetrapods::SPREAD_PARAM + r));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(LEAP_X, y)), module, Tetrapods::LEAP_PARAM + r));
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(mm2px(Vec(REV_X, y)), module,
				Tetrapods::REV_PARAM + r, Tetrapods::REV_LIGHT + r));
		}
		// right of the rows: root, join, reseed
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(127.f, 44.f)), module, Tetrapods::ROOT_PARAM));
		in(147.f, 44.f, Tetrapods::ROOT_INPUT);
		addParam(createParamCentered<CKSS>(mm2px(Vec(124.f, 59.f)), module, Tetrapods::JOIN_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(137.f, 59.f)), module, Tetrapods::RESEED_PARAM));
		in(149.f, 59.f, Tetrapods::RESEED_INPUT);

		// patterns
		for (int i = 0; i < NUM_PATTERNS; i++)
			addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(PAT_X0 + i * PAT_DX, PAT_Y)), module,
				Tetrapods::PAT_PARAM + i, Tetrapods::PAT_LIGHT + i));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(98.f, PAT_Y)), module, Tetrapods::CHAIN_PARAM));
		in(111.f, PAT_Y, Tetrapods::PAT_INPUT);
		in(123.f, PAT_Y, Tetrapods::NEXT_INPUT);
		in(135.f, PAT_Y, Tetrapods::RESET_INPUT);
		out(149.f, PAT_Y, Tetrapods::EOC_OUTPUT);

		// tracks
		for (int t = 0; t < NUM_TRACKS; t++) {
			float x = TRACK_X[t];
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x - 11.f, TR_Y1)), module, Tetrapods::PATH_PARAM + t));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x, TR_Y1)), module, Tetrapods::SRC_PARAM + t));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x + 11.f, TR_Y1)), module, Tetrapods::OCT_PARAM + t));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x - 11.f, TR_Y2)), module, Tetrapods::LEN_PARAM + t));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(x, TR_Y2)), module, Tetrapods::PROB_PARAM + t));
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(x + 11.f, TR_Y2)), module,
				Tetrapods::MODE_PARAM + t, Tetrapods::MODE_LIGHT + t));
			in(x - 11.f, TR_Y3, Tetrapods::CLOCK_INPUT + t);
			out(x, TR_Y3, Tetrapods::VOCT_OUTPUT + t);
			out(x + 11.f, TR_Y3, Tetrapods::GATE_OUTPUT + t);
			addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x + 15.6f, TR_Y3 - 4.4f)), module, Tetrapods::GATE_LIGHT + t));
		}
	}

	void appendContextMenu(Menu* menu) override {
		Tetrapods* m = getModule<Tetrapods>();
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Round root CV to semitones", "", &m->quantiseRoot));
		menu->addChild(createSubmenuItem("Copy this pattern to", "", [=](Menu* sub) {
			for (int i = 0; i < NUM_PATTERNS; i++) {
				sub->addChild(createMenuItem(string::f("Pattern %d", i + 1), i == m->e.cur ? "(this one)" : "", [=]() {
					m->e.pat[i] = m->e.pat[m->e.cur];
				}));
			}
		}));
		menu->addChild(createMenuItem("Clear this pattern", "", [=]() {
			m->e.p().init();
			m->needsRecall = true;
		}));
	}
};

Model* modelTetrapods = createModel<Tetrapods, TetrapodsWidget>("Tetrapods");
