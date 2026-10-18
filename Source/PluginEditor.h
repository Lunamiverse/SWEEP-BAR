#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Skin.h"

namespace ui
{
namespace colours
{
    const juce::Colour text  = skin::colours::text;
    const juce::Colour muted = skin::colours::textDim;
    const juce::Colour dim   = skin::colours::textFaint;
    const juce::Colour accent { 0xff2ee6ff };   // electric cyan: global controls
    juce::Colour effect (int index);
}

// Segmented switch bound to a choice or bool parameter (settings card).
class ChoiceButtons : public juce::Component
{
public:
    ChoiceButtons (juce::RangedAudioParameter& param, juce::StringArray labels, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int i) const;
    int indexAt (juce::Point<float>) const;
    juce::StringArray labels;
    juce::Colour accent;
    int selected = 0, hover = -1;
    juce::ParameterAttachment attachment;
};

// Row of illuminated effect switches. Click a switch to choose the paint effect, the gear for settings.
// The switch caps themselves are part of the rendered chassis; this draws their light and legends.
class EffectTiles : public juce::Component
{
public:
    explicit EffectTiles (SweepbarProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::Rectangle<float> tileBounds (int i) const;   // local
    juce::Rectangle<float> gearBounds (int i) const;   // local
    std::function<void (int effect)> onGear;
    int openSettings = -1;

private:
    SweepbarProcessor& proc;
    juce::ParameterAttachment attachment;
    int selected = 0, hoverTile = -1, hoverGear = -1;
};

// Side module: legend, icon, value window with dropdown.
class SideCell : public juce::Component
{
public:
    enum class Icon { Pattern, Shape, Length, Steps, Pencil, Sync };
    SideCell (juce::String label, Icon icon);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    static juce::Rectangle<float> windowBounds (juce::Rectangle<float> cell);   // value window inside a cell

    std::function<juce::String()> getValue;
    std::function<juce::PopupMenu()> buildMenu;
    std::function<void (int)> onMenuResult;
    juce::LookAndFeel* menuLook = nullptr;

private:
    juce::String label;
    Icon icon;
    bool hover = false;
};

// Step keys. Left-click paints with the selected effect; right-click opens a menu.
class StepGrid : public juce::Component
{
public:
    StepGrid (SweepbarProcessor&, juce::LookAndFeel& menuLook);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::Rectangle<float> cell (int i, int n) const;

private:
    void showMenu (int step);
    SweepbarProcessor& proc;
    juce::LookAndFeel& menuLook;
};

// The waveform editor: layered-light curves. Drag to draw, right-drag to erase.
class CurveView : public juce::Component
{
public:
    explicit CurveView (SweepbarProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    void drawSegment (juce::Point<float> a, juce::Point<float> b);

    SweepbarProcessor& proc;
    juce::Image fillLayer, bloomWide, bloomTight;
    juce::Point<float> last;
    bool erasing = false;
    std::array<bool, sweep::kMaxSteps> touched {};
};

// Pattern against 4 bars of the song.
class RulerView : public juce::Component
{
public:
    explicit RulerView (SweepbarProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    juce::Rectangle<float> cell (int k) const;
private:
    SweepbarProcessor& proc;
};

// Reflection drawn over a display window.
class GlassOverlay : public juce::Component
{
public:
    explicit GlassOverlay (float r = 7.0f) : radius (r) { setInterceptsMouseClicks (false, false); }
    void resized() override { image = skin::glassSheen (getWidth(), getHeight(), radius); }
    void paint (juce::Graphics& g) override { g.drawImageAt (image, 0, 0); }
private:
    float radius;
    juce::Image image;
};

// Knob: the machined body is rendered into the chassis; this draws the value ring, pointer and legends.
class Knob : public juce::Component
{
public:
    Knob (SweepbarProcessor&, const juce::String& paramId, const juce::String& caption, juce::Colour, juce::LookAndFeel&);
    ~Knob() override;
    void setGeometry (juce::Point<float> centreInParent, float diameter);
    juce::Point<float> centreInParent() const { return centre; }
    float diameter() const { return D; }
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;

private:
    juce::RangedAudioParameter* param;
    juce::String caption;
    juce::Colour colour;
    juce::Point<float> centre;
    float D = 60.0f;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// Live LED ladder meter.
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (std::atomic<float>& src) : source (src) { setInterceptsMouseClicks (false, false); }
    void tick();
    void paint (juce::Graphics&) override;
private:
    std::atomic<float>& source;
    float level = 0.0f, peak = 0.0f;
    int peakHold = 0;
    float shownLevel = -1.0f, shownPeak = -1.0f;
};

// The gear panel for one effect, shown over the display housing.
class SettingsCard : public juce::Component
{
public:
    SettingsCard (SweepbarProcessor&, juce::LookAndFeel&);
    void open (int effect, juce::Rectangle<int> area);
    void close();
    int effect() const { return current; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    std::function<void()> onClose;

private:
    SweepbarProcessor& proc;
    juce::LookAndFeel& look;
    int current = -1;
    juce::Rectangle<int> card;
    juce::Image cardImage;     // rendered relief of the card, its knob mounts and bodies
    struct Row { juce::String label; std::unique_ptr<ChoiceButtons> buttons; };
    std::vector<Row> rows;
    std::vector<std::unique_ptr<Knob>> knobs;
};

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override {}
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getPopupMenuFont() override { return skin::font (15.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};
} // namespace ui

class SweepbarEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SweepbarEditor (SweepbarProcessor&);
    ~SweepbarEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void openSettings (int effect);
    void rebuildBackground();
    int finishIndex() const;
    juce::String visualSignature() const;
    juce::PopupMenu choiceMenu (const juce::String& paramId) const;
    void setChoice (const juce::String& paramId, int index);

    SweepbarProcessor& proc;
    ui::Look look;

    ui::EffectTiles tiles;
    ui::SideCell patternCell { "PATTERN", ui::SideCell::Icon::Pattern };
    ui::SideCell shapeCell   { "SHAPE",   ui::SideCell::Icon::Shape };
    ui::SideCell lengthCell  { "LENGTH",  ui::SideCell::Icon::Length };
    ui::SideCell stepsCell   { "STEPS",   ui::SideCell::Icon::Steps };
    ui::SideCell drawCell    { "DRAWING", ui::SideCell::Icon::Pencil };
    ui::SideCell syncCell    { "SYNC",    ui::SideCell::Icon::Sync };
    ui::StepGrid stepGrid;
    ui::CurveView curve;
    ui::RulerView ruler;
    ui::Knob depthKnob, mixKnob, smoothKnob;
    juce::Slider inSlider, outSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment inAttach, outAttach;
    ui::LevelMeter inMeter, outMeter;
    ui::GlassOverlay displayGlass { 8.0f }, transportGlass { 8.0f };
    ui::SettingsCard settings;

    juce::Rectangle<int> screenPlate, stepTray, displayWell, deckPlate, rulerPlate, rulerTray,
                         transportWell, finishButton, logoPlate, effectTray;
    std::array<juce::Rectangle<int>, 6> sideCells;
    juce::Image background;     // the rendered hardware, at the display's pixel density
    float backgroundScale = 0.0f;
    int backgroundFinish = -1;
    juce::String lastSignature;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepbarEditor)
};
