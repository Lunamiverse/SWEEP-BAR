// Native harness: runs the real SweepbarProcessor + editor headless.
//  - processes audio with a fake host playhead (per-beat effects, drawn curves, mono, odd block sizes)
//  - state save/restore including drawn curves, and loading a 0.1 project
//  - simulated mouse clicks and drawing in the editor
//  - renders the editor to PNG files for visual inspection
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 128;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.toRawUTF8()); ++failures; }
}

static void setValue (SweepbarProcessor& p, const juce::String& id, float denormalised)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (denormalised));
}

static float runAudio (SweepbarProcessor& p, FakePlayHead& ph, double seconds, double sr = 48000.0, int block = 512)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    float peak = 0;
    long t = 0;
    const int blocks = (int) (seconds * sr / block);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const float s = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 330.0f * (float) (t + i) / (float) sr);
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (ch, i);
                if (! std::isfinite (v)) return -1.0f;
                peak = std::max (peak, std::abs (v));
            }
        if (ph.playing) ph.ppq += block * ph.bpm / 60.0 / sr;
        t += block;
    }
    return peak;
}

static juce::MouseEvent mouseEvent (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool right, bool dragged)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto mods = right ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)
                            : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, 1, dragged);
}

template <typename T>
static T* findChild (juce::Component& parent)
{
    for (auto* c : parent.getChildren())
        if (auto* t = dynamic_cast<T*> (c))
            return t;
    return nullptr;
}

static void snapshot (juce::Component& ed, const juce::String& file)
{
    auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
    juce::File f (file);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, out);
    std::printf ("  wrote %s\n", file.toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String outDir = argc > 1 ? argv[1] : ".";

    // ---- audio through the real plugin: every effect on every pattern length, then mixed + drawn
    {
        SweepbarProcessor p;
        FakePlayHead ph;
        p.setPlayHead (&ph);
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        setValue (p, "mix", 1.0f);
        for (int fx = 0; fx < 6; ++fx)
            for (int pat = 0; pat < 4; ++pat)
            {
                for (int s = 0; s < 16; ++s) setValue (p, SweepbarProcessor::effectId (s), (float) fx);
                setValue (p, "pattern", (float) pat);
                const float peak = runAudio (p, ph, 3.0);
                check (peak >= 0 && peak < 2.5f, "effect " + juce::String (fx) + " pattern " + juce::String (pat)
                                                      + " peak " + juce::String (peak));
            }
        // every beat on, a different effect on each, some beats hand-drawn
        for (int s = 0; s < 16; ++s)
        {
            setValue (p, SweepbarProcessor::stepId (s), 1.0f);
            setValue (p, SweepbarProcessor::effectId (s), (float) (s % 6));
        }
        for (int b = 0; b < 16; b += 3)
        {
            p.beginDrawing (b);
            for (int i = 0; i < sweep::kCurvePoints; ++i) p.curves.setPoint (b, i, (i % 8) < 4 ? 1.0f : 0.0f);
        }
        for (int pat = 0; pat < 4; ++pat)
        {
            setValue (p, "pattern", (float) pat);
            const float peak = runAudio (p, ph, 4.0);
            check (peak >= 0 && peak < 2.5f, "mixed effects pattern " + juce::String (pat) + " peak " + juce::String (peak));
        }
        juce::AudioBuffer<float> buf (2, 1000);
        juce::MidiBuffer midi;
        for (int n : { 1, 7, 333, 1000, 64 })
        {
            buf.setSize (2, n, false, false, true);
            buf.clear();
            p.processBlock (buf, midi);
        }
        ph.playing = false;
        check (runAudio (p, ph, 1.0) >= 0, "stopped transport");
        std::printf ("audio: all effects x pattern lengths, mixed effects with drawn curves\n");
    }

    // ---- mono layout
    {
        SweepbarProcessor p;
        FakePlayHead ph;
        p.setPlayHead (&ph);
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.setBusesLayout (mono), "mono layout accepted");
        p.prepareToPlay (44100.0, 256);
        juce::AudioBuffer<float> buf (1, 256);
        juce::MidiBuffer midi;
        bool finite = true;
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < 256; ++i) buf.setSample (0, i, 0.3f * std::sin ((float) (b * 256 + i) * 0.05f));
            p.processBlock (buf, midi);
            ph.ppq += 256 * 128.0 / 60.0 / 44100.0;
            finite = finite && std::isfinite (buf.getSample (0, 100));
        }
        check (finite, "mono output finite");
        std::printf ("mono: ok\n");
    }

    // ---- state save / restore, including per-beat effects and drawn curves
    {
        SweepbarProcessor a;
        setValue (a, "pattern", 1);
        setValue (a, "shape", 2);
        setValue (a, "depth", 0.33f);
        setValue (a, SweepbarProcessor::stepId (5), 1.0f);
        setValue (a, SweepbarProcessor::stepId (1), 0.0f);
        setValue (a, SweepbarProcessor::effectId (3), 4.0f);
        setValue (a, SweepbarProcessor::effectId (5), 3.0f);
        a.beginDrawing (3);
        for (int i = 0; i < sweep::kCurvePoints; ++i) a.curves.setPoint (3, i, (float) i / 63.0f);
        juce::MemoryBlock state;
        a.getStateInformation (state);

        SweepbarProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        const auto pa = a.currentParams(), pb = b.currentParams();
        check (pa.patternBeats == pb.patternBeats && pa.shape == pb.shape, "choices restored");
        check (std::abs (pa.depth - pb.depth) < 1e-4f, "depth restored");
        check (pa.steps == pb.steps, "steps restored");
        check (pa.stepEffect == pb.stepEffect, "per-beat effects restored");
        check (pb.stepEffect[3] == sweep::Effect::PitchDip && pb.stepEffect[5] == sweep::Effect::Stutter, "restored effects are the ones set");
        check (b.curves.isDrawn (3) && ! b.curves.isDrawn (0), "drawn flags restored");
        check (std::abs (b.curves.point (3, 32) - 32.0f / 63.0f) < 1e-3f, "drawn curve points restored");

        // saving twice must not duplicate the curve data
        juce::MemoryBlock again;
        b.getStateInformation (again);
        check (std::abs ((int) again.getSize() - (int) state.getSize()) < 64, "re-saving doesn't grow the state");
        std::printf ("state: ok (%d bytes)\n", (int) state.getSize());
    }

    // ---- loading a project saved by 0.1 (one effect for all beats, no fxN params, no curves)
    {
        SweepbarProcessor a;
        auto tree = a.apvts.copyState();
        for (int i = tree.getNumChildren(); --i >= 0;)
            if (tree.getChild (i).getProperty ("id").toString().startsWith ("fx"))
                tree.removeChild (i, nullptr);
        for (auto child : tree)
            if (child.getProperty ("id").toString() == "effect")
                child.setProperty ("value", 3.0f, nullptr);   // stutter
        juce::MemoryBlock old;
        a.copyXmlToBinary (*tree.createXml(), old);

        SweepbarProcessor b;
        b.setStateInformation (old.getData(), (int) old.getSize());
        const auto pb = b.currentParams();
        bool allStutter = true;
        for (auto fx : pb.stepEffect) allStutter = allStutter && fx == sweep::Effect::Stutter;
        check (allStutter, "0.1 project: its single effect carries over to every beat");
        std::printf ("0.1 project migration: ok\n");
    }

    // ---- editor interaction: paint beats and draw on the curve with simulated mouse events
    {
        SweepbarProcessor p;
        FakePlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* grid = findChild<ui::StepGrid> (*ed);
        auto* curve = findChild<ui::CurveView> (*ed);
        check (grid != nullptr && curve != nullptr, "editor has step grid and curve view");
        if (grid != nullptr && curve != nullptr)
        {
            // default pattern: 1 bar, beat 2 filter, beat 4 delay. Brush = reverb.
            setValue (p, "effect", 1.0f);
            auto clickStep = [&] (int i)
            {
                const juce::Point<float> pt = grid->cell (i, 4).getCentre();
                grid->mouseDown (mouseEvent (*grid, pt, pt, false, false));
            };
            clickStep (0);   // off -> on with reverb
            auto s = p.currentParams();
            check (s.steps[0] && s.stepEffect[0] == sweep::Effect::ReverbThrow, "click on an off beat adds it with the brush effect");
            clickStep (1);   // on (filter) -> repainted reverb
            s = p.currentParams();
            check (s.steps[1] && s.stepEffect[1] == sweep::Effect::ReverbThrow, "click on a beat with another effect repaints it");
            clickStep (1);   // on (reverb) -> off
            s = p.currentParams();
            check (! s.steps[1], "click on a beat with the same effect removes it");

            // draw a stroke across beat 3 (which is off): it should turn on and get a drawn curve
            const float W = (float) curve->getWidth();
            const float H = (float) curve->getHeight() - 22.0f;
            const juce::Point<float> a (W * 0.52f, H * 0.9f), b (W * 0.74f, H * 0.1f);
            curve->mouseDown (mouseEvent (*curve, a, a, false, false));
            for (int k = 1; k <= 10; ++k)
            {
                const auto pt = a + (b - a) * ((float) k / 10.0f);
                curve->mouseDrag (mouseEvent (*curve, pt, a, false, true));
            }
            s = p.currentParams();
            check (s.steps[2] && s.stepEffect[2] == sweep::Effect::ReverbThrow, "drawing on an off beat turns it on with the brush effect");
            check (p.curves.isDrawn (2), "drawn beat is marked drawn");
            const float early = p.curves.at (2, 0.1), late = p.curves.at (2, 0.9);
            check (late > early + 0.3f, "stroke going up draws a rising curve (" + juce::String (early) + " -> " + juce::String (late) + ")");

            // right-drag erases
            const juce::Point<float> e1 (W * 0.55f, H * 0.5f), e2 (W * 0.70f, H * 0.5f);
            curve->mouseDown (mouseEvent (*curve, e1, e1, true, false));
            curve->mouseDrag (mouseEvent (*curve, e2, e1, true, true));
            check (p.curves.at (2, 0.5) < 1e-4f, "right-drag erases");

            p.resetAllCurves();
            check (! p.curves.isDrawn (2), "reset drawing clears drawn curves");
        }
        // gear button opens that effect's settings card; clicking it again closes it
        auto* tiles = findChild<ui::EffectTiles> (*ed);
        auto* card = findChild<ui::SettingsCard> (*ed);
        check (tiles != nullptr && card != nullptr, "editor has effect tiles and settings card");
        if (tiles != nullptr && card != nullptr)
        {
            auto clickGear = [&] (int i)
            {
                const juce::Point<float> pt = tiles->gearBounds (i).getCentre();
                tiles->mouseDown (mouseEvent (*tiles, pt, pt, false, false));
            };
            clickGear (1);
            check (card->isVisible() && card->effect() == 1, "gear opens the reverb settings");
            int knobs = 0;
            for (auto* c : card->getChildren()) knobs += dynamic_cast<ui::Knob*> (c) != nullptr ? 1 : 0;
            check (knobs == 3, "reverb settings shows size, damping, width knobs (" + juce::String (knobs) + ")");
            clickGear (4);
            check (card->isVisible() && card->effect() == 4, "another gear switches the card to pitch");
            clickGear (4);
            check (! card->isVisible(), "clicking the same gear closes the card");
            // brush click on a tile body still selects the effect
            const juce::Point<float> body = tiles->tileBounds (3).getCentre().translated (-30.0f, 0.0f);
            tiles->mouseDown (mouseEvent (*tiles, body, body, false, false));
            check (p.brushEffect() == 3, "clicking a tile selects it for painting");
        }
        std::printf ("editor interaction: ok\n");
    }

    // ---- every settings parameter is saved and restored
    {
        SweepbarProcessor a;
        const char* ids[] = { "dlyTime", "dlyFeedback", "dlyTone", "dlyPingPong", "verbSize", "verbDamp", "verbWidth",
                              "fltMode", "fltReso", "fltRange", "stuSlice", "stuSmooth", "pchDir", "pchRange", "pchGrain",
                              "crsBits", "crsRate", "smooth", "inGain", "outGain" };
        for (auto* id : ids)
            a.apvts.getParameter (id)->setValueNotifyingHost (0.77f);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        SweepbarProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        for (auto* id : ids)
        {
            const float va = a.apvts.getParameter (id)->getValue(), vb = b.apvts.getParameter (id)->getValue();
            const bool isBool = dynamic_cast<juce::AudioParameterBool*> (a.apvts.getParameter (id)) != nullptr;
            check (isBool ? ((va > 0.5f) == (vb > 0.5f)) : std::abs (va - vb) < 1e-3f, juce::String ("setting restored: ") + id);
        }
        std::printf ("effect settings state: ok\n");
    }

    // ---- editor snapshots
    {
        struct Shot { int pattern; bool playing; double ppq; bool mixed; bool drawn; const char* name; };
        const Shot shots[] = {
            { 0, true,  1.2,  false, false, "ui_default" },
            { 0, true,  2.3,  true,  true,  "ui_1bar_mixed_drawn" },
            { 1, true,  10.3, true,  false, "ui_6beats_mixed" },
            { 3, true,  30.4, true,  true,  "ui_4bars_mixed_drawn" },
        };
        for (const auto& s : shots)
        {
            SweepbarProcessor p;
            FakePlayHead ph;
            ph.ppq = s.ppq;
            ph.playing = s.playing;
            p.setPlayHead (&ph);
            p.prepareToPlay (48000.0, 512);
            setValue (p, "pattern", (float) s.pattern);
            if (s.mixed)
            {
                const int n = sweep::patternBeatsForIndex (s.pattern);
                for (int i = 0; i < n; ++i)
                {
                    setValue (p, SweepbarProcessor::stepId (i), (i % 3 != 2) ? 1.0f : 0.0f);
                    setValue (p, SweepbarProcessor::effectId (i), (float) (i % 6));
                }
                setValue (p, "effect", 2.0f);
            }
            if (s.drawn)
            {
                // a hand-drawn stutter-style chop on beat 1 and a slow rise on beat 2
                p.beginDrawing (0);
                for (int i = 0; i < sweep::kCurvePoints; ++i) p.curves.setPoint (0, i, (i / 8) % 2 == 0 ? 0.95f : 0.15f);
                p.beginDrawing (1);
                for (int i = 0; i < sweep::kCurvePoints; ++i) p.curves.setPoint (1, i, std::pow ((float) i / 63.0f, 2.0f));
            }
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            buf.clear();
            p.processBlock (buf, midi);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            snapshot (*ed, outDir + "/" + s.name + ".png");
        }

        // settings cards
        for (int fx : { 0, 2, 4 })
        {
            SweepbarProcessor p;
            FakePlayHead ph;
            ph.ppq = 1.2;
            p.setPlayHead (&ph);
            p.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            buf.clear();
            p.processBlock (buf, midi);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            auto* tiles = findChild<ui::EffectTiles> (*ed);
            const juce::Point<float> pt = tiles->gearBounds (fx).getCentre();
            tiles->mouseDown (mouseEvent (*tiles, pt, pt, false, false));
            snapshot (*ed, outDir + "/ui_settings_" + juce::String (fx) + ".png");
        }
    }

    // ---- every finish, and paint cost
    {
        SweepbarProcessor p;
        FakePlayHead ph;
        ph.ppq = 2.2;
        p.setPlayHead (&ph);
        p.prepareToPlay (48000.0, 512);
        for (int i = 0; i < 16; ++i)
        {
            p.apvts.getParameter (SweepbarProcessor::stepId (i))->setValueNotifyingHost (i % 3 != 2 ? 1.0f : 0.0f);
            p.apvts.getParameter (SweepbarProcessor::effectId (i))->setValueNotifyingHost (p.apvts.getParameter (SweepbarProcessor::effectId (i))->convertTo0to1 ((float) (i % 6)));
        }
        p.apvts.getParameter ("pattern")->setValueNotifyingHost (1.0f);   // 4 bars
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        buf.clear();
        p.processBlock (buf, midi);
        for (int f = 0; f < 5; ++f)
        {
            p.apvts.state.setProperty ("finish", f, nullptr);
            const auto tOpen = juce::Time::getMillisecondCounterHiRes();
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            std::printf ("  window open with hardware render: %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - tOpen);
            // a loud-ish vocal-level signal so the meters light up
            for (int i = 0; i < 512; ++i) { const float v = 0.45f * std::sin ((float) i * 0.07f); buf.setSample (0, i, v); buf.setSample (1, i, v * 0.9f); }
            p.processBlock (buf, midi);
            for (auto* c : ed->getChildren())
                if (auto* m = dynamic_cast<ui::LevelMeter*> (c)) m->tick();
            snapshot (*ed, outDir + "/ui_finish_" + juce::String (f) + ".png");
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto img = juce::Image (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 10; ++k) { juce::Graphics g (img); ed->paintEntireComponent (g, true); }
        const double full = (juce::Time::getMillisecondCounterHiRes() - t0) / 10.0;
        auto* curve = findChild<ui::CurveView> (*ed);
        auto cimg = juce::Image (juce::Image::ARGB, curve->getWidth(), curve->getHeight(), true);
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 10; ++k) { juce::Graphics g (cimg); curve->paintEntireComponent (g, true); }
        const double cv = (juce::Time::getMillisecondCounterHiRes() - t1) / 10.0;
        std::printf ("paint cost: whole window %.1f ms, curve screen %.1f ms (16 neon beats)\n", full, cv);
        check (cv < 40.0, "curve screen paints fast enough for live updates");
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL PLUGIN TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
