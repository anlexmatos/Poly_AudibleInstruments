//#include "AudibleInstruments.hpp"
#include "plugin.hpp"
#pragma GCC diagnostic push
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wsuggest-override"
#endif
#include "plaits/dsp/voice.h"
#pragma GCC diagnostic pop

#define MAX_PLAITS_VOICES 16

class NVGRestorer
{
public:
	NVGRestorer() = delete;
	NVGRestorer(NVGcontext* ctx) : mCtx(ctx) 
	{
		if (mCtx) nvgSave(mCtx);
	}
	~NVGRestorer()
	{
		if (mCtx) nvgRestore(mCtx);
	} 
private:
	NVGcontext* mCtx = nullptr;
};

struct Plaits : Module {
	enum ParamIds {
		MODEL1_PARAM,
		MODEL2_PARAM,
		FREQ_PARAM,
		HARMONICS_PARAM,
		TIMBRE_PARAM,
		MORPH_PARAM,
		TIMBRE_CV_PARAM,
		FREQ_CV_PARAM,
		MORPH_CV_PARAM,
		TIMBRE_LPG_PARAM,
		FREQ_LPG_PARAM,
		MORPH_LPG_PARAM,
		LPG_COLOR_PARAM,
		LPG_DECAY_PARAM,
		OUTMIX_PARAM,
		HARMONICS_CV_PARAM,
		HARMONICS_LPG_PARAM,
		UNISONOMODE_PARAM,
		UNISONOSPREAD_PARAM,
		OUTMIX_CV_PARAM,
		OUTMIX_LPG_PARAM,
		DECAY_CV_PARAM,
		LPG_COLOR_CV_PARAM,
		ENGINE_CV_PARAM,
		UNISONOSPREAD_CV_PARAM,
		SECONDARY_FREQ_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENGINE_INPUT,
		TIMBRE_INPUT,
		FREQ_INPUT,
		MORPH_INPUT,
		HARMONICS_INPUT,
		TRIGGER_INPUT,
		LEVEL_INPUT,
		NOTE_INPUT,
		LPG_COLOR_INPUT,
		LPG_DECAY_INPUT,
		SPREAD_INPUT,
		OUTMIX_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_OUTPUT,
		AUX_OUTPUT,
		AUX2_OUTPUT,
		PITCH_SPREAD_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(MODEL_LIGHT, 8 * 2),
		NUM_LIGHTS
	};

	plaits::Voice voice[MAX_PLAITS_VOICES];
	plaits::Patch patch[MAX_PLAITS_VOICES] = {};
	plaits::Modulations modulations[MAX_PLAITS_VOICES] = {};
	char shared_buffer[MAX_PLAITS_VOICES][16384] = {};
	float triPhase = 0.f;

	dsp::SampleRateConverter<2> outputSrc[MAX_PLAITS_VOICES];
	dsp::DoubleRingBuffer<dsp::Frame<2>, 256> outputBuffer[MAX_PLAITS_VOICES];
	bool lowCpu = false;
	bool freeTune = false;
	bool showModulations = true;
	dsp::SchmittTrigger model1Trigger;
	dsp::SchmittTrigger model2Trigger;

	float currentOutmix = 0.0f;
	float currentPitch = 0.0f;

	Plaits() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MODEL1_PARAM, 0.0, 1.0, 0.0, "Model selection 1");
		configParam(MODEL2_PARAM, 0.0, 1.0, 0.0, "Model selection 2");
		configParam(FREQ_PARAM, -4.0, 4.0, 0.0, "Coarse frequency adjustment");
		configParam(HARMONICS_PARAM, 0.0, 1.0, 0.5, "Harmonics");
		configParam(TIMBRE_PARAM, 0.0, 1.0, 0.5, "Timbre");
		configParam(MORPH_PARAM, 0.0, 1.0, 0.5, "Morph");
		configParam(TIMBRE_CV_PARAM, -1.0, 1.0, 0.0, "Timbre CV");
		configParam(FREQ_CV_PARAM, -1.0, 1.0, 0.0, "Frequency CV");
		configParam(MORPH_CV_PARAM, -1.0, 1.0, 0.0, "Morph CV");
		configParam(TIMBRE_LPG_PARAM, -1.0, 1.0, 0.0, "LPG to Timbre");
		configParam(MORPH_LPG_PARAM, -1.0, 1.0, 0.0, "LPG to Morph");
		configParam(FREQ_LPG_PARAM, -1.0, 1.0, 0.0, "LPG to Frequency");
		configParam(LPG_COLOR_PARAM, 0.0, 1.0, 0.5, "LPG Colour");
		configParam(LPG_DECAY_PARAM, 0.0, 1.0, 0.5, "LPG Decay");
		configParam(OUTMIX_PARAM, 0.0, 1.0, 0.5, "Output mix");
		configParam(HARMONICS_CV_PARAM, -1.0, 1.0, 0.0, "Harmonics CV");
		configParam(HARMONICS_LPG_PARAM, -1.0, 1.0, 0.0, "LPG to Harmonics");
		configParam(UNISONOMODE_PARAM, 1.0, 16.0, 1.0, "Unisono/Spread num voices");
		configParam(UNISONOSPREAD_PARAM, 0.0, 1.0, 0.0, "Unisono/Spread");
		configParam(OUTMIX_CV_PARAM, -1.0, 1.0, 0.0, "Output mix CV");
		configParam(OUTMIX_LPG_PARAM, -1.0, 1.0, 0.0, "Output mix LPG");
		configParam(DECAY_CV_PARAM, -1.0, 1.0, 0.0, "Decay CV");
		configParam(LPG_COLOR_CV_PARAM, -1.0, 1.0, 0.0, "LPG Colour CV");
		configParam(ENGINE_CV_PARAM, -1.0, 1.0, 0.0, "Engine choice CV");
		configParam(UNISONOSPREAD_CV_PARAM, -1.0, 1.0, 0.0, "Unisono/Spread CV");
		configParam(SECONDARY_FREQ_PARAM, -7.0, 7.0, 0.0, "Tuning");
		for (int i=0;i<MAX_PLAITS_VOICES;++i)
		{
			stmlib::BufferAllocator allocator(shared_buffer[i], sizeof(shared_buffer[i]));
			voice[i].Init(&allocator);
		}
		onReset();
		
	}

	void onReset() override {
		for (int i=0;i<MAX_PLAITS_VOICES;++i)
		{
			patch[i].engine = 0;
			patch[i].lpg_colour = 0.5f;
			patch[i].decay = 0.5f;
		}

	}

	void onRandomize() override {
		for (int i=0;i<MAX_PLAITS_VOICES;++i)
			patch[i].engine = random::u32() % 16;
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		json_object_set_new(rootJ, "lowCpu", json_boolean(lowCpu));
		json_object_set_new(rootJ, "model", json_integer(patch[0].engine));
		json_object_set_new(rootJ, "lpgColor", json_real(patch[0].lpg_colour));
		json_object_set_new(rootJ, "decay", json_real(patch[0].decay));
		json_object_set_new(rootJ, "freetune", json_boolean(freeTune));
		json_object_set_new(rootJ, "showmods", json_boolean(showModulations));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *lowCpuJ = json_object_get(rootJ, "lowCpu");
		if (lowCpuJ)
			lowCpu = json_boolean_value(lowCpuJ);

		json_t *freetuneJ = json_object_get(rootJ, "freetune");
		if (freetuneJ)
			freeTune = json_boolean_value(freetuneJ);

		json_t *showmodJ = json_object_get(rootJ, "showmods");
		if (showmodJ)
			showModulations = json_boolean_value(showmodJ);

		json_t *modelJ = json_object_get(rootJ, "model");
		if (modelJ)
			for (int i=0;i<MAX_PLAITS_VOICES;++i)
				patch[i].engine = json_integer_value(modelJ);

		json_t *lpgColorJ = json_object_get(rootJ, "lpgColor");
		if (lpgColorJ)
			for (int i=0;i<MAX_PLAITS_VOICES;++i)
				patch[i].lpg_colour = json_number_value(lpgColorJ);

		json_t *decayJ = json_object_get(rootJ, "decay");
		if (decayJ)
			for (int i=0;i<MAX_PLAITS_VOICES;++i)
				patch[i].decay = json_number_value(decayJ);
	}
	float getModulatedParamNormalized(int paramid, int whichvoice=0)
	{
		if (paramid==FREQ_PARAM)
			return rescale(voice[whichvoice].epars.note,12.0f,108.0f,0.0f,1.0f);
		if (paramid==HARMONICS_PARAM)
			return voice[whichvoice].epars.harmonics;
		if (paramid==MORPH_PARAM)
			return voice[whichvoice].epars.morph;
		if (paramid==TIMBRE_PARAM)
			return voice[whichvoice].epars.timbre;
		if (paramid==OUTMIX_PARAM)
			return currentOutmix;
		return -1.0f;
	}
	inline float getUniSpreadAmount(int numchans, int chan, float spreadpar)
	{
		// spread slowly to ± 0.5 semitones
		if (spreadpar<0.5f)
		{
			float spreadnorm = spreadpar*2.0f;
			float spreadmt = spreadnorm*0.5;
			return rescale(chan,0,numchans-1,-spreadmt,spreadmt);
		// spread faster to ± 1 octaves
		} else if (spreadpar>=0.5f && spreadpar<0.9f)
		{
			float spreadnorm = (spreadpar-0.5f)*2.0f;
			float spreadmt = rescale(spreadpar,0.5f,0.9f,0.5f,12.0f);
			return rescale(chan,0,numchans-1,-spreadmt,spreadmt);
		// finally morph towards -1, 0, +1 octave for all voices
		} else
		{
			float interpos = rescale(spreadpar,0.9f,1.0f,0.0f,1.0f);
			interpos = 1.0-(std::pow(1.0-interpos,3.0f));
			float y0 = rescale(chan,0,numchans-1,-12.0f,12.0f);
			const float pitches[3] = {-12.0f,0.0f,12.0f};
			int index = chan % 3;
			float y1 = pitches[index];
			return y0+(y1-y0)*interpos;
		}
		return 0.0f;
	}
	void process(const ProcessArgs &args) override {
		float spreadamt = params[UNISONOSPREAD_PARAM].getValue();
		if (inputs[SPREAD_INPUT].isConnected())
		{
			spreadamt += rescale(inputs[SPREAD_INPUT].getVoltage()*params[UNISONOSPREAD_CV_PARAM].getValue(),-5.0f,5.0f,-1.0f,1.0f);
			spreadamt = clamp(spreadamt,0.0f,1.0f);
		}
		int numpolychs = std::max(inputs[NOTE_INPUT].getChannels(),1);
		int unispreadchans = params[UNISONOMODE_PARAM].getValue();
		if (unispreadchans>=2)
			numpolychs = unispreadchans;
		if (outputBuffer[0].empty()) {
			const int blockSize = 12;
			float pitchAdjust = params[SECONDARY_FREQ_PARAM].getValue();
			// Model buttons
			if (model1Trigger.process(params[MODEL1_PARAM].getValue())) {
				for (int i=0;i<MAX_PLAITS_VOICES;++i)
				{
					if (patch[i].engine >= 8) {
						patch[i].engine -= 8;
					}
					else {
						patch[i].engine = (patch[i].engine + 1) % 8;
					}
				}
			}
			if (model2Trigger.process(params[MODEL2_PARAM].getValue())) {
				for (int i=0;i<MAX_PLAITS_VOICES;++i)
				{
					if (patch[i].engine < 8) {
						patch[i].engine += 8;
					}
					else {
						patch[i].engine = (patch[i].engine + 1) % 8 + 8;
					}
				}
			}

			// Model lights
			int activeEngine = voice[0].active_engine();
			triPhase += 2.f * args.sampleTime * blockSize;
			if (triPhase >= 1.f)
				triPhase -= 1.f;
			float tri = (triPhase < 0.5f) ? triPhase * 2.f : (1.f - triPhase) * 2.f;

			for (int i = 0; i < 8; i++) {
				lights[MODEL_LIGHT + 2*i + 0].setBrightness((activeEngine == i) ? 1.f : (patch[0].engine == i) ? tri : 0.f);
				lights[MODEL_LIGHT + 2*i + 1].setBrightness((activeEngine == i + 8) ? 1.f : (patch[0].engine == i + 8) ? tri : 0.f);
			}
			float pitch;
			if (!freeTune)
				pitch = std::round(params[FREQ_PARAM].getValue());
			else pitch = params[FREQ_PARAM].getValue();
			currentPitch = pitch;
			// Calculate pitch for lowCpu mode if needed
			if (lowCpu)
				pitch += log2f(48000.f * args.sampleTime);
			// Update patch
			
			for (int i=0;i<numpolychs;++i)
			{
				patch[i].note = 60.f + pitch * 12.f + pitchAdjust;
				if (unispreadchans>1)
					patch[i].note+=getUniSpreadAmount(unispreadchans,i,spreadamt);
				patch[i].harmonics = params[HARMONICS_PARAM].getValue();
				//if (!lpg) {
					patch[i].timbre = params[TIMBRE_PARAM].getValue();
					patch[i].morph = params[MORPH_PARAM].getValue();
				//}
				//else {
					float lpg_colour = params[LPG_COLOR_PARAM].getValue();
					if (inputs[LPG_COLOR_INPUT].getChannels() < 2)
						lpg_colour += inputs[LPG_COLOR_INPUT].getVoltage()/10.0f*params[LPG_COLOR_CV_PARAM].getValue();
					else lpg_colour += inputs[LPG_COLOR_INPUT].getVoltage(i)/10.0f*params[LPG_COLOR_CV_PARAM].getValue();
					patch[i].lpg_colour = clamp(lpg_colour,0.0f,1.0f);
					float decay = params[LPG_DECAY_PARAM].getValue();
					if (inputs[LPG_DECAY_INPUT].getChannels() < 2)
						decay += inputs[LPG_DECAY_INPUT].getVoltage()/10.0f*params[DECAY_CV_PARAM].getValue();
					else decay += inputs[LPG_DECAY_INPUT].getVoltage(i)/10.0f*params[DECAY_CV_PARAM].getValue();
					patch[i].decay = clamp(decay,0.0f,1.0);
				//}
				patch[i].frequency_cv_amount = params[FREQ_CV_PARAM].getValue();
				patch[i].timbre_cv_amount = params[TIMBRE_CV_PARAM].getValue();
				patch[i].morph_cv_amount = params[MORPH_CV_PARAM].getValue();
				patch[i].harmonics_cv_amount = params[HARMONICS_CV_PARAM].getValue();
				patch[i].frequency_lpg_amount = params[FREQ_LPG_PARAM].getValue();
				patch[i].timbre_lpg_amount = params[TIMBRE_LPG_PARAM].getValue();
				patch[i].morph_lpg_amount = params[MORPH_LPG_PARAM].getValue();
				patch[i].harmonics_lpg_amount = params[HARMONICS_LPG_PARAM].getValue();
				// Update modulations
				if (inputs[ENGINE_INPUT].getChannels() < 2)
					modulations[i].engine = inputs[ENGINE_INPUT].getVoltage() / 5.f * params[ENGINE_CV_PARAM].getValue();
				else
					modulations[i].engine = inputs[ENGINE_INPUT].getVoltage(i) / 5.f * params[ENGINE_CV_PARAM].getValue();
				if (unispreadchans<2)
					modulations[i].note = inputs[NOTE_INPUT].getVoltage(i) * 12.f;
				else
					modulations[i].note = inputs[NOTE_INPUT].getVoltage() * 12.f;
				if (inputs[FREQ_INPUT].getChannels() < 2)
					modulations[i].frequency = inputs[FREQ_INPUT].getVoltage() * 6.f;
				else 
					modulations[i].frequency = inputs[FREQ_INPUT].getVoltage(i) * 6.f;
				if (inputs[HARMONICS_INPUT].getChannels() < 2)
					modulations[i].harmonics = inputs[HARMONICS_INPUT].getVoltage() / 5.f;
				else
					modulations[i].harmonics = inputs[HARMONICS_INPUT].getVoltage(i) / 5.f;
				if (inputs[TIMBRE_INPUT].getChannels() < 2)
					modulations[i].timbre = inputs[TIMBRE_INPUT].getVoltage() / 8.f;
				else
					modulations[i].timbre = inputs[TIMBRE_INPUT].getVoltage(i) / 8.f;
				if (inputs[MORPH_INPUT].getChannels() < 2)
					modulations[i].morph = inputs[MORPH_INPUT].getVoltage() / 8.f;
				else
					modulations[i].morph = inputs[MORPH_INPUT].getVoltage(i) / 8.f;
				// Triggers at around 0.7 V
				if (inputs[TRIGGER_INPUT].getChannels() < 2)
					modulations[i].trigger = inputs[TRIGGER_INPUT].getVoltage() / 3.f;
				else
					modulations[i].trigger = inputs[TRIGGER_INPUT].getVoltage(i) / 3.f;
				if (inputs[LEVEL_INPUT].getChannels() < 2)
					modulations[i].level = inputs[LEVEL_INPUT].getVoltage() / 8.f;
				else
					modulations[i].level = inputs[LEVEL_INPUT].getVoltage(i) / 8.f;

				modulations[i].frequency_patched = inputs[FREQ_INPUT].isConnected();
				modulations[i].timbre_patched = inputs[TIMBRE_INPUT].isConnected();
				modulations[i].morph_patched = inputs[MORPH_INPUT].isConnected();
				modulations[i].harmonics_patched = inputs[HARMONICS_INPUT].isConnected();
				modulations[i].trigger_patched = inputs[TRIGGER_INPUT].isConnected();
				modulations[i].level_patched = inputs[LEVEL_INPUT].isConnected();
			}
			

			// Render frames
			for (int polych=0;polych<numpolychs;++polych)
			{
				plaits::Voice::Frame output[blockSize];
				voice[polych].Render(patch[polych], modulations[polych], output, blockSize);

				// Convert output to frames
				dsp::Frame<2> outputFrames[blockSize];
				for (int i = 0; i < blockSize; i++) {
					outputFrames[i].samples[0] = output[i].out / 32768.f;
					outputFrames[i].samples[1] = output[i].aux / 32768.f;
				}

				// Convert output
				if (lowCpu) {
					int len = std::min((int) outputBuffer[polych].capacity(), blockSize);
					memcpy(outputBuffer[polych].endData(), outputFrames, len * sizeof(dsp::Frame<2>));
					outputBuffer[polych].endIncr(len);
				}
				else {
					outputSrc[polych].setRates(48000, args.sampleRate);
					int inLen = blockSize;
					int outLen = outputBuffer[polych].capacity();
					outputSrc[polych].process(outputFrames, &inLen, outputBuffer[polych].endData(), &outLen);
					outputBuffer[polych].endIncr(outLen);
				}
			}
		}
		outputs[OUT_OUTPUT].setChannels(numpolychs);
		outputs[AUX_OUTPUT].setChannels(numpolychs);
		outputs[AUX2_OUTPUT].setChannels(numpolychs);
		outputs[PITCH_SPREAD_OUTPUT].setChannels(numpolychs);
		// Set output
		float outmix = params[OUTMIX_PARAM].getValue();
		outmix += rescale(inputs[OUTMIX_INPUT].getVoltage()*params[OUTMIX_CV_PARAM].getValue(),
			-5.0f,5.0f,-1.0f,1.0f);
		outmix += voice[0].getDecayEnvelopeValue()*params[OUTMIX_LPG_PARAM].getValue();
		outmix = clamp(outmix,0.0f,1.0f);
		currentOutmix = outmix;
		for (int i=0;i<numpolychs;++i)
		{
			if (!outputBuffer[i].empty()) {
				dsp::Frame<2> outputFrame = outputBuffer[i].shift();
				// Inverting op-amp on outputs
				float out1 = -outputFrame.samples[0] * 5.f;
				float out2 = -outputFrame.samples[1] * 5.f;
				outputs[OUT_OUTPUT].setVoltage(out1,i);
				outputs[AUX_OUTPUT].setVoltage(out2,i);
				float out3 = outmix*out2 + (1.0f-outmix)*out1;
				outputs[AUX2_OUTPUT].setVoltage(out3,i);
				float pitchv = getUniSpreadAmount(numpolychs,i,spreadamt);
				pitchv = rescale(pitchv,-12.0f,12.0f,-5.0f,5.0f);
				outputs[PITCH_SPREAD_OUTPUT].setVoltage(pitchv,i);
			}
		}
	}
};


static const std::string modelLabels[16] = {
	"Pair of classic waveforms",
	"Waveshaping oscillator",
	"Two operator FM",
	"Granular formant oscillator",
	"Harmonic oscillator",
	"Wavetable oscillator",
	"Chords",
	"Vowel and speech synthesis",
	"Granular cloud",
	"Filtered noise",
	"Particle noise",
	"Inharmonic string modeling",
	"Modal resonator",
	"Analog bass drum",
	"Analog snare drum",
	"Analog hi-hat",
};

struct MyKnob1 : app::SvgKnob {
	MyKnob1() {
		minAngle = -0.78 * M_PI;
		maxAngle = 0.78 * M_PI;
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/palette/palette_knobL.svg")));
	}
	void draw(const DrawArgs& args) override
    {
        app::SvgKnob::draw(args);
        if (this->paramQuantity==nullptr)
			return;
		auto modul = dynamic_cast<Plaits*>(this->paramQuantity->module);
		if (modul)
		{
			if (paramQuantity->paramId == Plaits::FREQ_PARAM)
				this->snap = !modul->freeTune;
			if (modul->showModulations==false)
				return;
			static const NVGcolor colors[4]=
			{
				nvgRGBA(0x00, 0xee, 0x00, 0xff),
				nvgRGBA(0xee, 0x00, 0x00, 0xff),
				nvgRGBA(0x00, 0x00, 0xee, 0xff),
				nvgRGBA(0x00, 0xee, 0xee, 0xff),
			};
			NVGRestorer rs(args.vg);
			for (int i=0;i<1;++i)
			{
				float modulated = modul->getModulatedParamNormalized(this->paramQuantity->paramId,i);
				if (modulated>=0.0f)
				{
					float angle = rescale(modulated,0.0f,1.0f,this->minAngle,this->maxAngle)-1.5708;
					float xpos = args.clipBox.pos.x;
					float ypos = args.clipBox.pos.y;
					float w = args.clipBox.size.x;
					float h = args.clipBox.size.y;
					float xcor0 = xpos + (w / 2.0f) + 10.0f * std::cos(angle);
					float ycor0 = ypos + (w / 2.0f) + 10.0f * std::sin(angle);
					float xcor1 = xpos + (w / 2.0f) + 15.0f * std::cos(angle);
					float ycor1 = ypos + (w / 2.0f) + 15.0f * std::sin(angle);
					nvgBeginPath(args.vg);
					nvgStrokeColor(args.vg,colors[i]);
					nvgMoveTo(args.vg,xcor0,ycor0);
					nvgLineTo(args.vg,xcor1,ycor1);
					nvgStroke(args.vg);
					nvgRotate(args.vg,0.0f);
				}
				
			}
		}
        
    }
};

struct MyKnob2 : app::SvgKnob {
	MyKnob2() {
		minAngle = -0.83 * M_PI;
		maxAngle = 0.83 * M_PI;
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/palette/palette_knobS.svg")));
	}
};

struct MyPort1 : app::SvgPort {
	MyPort1() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance,"res/palette/palette_jack.svg")));
	}
};

struct MyButton1 : app::SvgSwitch {
	MyButton1() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/palette/palette_push.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/palette/palette_pushed.svg")));
	}
};

struct Model_LEDWidget : public TransparentWidget
{
	Model_LEDWidget(Plaits* m)
	{
		mPlaits = m;
	}
	void draw(const DrawArgs& args) override
	{
		if (mPlaits==nullptr)
			return;
		NVGRestorer nr(args.vg);
		static const NVGcolor inactive = nvgRGBA(0x00,0x00,0x00,0xff);
		static const NVGcolor active = nvgRGBA(0x84,0x84,0x84,0xff);
		
		for (int i=0;i<8;++i)
		{
			nvgBeginPath(args.vg);
			int modelIndex = mPlaits->patch[0].engine % 8;
			if (i == modelIndex)
				nvgFillColor(args.vg,active);
			else nvgFillColor(args.vg,inactive);
			nvgEllipse(args.vg,positions[i][0],positions[i][1],3.5f,3.5f);
			nvgFill(args.vg);
		}
	}
	void onButton(const event::Button& e) override
	{
		int engineBank = mPlaits->patch[0].engine / 8;
		for (int i=0;i<8;++i)
		{
			Rect r{positions[i][0]-3.5f,positions[i][1]-3.5f,7.0f,7.0f};
			if (r.contains(e.pos))
			{
				for (int j=0;j<MAX_PLAITS_VOICES;++j)
					mPlaits->patch[j].engine = (engineBank*8)+i;
				break;
			}
		}
	}
	Plaits* mPlaits = nullptr;
	const float positions[8][2]=
		{
			{96.5,102.5},
			{103.5,	90.5},
			{113.5,	81.5},
			{127.5,	75.5},
			{142.5,	75.5},
			{156.5,	81.5},
			{166.5,	90.5},
			{173.5,	102.5}
		};
};

struct PlaitsWidget : ModuleWidget {
	void step() override
	{
		ModuleWidget::step();
		auto plaits = dynamic_cast<Plaits*>(module);
		if (plaits)
		{
			if (plaits->patch->engine!=curSubPanel)
			{
				curSubPanel = plaits->patch->engine;
				if (subPanels[curSubPanel]!=nullptr)
				{
					swgWidget->show();	
					swgWidget->setSvg(subPanels[curSubPanel]);
				}
				else 
				{
					swgWidget->hide();
				}
			}
		}
		
	}
	PlaitsWidget(Plaits *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/palette/paletteBG.svg")));
		subPanels.resize(16);
		char resName[128];
		for (int i=0;i<16;++i)
		{
			sprintf(resName,"res/palette/palette_%d.svg",i+1);
			subPanels[i] = APP->window->loadSvg(asset::plugin(pluginInstance, resName));	
		}
		swgWidget = new SvgWidget;
		swgWidget->setSvg(subPanels[0]);
		addChild(swgWidget);
		swgWidget->box.pos = {0,0};
		swgWidget->box.size = this->box.size;

		addParam(createParamCentered<MyKnob1>(Vec(71, 235.5), module, Plaits::FREQ_PARAM));
		addParam(createParamCentered<MyKnob1>(Vec(199,235.5), module, Plaits::SECONDARY_FREQ_PARAM));
		addParam(createParamCentered<MyKnob1>(Vec(135,198.5), module, Plaits::HARMONICS_PARAM));
		addParam(createParamCentered<MyKnob1>(Vec(83.5,145.5), module, Plaits::TIMBRE_PARAM));
		addParam(createParamCentered<MyKnob1>(Vec(186.5,145.5), module, Plaits::MORPH_PARAM));
		addParam(createParamCentered<MyKnob1>(Vec(135,283.5), module, Plaits::OUTMIX_PARAM));
		addParam(createParamCentered<MyKnob1>(Vec(135,283.5), module, Plaits::OUTMIX_PARAM));
		
		addOutput(createOutputCentered<MyPort1>(Vec(135, 351), module, Plaits::AUX2_OUTPUT));
		addOutput(createOutputCentered<MyPort1>(Vec(71,351), module, Plaits::OUT_OUTPUT));
		addOutput(createOutputCentered<MyPort1>(Vec(199.5,351), module, Plaits::AUX_OUTPUT));

		addInput(createInputCentered<MyPort1>(Vec(135, 44), module, Plaits::TRIGGER_INPUT));
		addInput(createInputCentered<MyPort1>(Vec(88, 44), module, Plaits::NOTE_INPUT));

		addInput(createInputCentered<MyPort1>(Vec(252,255), module, Plaits::FREQ_INPUT));
		addInput(createInputCentered<MyPort1>(Vec(183,44), module, Plaits::LEVEL_INPUT));

		addInput(createInputCentered<MyPort1>(Vec(18,255), module, Plaits::HARMONICS_INPUT));
		addParam(createParamCentered<MyKnob2>(Vec(18,233), module, Plaits::HARMONICS_CV_PARAM));
		addInput(createInputCentered<MyPort1>(Vec(252,148), module, Plaits::MORPH_INPUT));
		addParam(createParamCentered<MyKnob2>(Vec(252,126), module, Plaits::MORPH_CV_PARAM));
		addInput(createInputCentered<MyPort1>(Vec(18,148), module, Plaits::TIMBRE_INPUT));
		addParam(createParamCentered<MyKnob2>(Vec(18,126), module, Plaits::TIMBRE_CV_PARAM));

		addParam(createParamCentered<MyKnob2>(Vec(252,233), module, Plaits::FREQ_CV_PARAM));
		addParam(createParamCentered<MyKnob2>(Vec(71,300), module, Plaits::LPG_COLOR_PARAM));
		addParam(createParamCentered<MyKnob2>(Vec(199.5,300), module, Plaits::LPG_DECAY_PARAM));

		addParam(createParamCentered<MyKnob2>(Vec(40,44), module, Plaits::UNISONOMODE_PARAM));
		addParam(createParamCentered<MyKnob2>(Vec(230,44), module, Plaits::UNISONOSPREAD_PARAM));
		
		addParam(createParamCentered<MyKnob2>(Vec(18,206), module, Plaits::HARMONICS_LPG_PARAM));
		addParam(createParamCentered<MyKnob2>(Vec(18,175), module, Plaits::TIMBRE_LPG_PARAM));
		addParam(createParamCentered<MyKnob2>(Vec(252,175), module, Plaits::MORPH_LPG_PARAM));

		addParam(createParamCentered<MyKnob2>(Vec(252,333), module, Plaits::DECAY_CV_PARAM));
		addInput(createInputCentered<MyPort1>(Vec(252,355), module, Plaits::LPG_DECAY_INPUT));
		addParam(createParamCentered<MyKnob2>(Vec(18,333), module, Plaits::LPG_COLOR_CV_PARAM));
		addInput(createInputCentered<MyPort1>(Vec(18,355), module, Plaits::LPG_COLOR_INPUT));

		addParam(createParamCentered<MyButton1>(Vec(77.5, 98.5), module, Plaits::MODEL1_PARAM));
		addParam(createParamCentered<MyButton1>(Vec(192.5, 98.5), module, Plaits::MODEL2_PARAM));

		addParam(createParamCentered<MyKnob2>(Vec(18,76), module, Plaits::ENGINE_CV_PARAM));
		addInput(createInputCentered<MyPort1>(Vec(18,98), module, Plaits::ENGINE_INPUT));

		addParam(createParamCentered<MyKnob2>(Vec(252,283), module, Plaits::OUTMIX_CV_PARAM));
		addInput(createInputCentered<MyPort1>(Vec(252,305), module, Plaits::OUTMIX_INPUT));

		addParam(createParamCentered<MyKnob2>(Vec(18,283), module, Plaits::OUTMIX_LPG_PARAM));

		addParam(createParamCentered<MyKnob2>(Vec(252,206), module, Plaits::FREQ_LPG_PARAM));

		addParam(createParamCentered<MyKnob2>(Vec(252,76), module, Plaits::UNISONOSPREAD_CV_PARAM));
		addInput(createInputCentered<MyPort1>(Vec(252,98), module, Plaits::SPREAD_INPUT));

		
		Model_LEDWidget* ledwid = new Model_LEDWidget(module);
		ledwid->box.pos = {0,0};
		ledwid->box.size = box.size;
		addChild(ledwid);
		

		
	}

	void appendContextMenu(Menu *menu) override {
		Plaits *module = dynamic_cast<Plaits*>(this->module);

		struct PlaitsLowCpuItem : MenuItem {
			Plaits *module;
			void onAction(const event::Action &e) override {
				module->lowCpu ^= true;
			}
		};

		struct PlaitsShowModulationsItem : MenuItem {
			Plaits *module;
			void onAction(const event::Action &e) override {
				module->showModulations ^= true;
			}
		};
		
		struct PlaitsFreeTuneItem : MenuItem {
			Plaits *module;
			void onAction(const event::Action &e) override {
				module->freeTune ^= true;
			}
		};

		struct PlaitsModelItem : MenuItem {
			Plaits *module;
			int model;
			void onAction(const event::Action &e) override {
				for (int i=0;i<MAX_PLAITS_VOICES;++i)
					module->patch[i].engine = model;
			}
		};

		menu->addChild(new MenuEntry);
		PlaitsLowCpuItem *lowCpuItem = createMenuItem<PlaitsLowCpuItem>("Low CPU", CHECKMARK(module->lowCpu));
		lowCpuItem->module = module;
		menu->addChild(lowCpuItem);
		PlaitsFreeTuneItem *freeTuneItem = createMenuItem<PlaitsFreeTuneItem>("Octave knob free tune", CHECKMARK(module->freeTune));
		freeTuneItem->module = module;
		menu->addChild(freeTuneItem);

		PlaitsShowModulationsItem *showModsItem 
			= createMenuItem<PlaitsShowModulationsItem>("Show modulation amounts on knobs", CHECKMARK(module->showModulations));
		showModsItem->module = module;
		menu->addChild(showModsItem);

		menu->addChild(new MenuEntry());
		menu->addChild(createMenuLabel("Models"));
		for (int i = 0; i < 16; i++) {
			PlaitsModelItem *modelItem = createMenuItem<PlaitsModelItem>(modelLabels[i], CHECKMARK(module->patch[0].engine == i));
			modelItem->module = module;
			modelItem->model = i;
			menu->addChild(modelItem);
		}
	}
	
	std::vector<std::shared_ptr<SVG>> subPanels;
	int curSubPanel = 0;
	SvgWidget* swgWidget = nullptr;
};


Model *modelPlaits = createModel<Plaits, PlaitsWidget>("PolyPlaits");
