#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SweepEngine.h"

class SweepbarProcessor : public juce::AudioProcessor
{
public:
    SweepbarProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Snapshot of all parameters, used by the audio thread and the editor.
    sweep::Params currentParams() const;

    // Editor helpers: change a parameter as one undoable, automatable gesture.
    void setStepOn (int step, bool on);
    void setStepEffect (int step, int effect);
    int brushEffect() const;

    // Drawn curves. beginDrawing copies the preset shape into the beat the first time it's drawn on.
    void beginDrawing (int beat);
    void resetCurve (int beat);
    void resetAllCurves();

    static const juce::StringArray effectNames, patternNames, shapeNames, lengthNames;

    // The gear panel for each effect lists these parameter IDs, in display order.
    static const std::array<juce::StringArray, sweep::kNumEffects> effectSettingIds;
    static juce::String stepId (int i)   { return "step" + juce::String (i + 1); }
    static juce::String effectId (int i) { return "fx" + juce::String (i + 1); }

    juce::AudioProcessorValueTreeState apvts;
    sweep::Engine engine;

    // Peak levels for the editor's meters (linear, after input gain / after output gain).
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };
    sweep::CurveBank curves;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* pEffect = nullptr;
    std::atomic<float>* pPattern = nullptr;
    std::atomic<float>* pShape = nullptr;
    std::atomic<float>* pLength = nullptr;
    std::atomic<float>* pDepth = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::array<std::atomic<float>*, sweep::kMaxSteps> pSteps {};
    std::array<std::atomic<float>*, sweep::kMaxSteps> pStepFx {};
    std::atomic<float>* raw (const char* id) const { return apvts.getRawParameterValue (id); }
    float lastInGain = 1.0f, lastOutGain = 1.0f;

    void setParam (const juce::String& id, float denormalisedValue);

    juce::AudioBuffer<float> monoScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepbarProcessor)
};
