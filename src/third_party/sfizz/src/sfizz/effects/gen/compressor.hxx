#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
/* ------------------------------------------------------------
name: "compressor"
Code generated with Faust 2.30.5 (https://faust.grame.fr)
Compilation options: -lang cpp -inpl -es 1 -scal -ftz 0
------------------------------------------------------------ */

// Modified by Audanika for aud_dsp_sampler (ticket 28 of aud_audio_pm):
// compute() calls log10 only for a level above fSkipBelow, a linear level
// 1e-4 below the threshold, and pow only for a gain other than 0 dB, the
// two calls per sample that took most of the compressor's time on an app's
// audio thread. At or below fSkipBelow the level in dB lies at least
// 8.7e-4 dB below the threshold, more than the rounding of log10 can
// bridge, and the original term max(20 log10(level) - threshold, 0) is
// exactly 0; a gain of 0 dB is 1, as pow(10, 0) is. The state of the loop
// lives in locals, which the compiler keeps in registers instead of storing
// and loading the members on every sample, and computeTwo() runs the
// compressors of both channels in one loop. The output is the original's,
// bit for bit: every sample takes the same operations in the same order.
// isAtRest() tells whether silence passes as silence and leaves the state
// as it is.

#ifndef  __faustCompressor_H__
#define  __faustCompressor_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <limits>
#include <math.h>


#ifndef FAUSTCLASS 
#define FAUSTCLASS faustCompressor
#endif

#ifdef __APPLE__ 
#define exp10f __exp10f
#define exp10 __exp10
#endif


//[Before:class]
class faustCompressor {
	//[Begin:class]

	
 private:
	
	FAUSTFLOAT fHslider0;
	int fSampleRate;
	float fConst0;
	FAUSTFLOAT fHslider1;
	FAUSTFLOAT fHslider2;
	float fRec2[2];
	float fRec1[2];
	FAUSTFLOAT fHslider3;
	float fRec0[2];
	float fSkipBelow = -1.0f;
	
	// The linear level at or below which the term of the threshold is 0:
	// 1e-4 below the threshold, or -1, never, if the threshold is not finite.
	void updateSkipBelow() {
		const float threshold = float(fHslider3);
		if (!std::isfinite(threshold)) {
			fSkipBelow = -1.0f;
			return;
		}
		const double level = std::pow(10.0, double(threshold) / 20.0) * (1.0 - 1e-4);
		fSkipBelow = float(std::min(level, double(std::numeric_limits<float>::max())));
	}
	
 public:
	

	static constexpr int getNumInputs() {
		return 1;
	}
	static constexpr int getNumOutputs() {
		return 1;
	}
	
	static void classInit(int sample_rate) {
		//[Begin:classInit]
		//[End:classInit]
	}
	
	void instanceConstants(int sample_rate) {
		//[Begin:instanceConstants]
		fSampleRate = sample_rate;
		fConst0 = (1.0f / float(fSampleRate));
		//[End:instanceConstants]
	}
	
	void instanceResetUserInterface() {
		//[Begin:instanceResetUserInterface]
		fHslider0 = FAUSTFLOAT(0.0f);
		fHslider1 = FAUSTFLOAT(1.0f);
		fHslider2 = FAUSTFLOAT(0.0f);
		fHslider3 = FAUSTFLOAT(0.0f);
		updateSkipBelow();
		//[End:instanceResetUserInterface]
	}
	
	void instanceClear() {
		//[Begin:instanceClear]
		for (int l0 = 0; (l0 < 2); l0 = (l0 + 1)) {
			fRec2[l0] = 0.0f;
		}
		for (int l1 = 0; (l1 < 2); l1 = (l1 + 1)) {
			fRec1[l1] = 0.0f;
		}
		for (int l2 = 0; (l2 < 2); l2 = (l2 + 1)) {
			fRec0[l2] = 0.0f;
		}
		//[End:instanceClear]
	}
	
	void init(int sample_rate) {
		//[Begin:init]
		classInit(sample_rate);
		instanceInit(sample_rate);
		//[End:init]
	}
	void instanceInit(int sample_rate) {
		//[Begin:instanceInit]
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
		//[End:instanceInit]
	}
	
	
	int getSampleRate() {
		return fSampleRate;
	}
	
	
	// The coefficients compute() derives from the controls, and the state
	// of its loop, which stays in registers there (Audanika, ticket 28).
	struct Slow {
		float fSlow3, fSlow4, fSlow6, fSlow9, fSlow10, fSlow11, fSkipBelow;
	};
	struct State {
		float fRec2, fRec1, fRec0;
	};
	
	Slow slow() const {
		float fSlow0 = float(fHslider0);
		float fSlow1 = (0.5f * fSlow0);
		int iSlow2 = (std::fabs(fSlow1) < 1.1920929e-07f);
		float fSlow3 = (iSlow2 ? 0.0f : std::exp((0.0f - (fConst0 / (iSlow2 ? 1.0f : fSlow1)))));
		float fSlow4 = ((1.0f / std::max<float>(1.1920929e-07f, float(fHslider1))) + -1.0f);
		int iSlow5 = (std::fabs(fSlow0) < 1.1920929e-07f);
		float fSlow6 = (iSlow5 ? 0.0f : std::exp((0.0f - (fConst0 / (iSlow5 ? 1.0f : fSlow0)))));
		float fSlow7 = float(fHslider2);
		int iSlow8 = (std::fabs(fSlow7) < 1.1920929e-07f);
		float fSlow9 = (iSlow8 ? 0.0f : std::exp((0.0f - (fConst0 / (iSlow8 ? 1.0f : fSlow7)))));
		float fSlow10 = float(fHslider3);
		float fSlow11 = (1.0f - fSlow3);
		return { fSlow3, fSlow4, fSlow6, fSlow9, fSlow10, fSlow11, fSkipBelow };
	}
	
	State state() const {
		return { fRec2[1], fRec1[1], fRec0[1] };
	}
	
	void setState(const State& state) {
		fRec2[0] = fRec2[1] = state.fRec2;
		fRec1[0] = fRec1[1] = state.fRec1;
		fRec0[0] = fRec0[1] = state.fRec0;
	}
	
	// One sample of the loop of compute(): its gain.
	static FAUSTFLOAT tick(const Slow& k, State& s, FAUSTFLOAT input) {
		float fTemp0 = float(input);
		float fTemp1 = std::fabs(fTemp0);
		float fTemp2 = ((s.fRec1 > fTemp1) ? k.fSlow9 : k.fSlow6);
		s.fRec2 = ((s.fRec2 * fTemp2) + (fTemp1 * (1.0f - fTemp2)));
		s.fRec1 = s.fRec2;
		float fTemp3 = ((s.fRec1 <= k.fSkipBelow) ? 0.0f : std::max<float>(((20.0f * std::log10(s.fRec1)) - k.fSlow10), 0.0f));
		s.fRec0 = ((k.fSlow3 * s.fRec0) + (k.fSlow4 * (fTemp3 * k.fSlow11)));
		return ((s.fRec0 == 0.0f) ? FAUSTFLOAT(1.0f) : FAUSTFLOAT(std::pow(10.0f, (0.0500000007f * s.fRec0))));
	}
	
	void compute(int count, FAUSTFLOAT const* const* inputs, FAUSTFLOAT* const* outputs) {
		//[Begin:compute]
		FAUSTFLOAT const* input0 = inputs[0];
		FAUSTFLOAT* output0 = outputs[0];
		const Slow k = slow();
		State s = state();
		for (int i = 0; (i < count); i = (i + 1)) {
			output0[i] = tick(k, s, input0[i]);
		}
		if (count > 0) {
			setState(s);
		}
		//[End:compute]
	}
	
	// compute() of two compressors on their own inputs in one loop, so that
	// the chains of their detectors overlap (Audanika, ticket 28).
	static void computeTwo(faustCompressor& a, faustCompressor& b, int count,
		FAUSTFLOAT const* inputA, FAUSTFLOAT const* inputB, FAUSTFLOAT* outputA, FAUSTFLOAT* outputB) {
		const Slow ka = a.slow();
		const Slow kb = b.slow();
		State sa = a.state();
		State sb = b.state();
		for (int i = 0; (i < count); i = (i + 1)) {
			outputA[i] = tick(ka, sa, inputA[i]);
			outputB[i] = tick(kb, sb, inputB[i]);
		}
		if (count > 0) {
			a.setState(sa);
			b.setState(sb);
		}
	}


    FAUSTFLOAT getRatio() const { return fHslider1; }
    void setRatio(FAUSTFLOAT value) { fHslider1 = value; }

    FAUSTFLOAT getThreshold() const { return fHslider3; }
    void setThreshold(FAUSTFLOAT value) { fHslider3 = value; updateSkipBelow(); }

    FAUSTFLOAT getAttack() const { return fHslider0; }
    void setAttack(FAUSTFLOAT value) { fHslider0 = value; }

    FAUSTFLOAT getRelease() const { return fHslider2; }
    void setRelease(FAUSTFLOAT value) { fHslider2 = value; }

    // Whether a silent input gives the gain 1 and leaves the state silent:
    // the level and the gain are 0, the attack is not negative and the
    // threshold is above -inf, which keeps every coefficient finite.
    bool isAtRest() const {
        return fRec2[0] == 0.0f && fRec2[1] == 0.0f && fRec1[0] == 0.0f && fRec1[1] == 0.0f &&
            fRec0[0] == 0.0f && fRec0[1] == 0.0f && float(fHslider0) >= 0.0f &&
            float(fHslider3) > -std::numeric_limits<float>::infinity();
    }

	//[End:class]
};
//[After:class]


#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#undef FAUSTFLOAT
#undef FAUSTCLASS
