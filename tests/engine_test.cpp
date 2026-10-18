// Headless test for SweepEngine: fake host transport, per-beat effects, drawn curves.
#include "../Source/SweepEngine.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <functional>

using namespace sweep;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); ++failures; } } while (0)

struct Result { float peak = 0, maxJump = 0; bool finite = true; double energyHit = 0, energyQuiet = 0; };

static Params allBeats (Effect fx)
{
    Params p;
    p.stepEffect.fill (fx);
    return p;
}

// Runs `seconds` of a 220 Hz tone plus a little noise through the engine.
static Result run (const Params& p, const CurveBank* curves, double sr, int block, double bpm, double seconds,
                   bool playing = true)
{
    Engine e;
    e.prepare (sr);
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> noise (-0.05f, 0.05f);
    const int total = (int) (seconds * sr);
    std::vector<float> L ((size_t) block), R ((size_t) block), in ((size_t) block);
    Result r;
    double ppq = 0;
    float prev = 0;
    long t = 0;
    for (int done = 0; done < total; done += block)
    {
        const int n = std::min (block, total - done);
        for (int i = 0; i < n; ++i)
        {
            const float s = 0.5f * std::sin (2.0f * kPi * 220.0f * (float) (t + i) / (float) sr) + noise (rng);
            L[(size_t) i] = s; R[(size_t) i] = s; in[(size_t) i] = s;
        }
        e.process (L.data(), R.data(), n, p, curves, playing, ppq, bpm);
        for (int i = 0; i < n; ++i)
        {
            const float o = L[(size_t) i];
            if (! std::isfinite (o) || ! std::isfinite (R[(size_t) i])) r.finite = false;
            r.peak = std::max (r.peak, std::abs (o));
            r.maxJump = std::max (r.maxJump, std::abs (o - prev));
            prev = o;
            const double pos = wrapPosition (ppq + i * bpm / 60.0 / sr, p.patternBeats);
            const double diff = (double) o - (double) in[(size_t) i];
            if (playing && modulationAt (p, curves, pos) > 0.3f) r.energyHit += diff * diff; else r.energyQuiet += diff * diff;
        }
        if (playing) ppq += n * bpm / 60.0 / sr;
        t += n;
    }
    return r;
}

int main()
{
    const char* fxNames[] = { "delay", "reverb", "filter", "stutter", "pitch", "crush" };

    // 1. Timing for each pattern length (preset shapes).
    {
        Params p; p.patternBeats = 4; p.steps.fill (false); p.steps[1] = p.steps[3] = true;
        p.shape = Shape::Pulse; p.depth = 1; p.hitLength = 0.5f;
        CHECK (modulationAt (p, nullptr, 0.2) == 0.0f, "beat 1 should be silent");
        CHECK (modulationAt (p, nullptr, 1.1) == 1.0f, "beat 2 should fire");
        CHECK (modulationAt (p, nullptr, 1.7) == 0.0f, "beat 2 hit should end after 1/8");
        CHECK (modulationAt (p, nullptr, 3.0) == 1.0f, "beat 4 should fire");

        p.patternBeats = 6; p.steps.fill (false); p.steps[0] = true;
        CHECK (modulationAt (p, nullptr, wrapPosition (6.1, 6)) == 1.0f, "6-beat restart at ppq 6");
        CHECK (modulationAt (p, nullptr, wrapPosition (4.1, 6)) == 0.0f, "6-beat: bar 2 beat 1 silent");
        CHECK (modulationAt (p, nullptr, wrapPosition (12.1, 6)) == 1.0f, "6-beat realigns at ppq 12");

        p.patternBeats = 16; p.steps.fill (false); p.steps[15] = true;
        CHECK (modulationAt (p, nullptr, wrapPosition (15.1, 16)) == 1.0f, "4-bar: beat 16 fires");
        CHECK (modulationAt (p, nullptr, wrapPosition (31.1, 16)) == 1.0f, "4-bar: second phrase fires");
        std::printf ("timing: ok\n");
    }

    // 2. Drawn curves replace the preset for that beat only, across the whole beat.
    {
        CurveBank bank;
        Params p; p.patternBeats = 4; p.steps.fill (false); p.steps[1] = p.steps[2] = true;
        p.shape = Shape::Pulse; p.depth = 0.5f; p.hitLength = 0.25f;
        for (int i = 0; i < kCurvePoints; ++i)          // beat 2: a ramp up across the full beat
            bank.setPoint (1, i, (float) i / (kCurvePoints - 1));
        bank.drawn[1] = true;
        CHECK (std::abs (modulationAt (p, &bank, 1.0)) < 1e-6f, "drawn ramp starts at 0");
        CHECK (std::abs (modulationAt (p, &bank, 1.5) - 0.25f) < 0.01f, "drawn ramp mid (x depth 0.5): %f",
               modulationAt (p, &bank, 1.5));
        CHECK (modulationAt (p, &bank, 1.9) > 0.4f, "drawn curve ignores hit length");
        CHECK (modulationAt (p, &bank, 2.1) == 0.5f, "beat 3 still uses the preset pulse");
        CHECK (modulationAt (p, &bank, 2.4) == 0.0f, "beat 3 preset ends after 1/16");
        bank.drawn[1] = false;
        CHECK (modulationAt (p, &bank, 1.1) == 0.5f, "un-drawn beat goes back to preset");
        std::printf ("drawn curves: ok\n");
    }

    // 3. Every effect x shape x sample rate x block size: finite, bounded.
    for (int fx = 0; fx < (int) Effect::Count; ++fx)
    {
        for (int sh = 0; sh < (int) Shape::Count; ++sh)
            for (double sr : { 44100.0, 48000.0, 96000.0 })
                for (int block : { 64, 512, 1023 })
                {
                    Params p = allBeats ((Effect) fx); p.shape = (Shape) sh; p.patternBeats = 6;
                    p.steps.fill (false); p.steps[1] = p.steps[3] = p.steps[5] = true;
                    p.depth = 1.0f; p.mix = 1.0f; p.hitLength = 0.75f;
                    const Result r = run (p, nullptr, sr, block, 128.0, 4.0);
                    CHECK (r.finite, "%s shape %d sr %.0f block %d: non-finite output", fxNames[fx], sh, sr, block);
                    CHECK (r.peak < 2.5f, "%s shape %d sr %.0f: peak %.2f too high", fxNames[fx], sh, sr, r.peak);
                }
        Params p = allBeats ((Effect) fx); p.shape = Shape::Pulse; p.patternBeats = 4;
        p.steps.fill (false); p.steps[1] = p.steps[3] = true; p.depth = 1; p.mix = 1; p.hitLength = 0.5f;
        const Result r = run (p, nullptr, 48000, 256, 120, 6.0);
        std::printf ("%-8s peak %.2f  maxJump %.3f  change in hits %8.1f  between hits %8.1f\n",
                     fxNames[fx], r.peak, r.maxJump, r.energyHit, r.energyQuiet);
        CHECK (r.energyHit > 1.0, "%s: no audible effect during hits", fxNames[fx]);
        if (fx >= 2)
            CHECK (r.energyQuiet < r.energyHit * 0.5, "%s: too much change between hits", fxNames[fx]);
    }

    // 4. A different effect on every beat, back to back (full-length pulses, so effects hand over
    //    directly). Must stay finite, bounded and free of big clicks.
    {
        Params p; p.patternBeats = 6; p.steps.fill (true);
        for (int i = 0; i < kMaxSteps; ++i) p.stepEffect[(size_t) i] = (Effect) (i % kNumEffects);
        p.shape = Shape::Pulse; p.hitLength = 1.0f; p.depth = 1; p.mix = 1;
        for (int block : { 32, 480, 1024 })
        {
            const Result r = run (p, nullptr, 48000, block, 140, 8.0);
            CHECK (r.finite && r.peak < 2.5f, "mixed effects block %d: finite %d peak %.2f", block, r.finite, r.peak);
            CHECK (r.maxJump < 0.6f, "mixed effects block %d: click, max jump %.3f", block, r.maxJump);
        }
        const Result r = run (p, nullptr, 48000, 512, 140, 8.0);
        std::printf ("mixed    peak %.2f  maxJump %.3f (one effect per beat, back to back)\n", r.peak, r.maxJump);
    }

    // 5. Mixed effects with drawn curves, including jagged hand-drawn ones.
    {
        CurveBank bank;
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        for (int b = 0; b < kMaxSteps; ++b)
        {
            for (int i = 0; i < kCurvePoints; ++i) bank.setPoint (b, i, u (rng));
            bank.drawn[(size_t) b] = (b % 2) == 0;
        }
        Params p; p.patternBeats = 16; p.steps.fill (true);
        for (int i = 0; i < kMaxSteps; ++i) p.stepEffect[(size_t) i] = (Effect) ((i * 5) % kNumEffects);
        p.depth = 1; p.mix = 1;
        const Result r = run (p, &bank, 44100, 256, 95, 12.0);
        CHECK (r.finite && r.peak < 2.5f, "random drawn curves: finite %d peak %.2f", r.finite, r.peak);
        std::printf ("drawn    peak %.2f  maxJump %.3f (random drawn curves, mixed effects)\n", r.peak, r.maxJump);
    }

    // 6. Effect settings: random extremes for every setting, mixed effects, random rates and blocks.
    {
        std::mt19937 rng (42);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        float worstPeak = 0;
        int bad = 0;
        for (int trial = 0; trial < 120; ++trial)
        {
            Params p; p.patternBeats = patternBeatsForIndex (trial % 4); p.steps.fill (true);
            for (int i = 0; i < kMaxSteps; ++i) p.stepEffect[(size_t) i] = (Effect) ((i + trial) % kNumEffects);
            p.shape = (Shape) (trial % 4); p.hitLength = hitLengthForIndex (trial % 4);
            p.depth = 1; p.mix = u (rng);
            auto pick = [&] (float lo, float hi) { const float r = u (rng); return r < 0.3f ? lo : (r > 0.7f ? hi : lo + (hi - lo) * u (rng)); };
            auto& f = p.fx;
            f.delayTime = trial % 6; f.delayFeedback = pick (0, 0.9f); f.delayToneHz = pick (1000, 12000); f.delayPingPong = trial % 2;
            f.verbSize = pick (0, 1); f.verbDamping = pick (0, 1); f.verbWidth = pick (0, 1);
            f.filterMode = (FilterMode) (trial % 3); f.filterResonance = pick (0, 1); f.filterRange = pick (0, 1);
            f.stutterSlice = trial % 4; f.stutterSmooth = pick (0, 1);
            f.pitchSemitones = pick (1, 24); f.pitchUp = (trial / 2) % 2; f.pitchGrainMs = pick (20, 100);
            f.crushBits = pick (2, 12); f.crushRate = pick (1, 32);
            p.smoothMs = pick (0.5f, 50.0f);
            const double sr = trial % 3 == 0 ? 44100.0 : (trial % 3 == 1 ? 48000.0 : 96000.0);
            const int block = 32 + (trial * 97) % 1000;
            const double bpm = 70 + (trial * 13) % 110;
            const Result r = run (p, nullptr, sr, block, bpm, 2.5);
            if (! r.finite || r.peak > 4.0f) { ++bad; std::printf ("  trial %d: finite %d peak %.2f\n", trial, r.finite, r.peak); }
            worstPeak = std::max (worstPeak, r.peak);
        }
        CHECK (bad == 0, "effect settings fuzz: %d bad trials", bad);
        std::printf ("settings fuzz: 120 random setting combinations, worst peak %.2f\n", worstPeak);
    }

    // 7. Each setting actually changes the sound.
    {
        // Renders 4 s of a 220 Hz tone through one setting and returns the left channel.
        auto render = [&] (Effect fx, const std::function<void (EffectSettings&)>& tweak)
        {
            Params p; p.stepEffect.fill (fx); p.steps.fill (false); p.steps[1] = p.steps[3] = true;
            p.shape = Shape::Pulse; p.depth = 1; p.mix = 1; p.hitLength = 0.5f;
            tweak (p.fx);
            Engine e; e.prepare (48000);
            const int total = 48000 * 4, block = 256;
            std::vector<float> out ((size_t) total), L (block), R (block);
            double ppq = 0;
            for (int done = 0; done < total; done += block)
            {
                for (int i = 0; i < block; ++i)
                    L[(size_t) i] = R[(size_t) i] = 0.5f * std::sin (2.0f * kPi * 220.0f * (float) (done + i) / 48000.0f);
                e.process (L.data(), R.data(), block, p, nullptr, true, ppq, 120);
                std::copy (L.begin(), L.end(), out.begin() + done);
                ppq += block * 120.0 / 60.0 / 48000.0;
            }
            return out;
        };
        struct Case { const char* name; Effect fx; std::function<void (EffectSettings&)> a, b; };
        const Case cases[] = {
            { "delay feedback", Effect::DelayThrow,  [] (auto& f) { f.delayFeedback = 0.0f; }, [] (auto& f) { f.delayFeedback = 0.85f; } },
            { "delay time",     Effect::DelayThrow,  [] (auto& f) { f.delayTime = 0; },        [] (auto& f) { f.delayTime = 5; } },
            { "reverb size",    Effect::ReverbThrow, [] (auto& f) { f.verbSize = 0.0f; },      [] (auto& f) { f.verbSize = 1.0f; } },
            { "filter mode",    Effect::FilterSweep, [] (auto& f) { f.filterMode = FilterMode::LowPass; }, [] (auto& f) { f.filterMode = FilterMode::HighPass; } },
            { "filter range",   Effect::FilterSweep, [] (auto& f) { f.filterRange = 0.1f; },   [] (auto& f) { f.filterRange = 1.0f; } },
            { "stutter slice",  Effect::Stutter,     [] (auto& f) { f.stutterSlice = 0; },     [] (auto& f) { f.stutterSlice = 3; } },
            { "pitch range",    Effect::PitchDip,    [] (auto& f) { f.pitchSemitones = 1; },   [] (auto& f) { f.pitchSemitones = 24; } },
            { "pitch dir",      Effect::PitchDip,    [] (auto& f) { f.pitchUp = false; },      [] (auto& f) { f.pitchUp = true; } },
            { "crush bits",     Effect::Bitcrush,    [] (auto& f) { f.crushBits = 12; f.crushRate = 1; }, [] (auto& f) { f.crushBits = 2; f.crushRate = 1; } },
        };
        for (const auto& c : cases)
        {
            // the two settings must produce clearly different audio
            const auto a = render (c.fx, c.a), b = render (c.fx, c.b);
            double diff = 0, sig = 0;
            for (size_t i = 0; i < a.size(); ++i) { diff += (a[i] - b[i]) * (a[i] - b[i]); sig += a[i] * a[i]; }
            const double rel = std::sqrt (diff / std::max (1e-12, sig));
            CHECK (rel > 0.05, "%s has no audible effect (difference %.3f)", c.name, rel);
            std::printf ("  %-15s difference %.2f\n", c.name, rel);
        }
        std::printf ("settings change the sound: ok\n");
    }

    // 8. Stopped transport: inserts pass audio unchanged.
    {
        Params p = allBeats (Effect::FilterSweep); p.steps.fill (true); p.depth = 1; p.mix = 1;
        const Result r = run (p, nullptr, 48000, 512, 120, 2.0, false);
        CHECK (r.energyHit + r.energyQuiet < 1e-3, "stopped transport should not process inserts (%.4f)", r.energyHit + r.energyQuiet);
        std::printf ("stopped transport: ok\n");
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
