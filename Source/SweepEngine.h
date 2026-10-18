// SweepEngine: the beat-synced modulation engine and vocal effects.
// Plain C++17 with no JUCE dependency, so it can be unit-tested on its own.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sweep
{

constexpr int kMaxSteps = 16;
constexpr int kNumEffects = 6;
constexpr int kCurvePoints = 64;   // resolution of a drawn curve, across one beat
constexpr float kPi = 3.14159265358979f;

enum class Effect { DelayThrow = 0, ReverbThrow, FilterSweep, Stutter, PitchDip, Bitcrush, Count };
enum class Shape  { RampDown = 0, RampUp, Pulse, Swell, Count };

// Pattern length options shown in the UI, in beats (quarter notes).
inline int patternBeatsForIndex (int i) { static const int b[] = { 4, 6, 8, 16 }; return b[std::clamp (i, 0, 3)]; }
// Hit length options, as a fraction of one beat: 1/16, 1/8, 3/16, 1/4 note.
inline float hitLengthForIndex (int i) { static const float l[] = { 0.25f, 0.5f, 0.75f, 1.0f }; return l[std::clamp (i, 0, 3)]; }

// Delay time options, in beats: 1/16, 1/8, dotted 1/8, 1/4, dotted 1/4, 1/2.
inline float delayBeatsForIndex (int i) { static const float b[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f }; return b[std::clamp (i, 0, 5)]; }
// Stutter slice options, in beats: 1/32, 1/16, 1/8, 1/4.
inline float sliceBeatsForIndex (int i) { static const float b[] = { 0.125f, 0.25f, 0.5f, 1.0f }; return b[std::clamp (i, 0, 3)]; }

enum class FilterMode { Telephone = 0, LowPass, HighPass };

// Per-effect settings (the gear panels).
struct EffectSettings
{
    // delay throw
    int   delayTime = 2;          // index into delayBeatsForIndex
    float delayFeedback = 0.45f;  // 0..0.9
    float delayToneHz = 5500.0f;  // low-pass in the echoes
    bool  delayPingPong = true;
    // reverb throw
    float verbSize = 0.75f;       // 0..1
    float verbDamping = 0.3f;     // 0..1
    float verbWidth = 1.0f;       // 0..1
    // filter sweep
    FilterMode filterMode = FilterMode::Telephone;
    float filterResonance = 0.4f;  // 0..1
    float filterRange = 0.8f;      // 0..1, how far the sweep goes
    // stutter
    int   stutterSlice = 1;        // index into sliceBeatsForIndex
    float stutterSmooth = 0.3f;    // 0..1, edge fades
    // pitch dip
    float pitchSemitones = 12.0f;  // 1..24
    bool  pitchUp = false;
    float pitchGrainMs = 45.0f;    // 20..100
    // bitcrush
    float crushBits = 4.0f;        // bit depth at full modulation, 2..12
    float crushRate = 15.0f;       // sample-rate reduction factor at full modulation, 1..32
};

struct Params
{
    EffectSettings fx;
    float  smoothMs = 2.5f;        // modulation smoothing
    int    patternBeats = 4;
    std::array<bool, kMaxSteps> steps {};
    std::array<Effect, kMaxSteps> stepEffect {};   // which effect each beat fires
    Shape  shape = Shape::Swell;                   // preset shape for beats without a drawn curve
    float  depth = 0.8f;       // 0..1
    float  hitLength = 0.5f;   // in beats (preset shapes only)
    float  mix = 0.6f;         // 0..1
};

// Hand-drawn curves, one per beat, each spanning the whole beat.
// Written by the editor, read by the audio thread, so every value is atomic.
struct CurveBank
{
    std::array<std::array<std::atomic<float>, kCurvePoints>, kMaxSteps> values {};
    std::array<std::atomic<bool>, kMaxSteps> drawn {};

    CurveBank() { clear(); }
    void clear()
    {
        for (auto& beat : values) for (auto& v : beat) v.store (0.0f, std::memory_order_relaxed);
        for (auto& d : drawn) d.store (false, std::memory_order_relaxed);
    }
    bool isDrawn (int beat) const { return drawn[(size_t) beat].load (std::memory_order_relaxed); }
    float point (int beat, int i) const { return values[(size_t) beat][(size_t) i].load (std::memory_order_relaxed); }
    void setPoint (int beat, int i, float v) { values[(size_t) beat][(size_t) i].store (std::clamp (v, 0.0f, 1.0f), std::memory_order_relaxed); }
    // value at t in [0, 1) across the beat, linearly interpolated
    float at (int beat, double t) const
    {
        const double x = std::clamp (t, 0.0, 1.0) * (kCurvePoints - 1);
        const int i = std::min ((int) x, kCurvePoints - 2);
        const float f = (float) (x - i);
        const float a = point (beat, i), b = point (beat, i + 1);
        return a + f * (b - a);
    }
};

inline float shapeValue (Shape s, float u)
{
    u = std::clamp (u, 0.0f, 1.0f);
    switch (s)
    {
        case Shape::RampDown: return 1.0f - u;
        case Shape::RampUp:   return u;
        case Shape::Pulse:    return u < 0.85f ? 1.0f : 0.0f;
        case Shape::Swell:    return std::sin (kPi * u);
        default:              return 0.0f;
    }
}

// Preset curve (before depth) at t in [0, 1) across a beat.
inline float presetCurveAt (const Params& p, double t)
{
    if (t > p.hitLength)
        return 0.0f;
    return shapeValue (p.shape, (float) (t / p.hitLength));
}

// Modulation amount (0..1) at a position inside the pattern, in beats.
// Uses the beat's drawn curve when it has one, otherwise the preset shape.
inline float modulationAt (const Params& p, const CurveBank* curves, double posInPattern)
{
    const int beat = std::clamp ((int) std::floor (posInPattern), 0, p.patternBeats - 1);
    if (! p.steps[(size_t) beat])
        return 0.0f;
    const double t = posInPattern - beat;
    const float v = (curves != nullptr && curves->isDrawn (beat)) ? curves->at (beat, t) : presetCurveAt (p, t);
    return v * p.depth;
}

inline int beatAt (const Params& p, double posInPattern)
{
    return std::clamp ((int) std::floor (posInPattern), 0, p.patternBeats - 1);
}

inline double wrapPosition (double ppq, int patternBeats)
{
    double pos = std::fmod (ppq, (double) patternBeats);
    if (pos < 0) pos += patternBeats;
    return pos;
}

//==============================================================================
// Small DSP building blocks

struct OnePole
{
    float z = 0.0f, a = 0.0f;
    void setCutoff (float hz, float sr) { a = std::exp (-2.0f * kPi * hz / sr); }
    float lp (float x) { z = x + a * (z - x); return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0.0f; }
};

// Topology-preserving-transform state variable filter (Zavalishin).
struct SVF
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1.4f, a1 = 0, a2 = 0, a3 = 0;
    void set (float hz, float q, float sr)
    {
        hz = std::clamp (hz, 10.0f, sr * 0.45f);
        g = std::tan (kPi * hz / sr);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void process (float x, float& low, float& high)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        low = v2;
        high = x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0; }
};

struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int minSize)
    {
        int size = 1;
        while (size < minSize) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // delay in samples (>= 1), linear interpolation
    float read (float d) const
    {
        const float rp = (float) w - d;
        const int i0 = (int) std::floor (rp);
        const float f = rp - (float) i0;
        const float s0 = buf[(size_t) (i0 & mask)];
        const float s1 = buf[(size_t) ((i0 + 1) & mask)];
        return s0 + f * (s1 - s0);
    }
    float at (int absoluteIndex) const { return buf[(size_t) (absoluteIndex & mask)]; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
};

// Freeverb-style reverb (Jezar's public-domain design), stereo.
class Reverb
{
public:
    void prepare (float sr)
    {
        const float scale = sr / 44100.0f;
        static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apT[4]   = { 556, 441, 341, 225 };
        for (int ch = 0; ch < 2; ++ch)
        {
            const int spread = ch == 0 ? 0 : 23;
            for (int i = 0; i < 8; ++i) combs[ch][i].prepare ((int) ((combT[i] + spread) * scale));
            for (int i = 0; i < 4; ++i) aps[ch][i].prepare ((int) ((apT[i] + spread) * scale));
        }
    }
    void reset()
    {
        for (auto& c : combs) for (auto& x : c) x.reset();
        for (auto& a : aps)   for (auto& x : a) x.reset();
    }
    void process (float in, float& outL, float& outR)
    {
        float o[2] = { 0, 0 };
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& c : combs[ch]) o[ch] += c.process (in);
            for (auto& a : aps[ch])   o[ch] = a.process (o[ch]);
        }
        outL = o[0];
        outR = o[1];
    }
    // size 0..1 -> decay, damping 0..1 -> darker tail
    void set (float size, float damping)
    {
        // defaults (size 0.75, damping 0.3) give the 0.2.0 sound: feedback 0.84, damp 0.3
        const float fb = 0.56f + 0.3733f * std::clamp (size, 0.0f, 1.0f);
        const float d = 0.03f + 0.9f * std::clamp (damping, 0.0f, 1.0f);
        for (auto& ch : combs) for (auto& c : ch) { c.feedback = fb; c.damp = d; }
    }

private:
    struct Comb
    {
        std::vector<float> b; int i = 0; float store = 0;
        float feedback = 0.84f, damp = 0.3f;
        void prepare (int n) { b.assign ((size_t) std::max (n, 1), 0.0f); i = 0; store = 0; }
        void reset() { std::fill (b.begin(), b.end(), 0.0f); store = 0; }
        float process (float x)
        {
            const float out = b[(size_t) i];
            store = out * (1 - damp) + store * damp;
            b[(size_t) i] = x + store * feedback;
            if (++i >= (int) b.size()) i = 0;
            return out;
        }
    };
    struct Allpass
    {
        std::vector<float> b; int i = 0;
        void prepare (int n) { b.assign ((size_t) std::max (n, 1), 0.0f); i = 0; }
        void reset() { std::fill (b.begin(), b.end(), 0.0f); }
        float process (float x)
        {
            const float bo = b[(size_t) i];
            b[(size_t) i] = x + bo * 0.5f;
            if (++i >= (int) b.size()) i = 0;
            return bo - x;
        }
    };
    std::array<std::array<Comb, 8>, 2> combs;
    std::array<std::array<Allpass, 4>, 2> aps;
};

//==============================================================================
class Engine
{
public:
    // Values the editor reads for its playhead.
    std::atomic<double> uiPosition { 0.0 };   // position inside the pattern, in beats
    std::atomic<double> uiSongPpq  { 0.0 };   // host song position, in beats
    std::atomic<bool>   uiPlaying  { false };
    std::atomic<double> uiBpm      { 120.0 };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const int fourSeconds = (int) (sr * 4.0f) + 8;
        delayL.prepare (fourSeconds);
        delayR.prepare (fourSeconds);
        stutterL.prepare (fourSeconds);
        stutterR.prepare (fourSeconds);
        pitchL.prepare ((int) (sr * 0.2f));
        pitchR.prepare ((int) (sr * 0.2f));
        reverb.prepare (sr);
        modSmoothCoef = std::exp (-1.0f / (0.0025f * sr));   // ~2.5 ms
        paramSmoothCoef = std::exp (-1.0f / (0.03f * sr));   // ~30 ms
        dlyFbLpL.setCutoff (5500.0f, sr); dlyFbLpR.setCutoff (5500.0f, sr);
        dlyFbHpL.setCutoff (280.0f, sr);  dlyFbHpR.setCutoff (280.0f, sr);
        verbInHp.setCutoff (220.0f, sr);
        reset();
    }

    void reset()
    {
        delayL.reset(); delayR.reset(); stutterL.reset(); stutterR.reset();
        pitchL.reset(); pitchR.reset(); reverb.reset();
        svfHpL.reset(); svfHpR.reset(); svfLpL.reset(); svfLpR.reset();
        dlyFbLpL.reset(); dlyFbLpR.reset(); dlyFbHpL.reset(); dlyFbHpR.reset(); verbInHp.reset();
        mods.fill (0.0f);
        lastBeat = -1; stutterPhase = 1 << 30; stutterStart = 0;
        pitchPhase = 0; crushHoldL = crushHoldR = 0; crushCounter = 0;
        delaySamplesSmoothed = -1; mixSmoothed = -1;
        lastDelayTone = -1; lastSmoothMs = -1;
    }

    // Processes one block in place. ppqStart is the host position (quarter notes) at the
    // first sample; when the host isn't playing the modulation fades out and tails ring.
    void process (float* left, float* right, int numSamples, const Params& p, const CurveBank* curves,
                  bool playing, double ppqStart, double bpm)
    {
        bpm = std::clamp (bpm, 20.0, 400.0);
        const double beatsPerSample = bpm / 60.0 / sr;
        const float samplesPerBeat = (float) (60.0 / bpm * sr);

        const auto& fxs = p.fx;

        // Block-rate settings.
        const float targetDelay = std::clamp (delayBeatsForIndex (fxs.delayTime) * samplesPerBeat, 16.0f, (float) delayL.mask - 4.0f);
        if (delaySamplesSmoothed < 0) delaySamplesSmoothed = targetDelay;
        if (mixSmoothed < 0) mixSmoothed = p.mix;
        if (fxs.delayToneHz != lastDelayTone)
        {
            lastDelayTone = fxs.delayToneHz;
            dlyFbLpL.setCutoff (fxs.delayToneHz, sr);
            dlyFbLpR.setCutoff (fxs.delayToneHz, sr);
        }
        const float feedback = std::clamp (fxs.delayFeedback, 0.0f, 0.9f);
        reverb.set (fxs.verbSize, fxs.verbDamping);
        if (p.smoothMs != lastSmoothMs)
        {
            lastSmoothMs = p.smoothMs;
            modSmoothCoef = std::exp (-1.0f / (std::max (0.5f, p.smoothMs) * 0.001f * sr));
        }

        const int sliceLen = std::max (64, (int) (sliceBeatsForIndex (fxs.stutterSlice) * samplesPerBeat));
        const int stutterFade = std::max (16, (int) (fxs.stutterSmooth * 0.25f * (float) sliceLen));
        const float pitchWindow = std::clamp (fxs.pitchGrainMs, 20.0f, 100.0f) * 0.001f * sr;
        const float pitchSign = fxs.pitchUp ? 1.0f : -1.0f;
        const float pitchOctaves = std::clamp (fxs.pitchSemitones, 1.0f, 24.0f) / 12.0f;
        const float crushMinBits = std::clamp (fxs.crushBits, 2.0f, 12.0f);
        const float crushMaxHold = std::clamp (fxs.crushRate, 1.0f, 32.0f);
        const float verbWidth = std::clamp (fxs.verbWidth, 0.0f, 1.0f);
        constexpr float kActive = 1.0e-5f;

        double ppq = ppqStart;
        for (int n = 0; n < numSamples; ++n)
        {
            // ---- modulation: the current beat drives its own effect; every effect has its own
            // smoothed amount so handing over between beats with different effects is click-free.
            std::array<float, kNumEffects> target {};
            if (playing)
            {
                const double pos = wrapPosition (ppq, p.patternBeats);
                const int beat = beatAt (p, pos);
                const auto fx = p.stepEffect[(size_t) beat];
                if (beat != lastBeat)
                {
                    lastBeat = beat;
                    if (p.steps[(size_t) beat] && fx == Effect::Stutter)
                    {
                        stutterStart = stutterL.w;
                        stutterPhase = 0;
                    }
                }
                target[(size_t) fx] = modulationAt (p, curves, pos);
                if (n == 0)
                {
                    uiPosition.store (pos, std::memory_order_relaxed);
                    uiSongPpq.store (ppq, std::memory_order_relaxed);
                }
                ppq += beatsPerSample;
            }
            else
            {
                lastBeat = -1;
            }
            for (size_t e = 0; e < (size_t) kNumEffects; ++e)
            {
                mods[e] = target[e] + modSmoothCoef * (mods[e] - target[e]);
                if (mods[e] < 1.0e-7f) mods[e] = 0.0f;
            }
            mixSmoothed = p.mix + paramSmoothCoef * (mixSmoothed - p.mix);
            delaySamplesSmoothed = targetDelay + paramSmoothCoef * (delaySamplesSmoothed - targetDelay);

            const float dryL = left[n], dryR = right[n];
            const float mono = 0.5f * (dryL + dryR);
            float sendL = 0.0f, sendR = 0.0f;   // delay + reverb, added on top of the dry vocal

            // ---- delay throw (always running so tails keep ringing)
            {
                const float in = mono * mods[(size_t) Effect::DelayThrow];
                const float dl = delayL.read (delaySamplesSmoothed);
                const float dr = delayR.read (delaySamplesSmoothed);
                if (fxs.delayPingPong)
                {
                    // echoes bounce left -> right -> left
                    const float fbL = dlyFbHpL.hp (dlyFbLpL.lp (dr)) * feedback;
                    const float fbR = dlyFbHpR.hp (dlyFbLpR.lp (dl)) * feedback;
                    delayL.push (in + fbL);
                    delayR.push (fbR);
                }
                else
                {
                    // centred echoes
                    const float fbL = dlyFbHpL.hp (dlyFbLpL.lp (dl)) * feedback;
                    const float fbR = dlyFbHpR.hp (dlyFbLpR.lp (dr)) * feedback;
                    delayL.push (in + fbL);
                    delayR.push (in + fbR);
                }
                sendL += dl;
                sendR += dr;
            }

            // ---- reverb throw (always running so tails keep ringing)
            {
                const float in = verbInHp.hp (mono * mods[(size_t) Effect::ReverbThrow]);
                float rl, rr;
                reverb.process (in * 0.06f, rl, rr);
                const float mid = 0.5f * (rl + rr), side = 0.5f * (rl - rr) * verbWidth;
                sendL += mid + side;
                sendR += mid - side;
            }

            // ---- insert effects, in series; each is transparent when its amount is 0
            float wetL = dryL, wetR = dryR;

            if (const float m = mods[(size_t) Effect::FilterSweep]; m > kActive)
            {
                // Full range at 0. At full sweep (range 0.8 defaults):
                //   telephone: 20 Hz-20 kHz -> ~700 Hz-2.4 kHz band
                //   low-pass:  20 kHz -> ~600 Hz      high-pass: 20 Hz -> ~1.5 kHz
                const float r = std::clamp (fxs.filterRange, 0.05f, 1.0f) * 1.25f;
                float hpHz = 20.0f, lpHz = 20000.0f;
                switch (fxs.filterMode)
                {
                    case FilterMode::Telephone: hpHz = 20.0f * std::pow (35.0f, m * r); lpHz = 20000.0f * std::pow (0.12f, m * r); break;
                    case FilterMode::LowPass:   lpHz = 20000.0f * std::pow (0.03f, m * r); break;
                    case FilterMode::HighPass:  hpHz = 20.0f * std::pow (75.0f, m * r); break;
                }
                const float q = 0.707f + 4.0f * std::clamp (fxs.filterResonance, 0.0f, 1.0f) * m;
                svfHpL.set (hpHz, q, sr); svfHpR.set (hpHz, q, sr);
                svfLpL.set (lpHz, q, sr); svfLpR.set (lpHz, q, sr);
                float lo, hi, fl, fr;
                svfHpL.process (wetL, lo, hi); svfLpL.process (hi, fl, hi);
                svfHpR.process (wetR, lo, hi); svfLpR.process (hi, fr, hi);
                const float makeup = fxs.filterMode == FilterMode::Telephone ? 1.0f + 0.6f * m : 1.0f;
                const float blend = std::min (1.0f, m * 4.0f);
                wetL += (fl * makeup - wetL) * blend;
                wetR += (fr * makeup - wetR) * blend;
            }
            else
            {
                svfHpL.reset(); svfHpR.reset(); svfLpL.reset(); svfLpR.reset();
            }

            // pitch dip (buffer always fed so it can start instantly)
            pitchL.push (wetL);
            pitchR.push (wetR);
            if (const float m = mods[(size_t) Effect::PitchDip]; m > kActive)
            {
                const float ratio = std::pow (2.0f, pitchSign * pitchOctaves * m);
                pitchPhase += (1.0f - ratio) / pitchWindow;
                pitchPhase -= std::floor (pitchPhase);
                const float p1 = pitchPhase;
                const float p2 = p1 + 0.5f - std::floor (p1 + 0.5f);
                const float d1 = 2.0f + p1 * pitchWindow, d2 = 2.0f + p2 * pitchWindow;
                const float g1 = 0.5f - 0.5f * std::cos (2.0f * kPi * p1);
                const float g2 = 0.5f - 0.5f * std::cos (2.0f * kPi * p2);
                const float shL = pitchL.read (d1) * g1 + pitchL.read (d2) * g2;
                const float shR = pitchR.read (d1) * g1 + pitchR.read (d2) * g2;
                const float blend = std::min (1.0f, m * 4.0f);
                wetL += (shL - wetL) * blend;
                wetR += (shR - wetR) * blend;
            }

            // stutter (buffer always fed; the slice starts at the beat that triggered it)
            stutterL.push (wetL);
            stutterR.push (wetR);
            if (stutterPhase < (1 << 29)) ++stutterPhase;
            if (const float m = mods[(size_t) Effect::Stutter]; m > kActive)
            {
                const int k = (stutterPhase - 1) % sliceLen;
                const int idx = stutterStart + k;
                const int fade = stutterFade;
                const float env = std::min ({ 1.0f, (k + 1) / (float) fade, (sliceLen - k) / (float) fade });
                const float g = stutterPhase <= sliceLen ? 1.0f : env;   // first pass is live audio
                wetL += (stutterL.at (idx) * g - wetL) * m;
                wetR += (stutterR.at (idx) * g - wetR) * m;
            }

            if (const float m = mods[(size_t) Effect::Bitcrush]; m > kActive)
            {
                const int hold = 1 + (int) (m * (crushMaxHold - 1.0f));
                if (++crushCounter >= hold) { crushCounter = 0; crushHoldL = wetL; crushHoldR = wetR; }
                const float bits = 16.0f - (16.0f - crushMinBits) * m;
                const float levels = std::pow (2.0f, bits - 1.0f);
                const float cl = std::round (crushHoldL * levels) / levels;
                const float cr = std::round (crushHoldR * levels) / levels;
                const float blend = std::min (1.0f, m * 3.0f);
                wetL += (cl - wetL) * blend;
                wetR += (cr - wetR) * blend;
            }
            else
            {
                crushCounter = 0;
            }

            left[n]  = dryL + (wetL - dryL) * mixSmoothed + sendL * mixSmoothed;
            right[n] = dryR + (wetR - dryR) * mixSmoothed + sendR * mixSmoothed;
        }

        uiPlaying.store (playing, std::memory_order_relaxed);
        uiBpm.store (bpm, std::memory_order_relaxed);
    }

private:
    float sr = 44100.0f;
    float modSmoothCoef = 0.0f, paramSmoothCoef = 0.0f;
    std::array<float, kNumEffects> mods {};
    float mixSmoothed = -1.0f, delaySamplesSmoothed = -1.0f;
    float lastDelayTone = -1.0f, lastSmoothMs = -1.0f;
    int lastBeat = -1;

    DelayLine delayL, delayR;
    OnePole dlyFbLpL, dlyFbLpR, dlyFbHpL, dlyFbHpR, verbInHp;
    Reverb reverb;
    SVF svfHpL, svfHpR, svfLpL, svfLpR;
    DelayLine stutterL, stutterR;
    int stutterStart = 0, stutterPhase = 1 << 30;
    DelayLine pitchL, pitchR;
    float pitchPhase = 0.0f;
    float crushHoldL = 0, crushHoldR = 0;
    int crushCounter = 0;
};

} // namespace sweep
