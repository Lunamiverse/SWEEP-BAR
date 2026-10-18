// Renders a material test scene with skin::Relief (knob, wells, keys, logotype) and writes it to PNG.
#include "Skin.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? argv[1] : "relief.png";
    const int W = 560, H = 320;
    const auto tint = skin::finishes()[0].tint;
    const auto wm = skin::wordmark ({ 34.0f, 22.0f }, 21.0f);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    skin::Relief r (W, H, 2.0f, tint);
    r.plate ({ 0, 0, (float) W, (float) H }, 12, 3, 3.5f, skin::Relief::Chassis, skin::Relief::Edge);
    for (auto& letter : wm.letters) r.emboss (letter, 2.2f, 0.55f, skin::Relief::Plate, skin::Relief::Edge);
    juce::Path slit;
    slit.addRectangle (wm.slitX0, wm.slitY - wm.slitHeight * 0.5f, wm.slitX1 - wm.slitX0, wm.slitHeight);
    r.emboss (slit, -5.5f, 0.6f, skin::Relief::Floor, skin::Relief::Edge);

    r.plate ({ 16, 78, 528, 226 }, 10, 7, 2.4f, skin::Relief::Plate);
    r.recess ({ 30, 92, 220, 120 }, 8, 11, 3.2f, skin::Relief::Glass);
    r.recess ({ 30, 226, 220, 62 }, 7, 4.5f, 2.0f);
    r.mount ({ 370, 190 }, 43, 5);
    r.knobBody ({ 370, 190 }, 37, 25);
    r.mount ({ 486, 250 }, 30, 4);
    r.knobBody ({ 486, 250 }, 26, 18);
    auto img = r.render();
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    auto small = img.rescaled (W, H, juce::Graphics::highResamplingQuality);
    {
        juce::Graphics g (small);
        skin::knobScale (g, { 370, 190 }, 74, true);
        skin::knobRing (g, { 370, 190 }, 74, 0.62f, skin::accentA (0));
        skin::knobPointer (g, { 370, 190 }, 74, -2.35619f + 4.71238f * 0.62f, skin::accentA (0));
        skin::knobRing (g, { 486, 250 }, 52, 0.3f, skin::accentA (3));
        skin::knobPointer (g, { 486, 250 }, 52, -2.35619f + 4.71238f * 0.3f, skin::accentA (3));
        // light in the slit
        juce::Path bar;
        bar.startNewSubPath (wm.slitX0 + 2, wm.slitY);
        bar.lineTo (wm.slitX1 - 2, wm.slitY);
        g.setGradientFill (juce::ColourGradient (skin::accentA (3).withAlpha (0.0f), wm.slitX0, 0, skin::accentA (1), wm.bounds.getCentreX(), 0, false));
        g.strokePath (bar, juce::PathStrokeType (0.8f));
    }
    juce::File f (out);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (small, os);
    std::printf ("relief %dx%d @2x rendered in %.0f ms -> %s\n", W, H, t1 - t0, out.toRawUTF8());
    return 0;
}
