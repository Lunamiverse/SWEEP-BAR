// Skin: the rendered materials of the Sweepbar editor.
//
// Static hardware (chassis, plates, recessed wells, glass housings, key caps, knob bodies) is modelled
// as a height field and lit per pixel by `Relief`: surface normals from the geometry, a directional key
// light + rim light + sky ambient, multi-scale ambient occlusion, ray-marched soft cast shadows, and
// physically-motivated materials (anodized aluminium, polished chamfers, horizontally brushed grain,
// radially spun knob faces, dark glass, rubber). Dynamic parts (light, pointers, text, waveforms) are
// drawn on top every frame.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace skin
{
//==============================================================================
// Typography: embedded Barlow (SIL OFL) so every machine renders identically.
enum class Weight { Medium, SemiBold, Bold };
juce::Font font (float height, Weight = Weight::Medium, bool condensed = false);
juce::Font label (float height);                     // condensed semibold, tracked: panel legends
juce::Font numeric (float height, Weight = Weight::SemiBold);

namespace colours
{
    const juce::Colour text      { 0xffe6e7ea };   // silk-screen legend
    const juce::Colour textDim   { 0xff9a9ea8 };
    const juce::Colour textFaint { 0xff5d6069 };
    const juce::Colour ink       { 0xff0a0a0c };
}

struct Finish { const char* name; juce::Colour tint; };
const std::array<Finish, 5>& finishes();

// Effect palette: luminous pairs for spectral gradients (electric cyan, ultraviolet, molten coral, magenta...).
juce::Colour accentA (int effect);
juce::Colour accentB (int effect);

//==============================================================================
class Relief
{
public:
    enum Mat : uint8_t { Empty, Chassis, Plate, Edge, Floor, Glass, Rubber, Knurl, Spun, Cap };

    // Logical size in UI points; `scale` = internal pixels per point (supersampling / HiDPI).
    Relief (int width, int height, float scale, juce::Colour tint, bool transparent = false);

    // Raised rounded block. `height`, `bevel` in points. Top surface gets `top`, its chamfer `edge`.
    void plate (juce::Rectangle<float>, float radius, float height, float bevel, Mat top = Plate, Mat edge = Edge);
    // Recessed rounded pocket with inner wall `edge` and floor `floor`.
    void recess (juce::Rectangle<float>, float radius, float depth, float bevel, Mat floor = Floor, Mat edge = Edge);
    // Thin machined groove (seam) along a rectangle outline or a line.
    void groove (juce::Rectangle<float>, float radius, float width = 1.2f, float depth = 0.8f);
    void grooveLine (juce::Point<float> a, juce::Point<float> b, float width = 1.2f, float depth = 0.8f);

    // Circular mount (recessed ring) and a machined knob body standing in it.
    void mount (juce::Point<float> c, float radius, float depth);
    void knobBody (juce::Point<float> c, float radius, float height);

    juce::Image render();

private:
    float sdRect (float px, float py, juce::Rectangle<float> r, float radius) const;
    template <typename Fn> void forBox (juce::Rectangle<float> box, Fn&& fn);
    void setMat (int i, Mat m, float coverage);

    int W, H;
    float s;
    juce::Colour tint;
    bool transparent;
    std::vector<float> h;         // height, internal pixels
    std::vector<uint8_t> mat;
    std::vector<float> cover;     // alpha for transparent reliefs
    std::vector<uint8_t> spin;    // index into spinCentres for Spun / Knurl pixels (0 = none)
    std::vector<juce::Point<float>> spinCentres { {} };
};

//==============================================================================
// Light helpers
void blur (juce::Image&, int radius, int passes = 3);   // separable box blur ~ gaussian, ARGB in place
void glowStroke (juce::Graphics&, const juce::Path&, juce::Colour core, float width, float bloom = 6.0f, float bloomAlpha = 0.55f);
void led (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, bool on);
void lens (juce::Graphics&, juce::Rectangle<float>, float radius, juce::Colour, float strength);   // backlit key lens
void ledMeter (juce::Graphics&, juce::Rectangle<float>, int segments, float level01, float peak01);
void knobPointer (juce::Graphics&, juce::Point<float> centre, float diameter, float angle);
void knobScale (juce::Graphics&, juce::Point<float> centre, float diameter, bool numbers);   // engraved ticks + numerals
juce::Image faderCap (int width, int height, float scale);
// Raised key cap with its own contact + cast shadow. Image covers (w + 2*kCapMargin) x (h + 2*kCapMargin) points.
constexpr int kCapMargin = 7;
const juce::Image& keyCap (int width, int height, float scale);
juce::Image glassSheen (int width, int height, float radius);   // reflection over a display window
}
