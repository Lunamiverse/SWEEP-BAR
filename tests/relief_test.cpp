// Renders a material test scene with skin::Relief and writes it to PNG.
#include "Skin.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? argv[1] : "relief.png";
    const int W = 520, H = 300;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    skin::Relief r (W, H, 2.0f, skin::finishes()[0].tint);
    r.plate ({ 16, 16, 488, 268 }, 9, 6, 2.2f);
    r.groove ({ 16, 16, 488, 268 }, 9, 1.0f, 0.5f);
    r.recess ({ 30, 30, 200, 120 }, 7, 10, 3.0f, skin::Relief::Glass);
    r.recess ({ 30, 170, 200, 54 }, 6, 4, 2.0f);
    for (int i = 0; i < 4; ++i) r.plate ({ 36.0f + (float) i * 48.0f, 176, 42, 42 }, 4.5f, 5, 1.8f, skin::Relief::Cap);
    r.mount ({ 340, 140 }, 44, 4);
    r.knobBody ({ 340, 140 }, 38, 22);
    r.mount ({ 450, 220 }, 30, 4);
    r.knobBody ({ 450, 220 }, 25, 16);
    auto img = r.render();
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    auto small = img.rescaled (W, H, juce::Graphics::highResamplingQuality);
    {
        juce::Graphics g (small);
        skin::knobScale (g, { 340, 140 }, 76, true);
        skin::knobPointer (g, { 340, 140 }, 76, 1.2f);
        skin::knobPointer (g, { 450, 220 }, 50, -0.8f);
        skin::lens (g, { 36.0f + 48.0f + 4, 180, 34, 34 }, 3.0f, skin::accentA (1), 1.0f);
        skin::lens (g, { 36.0f + 144.0f + 4, 180, 34, 34 }, 3.0f, skin::accentA (0), 1.0f);
        g.setColour (skin::colours::text);
        g.setFont (skin::label (13.0f));
        g.drawText ("DEPTH", 300, 250, 80, 18, juce::Justification::centred);
    }
    juce::File f (out);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (small, os);
    std::printf ("relief %dx%d @2x rendered in %.0f ms -> %s\n", W, H, t1 - t0, out.toRawUTF8());
    return 0;
}
