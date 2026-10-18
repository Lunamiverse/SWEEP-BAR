#include "PluginProcessor.h"
#include "PluginEditor.h"

const juce::StringArray SweepbarProcessor::effectNames  { "Delay throw", "Reverb throw", "Filter sweep", "Stutter", "Pitch dip", "Bitcrush" };
const juce::StringArray SweepbarProcessor::patternNames { "1 bar", "6 beats", "2 bars", "4 bars" };
const juce::StringArray SweepbarProcessor::shapeNames   { "Ramp down", "Ramp up", "Pulse", "Swell" };
const juce::StringArray SweepbarProcessor::lengthNames  { "1/16", "1/8", "3/16", "1/4" };

const std::array<juce::StringArray, sweep::kNumEffects> SweepbarProcessor::effectSettingIds {
    juce::StringArray { "dlyTime", "dlyFeedback", "dlyTone", "dlyPingPong" },
    juce::StringArray { "verbSize", "verbDamp", "verbWidth" },
    juce::StringArray { "fltMode", "fltReso", "fltRange" },
    juce::StringArray { "stuSlice", "stuSmooth" },
    juce::StringArray { "pchDir", "pchRange", "pchGrain" },
    juce::StringArray { "crsBits", "crsRate" },
};

SweepbarProcessor::SweepbarProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SweepbarState", createLayout())
{
    pEffect  = apvts.getRawParameterValue ("effect");
    pPattern = apvts.getRawParameterValue ("pattern");
    pShape   = apvts.getRawParameterValue ("shape");
    pLength  = apvts.getRawParameterValue ("length");
    pDepth   = apvts.getRawParameterValue ("depth");
    pMix     = apvts.getRawParameterValue ("mix");
    for (int i = 0; i < sweep::kMaxSteps; ++i)
    {
        pSteps[(size_t) i]  = apvts.getRawParameterValue (stepId (i));
        pStepFx[(size_t) i] = apvts.getRawParameterValue (effectId (i));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SweepbarProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // "effect" is the brush: the effect a beat gets when you click it on. Each beat's own effect is fxN.
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "effect", 1 },  "Effect for new beats", effectNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pattern", 1 }, "Pattern length", patternNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "shape", 1 },   "Shape",          shapeNames,   3));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "length", 1 },  "Hit length",     lengthNames,  1));

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "depth", 1 }, "Depth",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.8f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));

    for (int i = 0; i < sweep::kMaxSteps; ++i)
    {
        const bool on = (i == 1 || i == 3 || i == 15);   // beats 2 and 4, plus the last beat of a 4-bar phrase
        const String name = "Step " + String (i / 4 + 1) + "." + String (i % 4 + 1);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { stepId (i), 1 }, name, on));
    }
    for (int i = 0; i < sweep::kMaxSteps; ++i)
    {
        // defaults: filter sweep on beat 2, delay throw on beat 4, reverb throw at the end of a 4-bar phrase
        const int fx = i == 1 ? 2 : (i == 15 ? 1 : 0);
        const String name = "Step " + String (i / 4 + 1) + "." + String (i % 4 + 1) + " effect";
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { effectId (i), 2 }, name, effectNames, fx));
    }

    // ---- per-effect settings (gear panels), added in 0.3.0
    auto pct = [] (const char* id, const char* name, float def, float max = 1.0f)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 3 }, name, NormalisableRange<float> (0.0f, max), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; }));
    };
    auto ranged = [] (const char* id, const char* name, float lo, float hi, float def, float centre, std::function<String (float)> fmt, float step = 0.0f)
    {
        NormalisableRange<float> r (lo, hi, step);
        if (centre > 0) r.setSkewForCentre (centre);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 3 }, name, r, def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); }));
    };
    auto hz = [] (float v) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto ms = [] (float v) { return String (v, v < 10.0f ? 1 : 0) + " ms"; };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "dlyTime", 3 }, "Delay time",
                StringArray { "1/16", "1/8", "1/8 dot", "1/4", "1/4 dot", "1/2" }, 2));
    layout.add (pct ("dlyFeedback", "Delay feedback", 0.45f, 0.9f));
    layout.add (ranged ("dlyTone", "Delay tone", 1000.0f, 12000.0f, 5500.0f, 4000.0f, hz));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "dlyPingPong", 3 }, "Delay ping-pong", true));

    layout.add (pct ("verbSize", "Reverb size", 0.75f));
    layout.add (pct ("verbDamp", "Reverb damping", 0.3f));
    layout.add (pct ("verbWidth", "Reverb width", 1.0f));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fltMode", 3 }, "Filter mode",
                StringArray { "Telephone", "Low-pass", "High-pass" }, 0));
    layout.add (pct ("fltReso", "Filter resonance", 0.4f));
    layout.add (pct ("fltRange", "Filter range", 0.8f));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stuSlice", 3 }, "Stutter slice",
                StringArray { "1/32", "1/16", "1/8", "1/4" }, 1));
    layout.add (pct ("stuSmooth", "Stutter smoothing", 0.3f));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pchDir", 3 }, "Pitch direction",
                StringArray { "Down", "Up" }, 0));
    layout.add (ranged ("pchRange", "Pitch range", 1.0f, 24.0f, 12.0f, 0.0f,
                        [] (float v) { return String (roundToInt (v)) + " st"; }, 1.0f));
    layout.add (ranged ("pchGrain", "Pitch grain", 20.0f, 100.0f, 45.0f, 0.0f, ms));

    layout.add (ranged ("crsBits", "Crush bits", 2.0f, 12.0f, 4.0f, 0.0f,
                        [] (float v) { return String (v, 1) + " bits"; }));
    layout.add (ranged ("crsRate", "Crush rate", 1.0f, 32.0f, 15.0f, 8.0f,
                        [] (float v) { return String (v, 1) + "x"; }));

    // ---- global, added in 0.3.0
    layout.add (ranged ("smooth", "Smoothing", 0.5f, 50.0f, 2.5f, 8.0f, ms));
    auto db = [] (float v) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
    layout.add (ranged ("inGain", "Input gain", -24.0f, 12.0f, 0.0f, 0.0f, db));
    layout.add (ranged ("outGain", "Output gain", -24.0f, 12.0f, 0.0f, 0.0f, db));
    return layout;
}

void SweepbarProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void SweepbarProcessor::setStepOn (int step, bool on)          { setParam (stepId (step), on ? 1.0f : 0.0f); }
void SweepbarProcessor::setStepEffect (int step, int effect)   { setParam (effectId (step), (float) effect); }
int  SweepbarProcessor::brushEffect() const                    { return juce::jlimit (0, 5, (int) pEffect->load()); }

void SweepbarProcessor::beginDrawing (int beat)
{
    if (curves.isDrawn (beat))
        return;
    // Start from what the beat looks like now, so drawing edits the visible curve.
    const auto p = currentParams();
    for (int i = 0; i < sweep::kCurvePoints; ++i)
        curves.setPoint (beat, i, sweep::presetCurveAt (p, (double) i / (sweep::kCurvePoints - 1)));
    curves.drawn[(size_t) beat].store (true);
}

void SweepbarProcessor::resetCurve (int beat)  { curves.drawn[(size_t) beat].store (false); }
void SweepbarProcessor::resetAllCurves()       { for (auto& d : curves.drawn) d.store (false); }

sweep::Params SweepbarProcessor::currentParams() const
{
    sweep::Params p;
    p.patternBeats = sweep::patternBeatsForIndex ((int) pPattern->load());
    p.shape        = (sweep::Shape) juce::jlimit (0, (int) sweep::Shape::Count - 1, (int) pShape->load());
    p.hitLength    = sweep::hitLengthForIndex ((int) pLength->load());
    p.depth        = pDepth->load();
    p.mix          = pMix->load();
    for (int i = 0; i < sweep::kMaxSteps; ++i)
    {
        p.steps[(size_t) i] = pSteps[(size_t) i]->load() > 0.5f;
        p.stepEffect[(size_t) i] = (sweep::Effect) juce::jlimit (0, sweep::kNumEffects - 1, (int) pStepFx[(size_t) i]->load());
    }

    auto& f = p.fx;
    f.delayTime       = (int) raw ("dlyTime")->load();
    f.delayFeedback   = raw ("dlyFeedback")->load();
    f.delayToneHz     = raw ("dlyTone")->load();
    f.delayPingPong   = raw ("dlyPingPong")->load() > 0.5f;
    f.verbSize        = raw ("verbSize")->load();
    f.verbDamping     = raw ("verbDamp")->load();
    f.verbWidth       = raw ("verbWidth")->load();
    f.filterMode      = (sweep::FilterMode) juce::jlimit (0, 2, (int) raw ("fltMode")->load());
    f.filterResonance = raw ("fltReso")->load();
    f.filterRange     = raw ("fltRange")->load();
    f.stutterSlice    = (int) raw ("stuSlice")->load();
    f.stutterSmooth   = raw ("stuSmooth")->load();
    f.pitchUp         = raw ("pchDir")->load() > 0.5f;
    f.pitchSemitones  = raw ("pchRange")->load();
    f.pitchGrainMs    = raw ("pchGrain")->load();
    f.crushBits       = raw ("crsBits")->load();
    f.crushRate       = raw ("crsRate")->load();
    p.smoothMs        = raw ("smooth")->load();
    return p;
}

void SweepbarProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    monoScratch.setSize (1, juce::jmax (1, samplesPerBlock), false, false, true);
}

bool SweepbarProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return in == out;
}

void SweepbarProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            playing = pos->getIsPlaying();
            ppq = pos->getPpqPosition().orFallback (0.0);
            bpm = pos->getBpm().orFallback (120.0);
        }
    }

    const auto params = currentParams();
    float* left = buffer.getWritePointer (0);
    float* right;
    if (buffer.getNumChannels() > 1)
    {
        right = buffer.getWritePointer (1);
    }
    else
    {
        if (monoScratch.getNumSamples() < numSamples)
            monoScratch.setSize (1, numSamples, false, false, true);   // only if the host lied about block size
        monoScratch.copyFrom (0, 0, buffer, 0, 0, numSamples);
        right = monoScratch.getWritePointer (0);
    }

    // input / output gain, ramped across the block to avoid zipper noise
    const float inGain  = juce::Decibels::decibelsToGain (raw ("inGain")->load());
    const float outGain = juce::Decibels::decibelsToGain (raw ("outGain")->load());
    const int nch = juce::jmin (2, buffer.getNumChannels());
    for (int ch = 0; ch < nch; ++ch)
        buffer.applyGainRamp (ch, 0, numSamples, lastInGain, inGain);
    if (buffer.getNumChannels() == 1)
        juce::FloatVectorOperations::copy (right, left, numSamples);
    lastInGain = inGain;
    {
        float peak = 0.0f;
        for (int ch = 0; ch < nch; ++ch) peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
        inLevel.store (juce::jmax (peak, inLevel.load (std::memory_order_relaxed)), std::memory_order_relaxed);   // editor resets after reading
    }

    engine.process (left, right, numSamples, params, &curves, playing, ppq, bpm);

    for (int ch = 0; ch < nch; ++ch)
        buffer.applyGainRamp (ch, 0, numSamples, lastOutGain, outGain);
    if (buffer.getNumChannels() == 1)
        juce::FloatVectorOperations::multiply (right, outGain, numSamples);   // folded into mono below
    lastOutGain = outGain;
    {
        float peak = juce::FloatVectorOperations::findMaximum (left, numSamples);
        peak = juce::jmax (peak, -juce::FloatVectorOperations::findMinimum (left, numSamples));
        if (buffer.getNumChannels() > 1)
            peak = juce::jmax (peak, buffer.getMagnitude (1, 0, numSamples));
        outLevel.store (juce::jmax (peak, outLevel.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    }

    if (buffer.getNumChannels() == 1)
    {
        // fold the stereo effect output back down to mono
        juce::FloatVectorOperations::add (left, right, numSamples);
        juce::FloatVectorOperations::multiply (left, 0.5f, numSamples);
    }
}

static const juce::Identifier curvesTag ("Curves"), beatTag ("Beat"), indexAttr ("index"), pointsAttr ("points");

void SweepbarProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName (curvesTag), nullptr);

    // Drawn curves aren't parameters, so they're stored next to them: one child per drawn beat.
    juce::ValueTree curveTree (curvesTag);
    for (int b = 0; b < sweep::kMaxSteps; ++b)
    {
        if (! curves.isDrawn (b))
            continue;
        juce::StringArray pts;
        for (int i = 0; i < sweep::kCurvePoints; ++i)
            pts.add (juce::String (curves.point (b, i), 4));
        curveTree.appendChild (juce::ValueTree (beatTag, { { indexAttr, b }, { pointsAttr, pts.joinIntoString (",") } }), nullptr);
    }
    state.appendChild (curveTree, nullptr);
    state.setProperty ("version", JucePlugin_VersionString, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SweepbarProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // Projects saved with 0.1 had one effect for every beat and no per-beat effect parameters.
    bool hasPerBeatEffects = false;
    float oldEffect = 0.0f;
    for (auto* param : xml->getChildWithTagNameIterator ("PARAM"))
    {
        if (param->getStringAttribute ("id") == effectId (0)) hasPerBeatEffects = true;
        if (param->getStringAttribute ("id") == "effect") oldEffect = (float) param->getDoubleAttribute ("value");
    }

    auto tree = juce::ValueTree::fromXml (*xml);
    curves.clear();
    const auto curveTree = tree.getChildWithName (curvesTag);
    for (const auto& beat : curveTree)
    {
        const int b = beat.getProperty (indexAttr, -1);
        if (b < 0 || b >= sweep::kMaxSteps) continue;
        juce::StringArray pts;
        pts.addTokens (beat.getProperty (pointsAttr).toString(), ",", "");
        if (pts.size() != sweep::kCurvePoints) continue;
        for (int i = 0; i < sweep::kCurvePoints; ++i)
            curves.setPoint (b, i, pts[i].getFloatValue());
        curves.drawn[(size_t) b].store (true);
    }
    tree.removeChild (curveTree, nullptr);
    apvts.replaceState (tree);

    if (! hasPerBeatEffects)
        for (int i = 0; i < sweep::kMaxSteps; ++i)
            if (auto* p = apvts.getParameter (effectId (i)))
                p->setValueNotifyingHost (p->convertTo0to1 (oldEffect));
}

juce::AudioProcessorEditor* SweepbarProcessor::createEditor()
{
    return new SweepbarEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SweepbarProcessor();
}
