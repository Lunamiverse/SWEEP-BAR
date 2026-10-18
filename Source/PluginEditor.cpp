#include "PluginEditor.h"

using namespace ui;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kKnobStart = -2.35619f, kKnobEnd = 2.35619f;   // -135 .. +135 degrees

const char* effectShort[]  = { "Delay", "Reverb", "Filter", "Stutter", "Pitch", "Crush" };
const char* effectAbbrev[] = { "DLY", "REV", "FLT", "STU", "PCH", "BIT" };

bool isMultiBar (int n) { return n == 8 || n == 16; }

juce::String stepLabel (int i, int n)
{
    return isMultiBar (n) ? juce::String (i / 4 + 1) + "." + juce::String (i % 4 + 1) : juce::String (i + 1);
}

float displayScale (const juce::Component* c)
{
    return juce::jlimit (1.0f, 3.0f, (float) juce::Component::getApproximateScaleFactorForComponent (c));
}

// Effect glyphs, designed on a 20 x 20 grid around (10, 10).
juce::Path effectGlyph (int fx)
{
    juce::Path p;
    switch (fx)
    {
        case 0: p.addEllipse (7.4f, 7.4f, 5.2f, 5.2f); break;                                                  // delay: echo source
        case 1: p.startNewSubPath (6.6f, 13.6f); p.lineTo (10.0f, 6.4f); p.lineTo (13.4f, 13.6f);
                p.startNewSubPath (7.9f, 11.0f); p.lineTo (12.1f, 11.0f); break;                               // reverb: room
        case 2: p.startNewSubPath (5.4f, 8.0f); p.lineTo (9.6f, 8.0f); p.quadraticTo (12.0f, 8.0f, 14.6f, 13.6f); break; // filter slope
        case 3: for (float x : { 6.6f, 9.0f, 11.4f, 13.8f }) { const float hh = x == 9.0f ? 4.2f : (x == 13.8f ? 1.0f : 2.6f);
                    p.startNewSubPath (x, 10.0f - hh); p.lineTo (x, 10.0f + hh); } break;                    // stutter: repeats
        case 4: p.startNewSubPath (10.0f, 5.8f); p.lineTo (10.0f, 14.0f);
                p.startNewSubPath (6.9f, 11.0f); p.lineTo (10.0f, 14.1f); p.lineTo (13.1f, 11.0f); break;     // pitch: down
        default: p.startNewSubPath (5.8f, 13.8f); p.lineTo (8.2f, 13.8f); p.lineTo (8.2f, 11.4f); p.lineTo (10.6f, 11.4f);
                 p.lineTo (10.6f, 9.0f); p.lineTo (13.0f, 9.0f); p.lineTo (13.0f, 6.6f); p.lineTo (14.4f, 6.6f); break;   // crush: steps
    }
    return p;
}

juce::Path gearGlyph (juce::Point<float> c, float r)
{
    juce::Path p;
    const int teeth = 8;
    for (int k = 0; k < teeth * 2; ++k)
    {
        const float a0 = 2.0f * kPi * (float) k / (float) (teeth * 2), a1 = 2.0f * kPi * (float) (k + 1) / (float) (teeth * 2);
        const float rr = (k % 2 == 0) ? r : r * 0.74f;
        const juce::Point<float> p0 (c.x + rr * std::cos (a0), c.y + rr * std::sin (a0)), p1 (c.x + rr * std::cos (a1), c.y + rr * std::sin (a1));
        if (k == 0) p.startNewSubPath (p0); else p.lineTo (p0);
        p.lineTo (p1);
    }
    p.closeSubPath();
    p.addEllipse (c.x - r * 0.32f, c.y - r * 0.32f, r * 0.64f, r * 0.64f);
    p.setUsingNonZeroWinding (false);
    return p;
}

void drawChevron (juce::Graphics& g, juce::Point<float> c, juce::Colour col)
{
    juce::Path p;
    p.startNewSubPath (c.x - 3.5f, c.y - 1.8f);
    p.lineTo (c.x, c.y + 1.8f);
    p.lineTo (c.x + 3.5f, c.y - 1.8f);
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// key cap image placed over a cell
void drawCap (juce::Graphics& g, juce::Rectangle<float> r, float scale)
{
    const auto& img = skin::keyCap (juce::roundToInt (r.getWidth()), juce::roundToInt (r.getHeight()), scale);
    const float m = (float) skin::kCapMargin;
    g.drawImage (img, juce::Rectangle<float> (r.getX() - m, r.getY() - m, (float) juce::roundToInt (r.getWidth()) + 2 * m,
                                              (float) juce::roundToInt (r.getHeight()) + 2 * m));
}

// Silk-screened legend with a hairline dark undercut so it reads printed, not floating.
void legend (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Font f, juce::Colour c)
{
    g.setFont (f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText (t, r.translated (0.0f, 1.0f), j, false);
    g.setColour (c);
    g.drawText (t, r, j, false);
}
} // namespace

juce::Colour ui::colours::effect (int index) { return skin::accentA (index); }

//==============================================================================
ChoiceButtons::ChoiceButtons (juce::RangedAudioParameter& param, juce::StringArray l, juce::Colour acc)
    : labels (std::move (l)), accent (acc),
      attachment (param, [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ChoiceButtons::cell (int i) const
{
    const auto area = getLocalBounds().toFloat().reduced (4.0f);
    const float w = area.getWidth() / (float) labels.size();
    return { area.getX() + (float) i * w, area.getY(), w, area.getHeight() };
}

int ChoiceButtons::indexAt (juce::Point<float> pt) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (pt)) return i;
    return -1;
}

void ChoiceButtons::paint (juce::Graphics& g)
{
    // the slot is part of the card's rendered relief; this draws the segments in it
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = cell (i).reduced (1.5f, 0.0f);
        const bool sel = i == selected;
        if (sel)
            skin::lens (g, r, 4.0f, accent, 0.95f);
        else if (i == hover)
        {
            g.setColour (juce::Colours::white.withAlpha (0.045f));
            g.fillRoundedRectangle (r, 4.0f);
        }
        if (i > 0 && ! sel && i - 1 != selected)
        {
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawVerticalLine ((int) cell (i).getX(), r.getY() + 7.0f, r.getBottom() - 7.0f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawVerticalLine ((int) cell (i).getX() + 1, r.getY() + 7.0f, r.getBottom() - 7.0f);
        }
        g.setColour (sel ? skin::colours::ink : (i == hover ? colours::text : colours::muted));
        g.setFont (skin::font (14.0f, sel ? skin::Weight::Bold : skin::Weight::Medium));
        g.drawFittedText (labels[i], r.toNearestInt(), juce::Justification::centred, 1);
    }
}

void ChoiceButtons::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i >= 0 && i != selected) attachment.setValueAsCompleteGesture ((float) i);
}

void ChoiceButtons::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i != hover) { hover = i; repaint(); }
}

void ChoiceButtons::mouseExit (const juce::MouseEvent&) { hover = -1; repaint(); }

//==============================================================================
EffectTiles::EffectTiles (SweepbarProcessor& p)
    : proc (p),
      attachment (*p.apvts.getParameter ("effect"), [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> EffectTiles::tileBounds (int i) const
{
    const float pad = 7.0f, gap = 8.0f;
    const float w = ((float) getWidth() - 2.0f * pad - gap * 5.0f) / 6.0f;
    return { pad + (float) i * (w + gap), pad, w, (float) getHeight() - 2.0f * pad };
}

juce::Rectangle<float> EffectTiles::gearBounds (int i) const
{
    const auto t = tileBounds (i);
    return { t.getRight() - 36.0f, t.getCentreY() - 14.0f, 28.0f, 28.0f };
}

void EffectTiles::paint (juce::Graphics& g)
{
    for (int i = 0; i < sweep::kNumEffects; ++i)
    {
        const auto t = tileBounds (i);
        const bool sel = i == selected;
        const auto c = skin::accentA (i);
        const auto top = t.reduced (2.5f);

        // light pipe along the lower edge of the switch: the switch's illumination
        const juce::Rectangle<float> pipe (top.getX() + 14.0f, top.getBottom() - 5.0f, top.getWidth() - 62.0f, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (pipe.expanded (1.0f, 0.8f), 1.5f);
        if (sel)
        {
            // light spilling up into the translucent cap
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.16f), 0.0f, top.getBottom(), c.withAlpha (0.0f), 0.0f, top.getY() + top.getHeight() * 0.25f, false));
            g.fillRoundedRectangle (top, 5.0f);
            juce::Path pp;
            pp.addRoundedRectangle (pipe, 1.0f);
            juce::DropShadow (c.withAlpha (0.85f), 7, {}).drawForPath (g, pp);
            g.setColour (c.interpolatedWith (juce::Colours::white, 0.25f));
            g.fillPath (pp);
        }
        else
        {
            g.setColour (c.withAlpha (i == hoverTile ? 0.32f : 0.16f));
            g.fillRoundedRectangle (pipe, 1.0f);
        }

        // glyph in a ring
        const juce::Point<float> ic (top.getX() + 21.0f, top.getCentreY() - 1.5f);
        juce::Path ring;
        ring.addEllipse (ic.x - 10.5f, ic.y - 10.5f, 21.0f, 21.0f);
        auto glyph = effectGlyph (i);
        glyph.applyTransform (juce::AffineTransform::translation (ic.x - 10.0f, ic.y - 10.0f));
        if (sel)
        {
            skin::glowStroke (g, ring, c, 1.4f, 5.0f, 0.5f);
            if (i == 0) { g.setColour (c); g.fillPath (glyph); }
            else skin::glowStroke (g, glyph, c, 1.5f, 4.0f, 0.4f);
        }
        else
        {
            const auto dimC = c.withAlpha (i == hoverTile ? 0.75f : 0.5f);
            g.setColour (dimC);
            g.strokePath (ring, juce::PathStrokeType (1.2f));
            if (i == 0) g.fillPath (glyph);
            else g.strokePath (glyph, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        auto nameArea = juce::Rectangle<float> (ic.x + 17.0f, top.getY(), top.getRight() - 40.0f - (ic.x + 17.0f), top.getHeight() - 4.0f);
        g.setColour (sel ? colours::text : (i == hoverTile ? colours::text.withAlpha (0.9f) : colours::muted));
        g.setFont (skin::font (15.0f, sel ? skin::Weight::SemiBold : skin::Weight::Medium));
        g.drawFittedText (SweepbarProcessor::effectNames[i], nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);

        // gear in its recessed hole
        const auto gr = gearBounds (i);
        auto gp = gearGlyph (gr.getCentre(), 7.0f);
        const bool lit = i == openSettings || i == hoverGear;
        if (lit) skin::glowStroke (g, gp, sel || i == openSettings ? c : colours::text, 0.0f, 4.0f, 0.35f);
        g.setColour (lit ? (sel || i == openSettings ? c : colours::text) : colours::dim.brighter (0.25f));
        g.fillPath (gp);
    }
}

void EffectTiles::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < sweep::kNumEffects; ++i)
    {
        if (gearBounds (i).expanded (3.0f).contains (e.position)) { if (onGear) onGear (i); return; }
        if (tileBounds (i).contains (e.position)) { if (i != selected) attachment.setValueAsCompleteGesture ((float) i); return; }
    }
}

void EffectTiles::mouseMove (const juce::MouseEvent& e)
{
    int t = -1, gr = -1;
    for (int i = 0; i < sweep::kNumEffects; ++i)
    {
        if (gearBounds (i).expanded (3.0f).contains (e.position)) gr = i;
        if (tileBounds (i).contains (e.position)) t = i;
    }
    if (t != hoverTile || gr != hoverGear) { hoverTile = t; hoverGear = gr; repaint(); }
}

void EffectTiles::mouseExit (const juce::MouseEvent&) { hoverTile = hoverGear = -1; repaint(); }

//==============================================================================
SideCell::SideCell (juce::String l, Icon i) : label (std::move (l)), icon (i) {}

juce::Rectangle<float> SideCell::windowBounds (juce::Rectangle<float> c)
{
    return { c.getX() + 10.0f, c.getBottom() - 40.0f, c.getWidth() - 20.0f, 28.0f };
}

void SideCell::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const bool clickable = buildMenu != nullptr;
    legend (g, label, b.withHeight (34.0f).withTrimmedTop (12.0f), juce::Justification::centred, skin::label (12.5f), colours::text);

    // printed line icon
    const auto c = juce::Point<float> (b.getCentreX(), b.getY() + 60.0f);
    g.setColour (hover && clickable ? colours::text : colours::muted);
    juce::Path p;
    const juce::PathStrokeType st (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    switch (icon)
    {
        case Icon::Pattern:
            for (int k = 0; k < 4; ++k)
            {
                const juce::Rectangle<float> r (c.x - 19.0f + (float) k * 10.0f, c.y - 8.0f, 7.0f, 16.0f);
                if (k == 1) g.fillRoundedRectangle (r, 1.5f); else g.drawRoundedRectangle (r, 1.5f, 1.3f);
            }
            break;
        case Icon::Shape:
            p.startNewSubPath (c.x - 22.0f, c.y + 9.0f);
            for (int k = 0; k <= 40; ++k)
            {
                const float t = (float) k / 40.0f;
                p.lineTo (c.x - 22.0f + 44.0f * t, c.y + 9.0f - 18.0f * std::pow (std::sin (kPi * t), 1.6f));
            }
            g.strokePath (p, st);
            break;
        case Icon::Length:
            p.startNewSubPath (c.x - 18.0f, c.y - 7.0f); p.lineTo (c.x - 18.0f, c.y + 7.0f);
            p.startNewSubPath (c.x + 18.0f, c.y - 7.0f); p.lineTo (c.x + 18.0f, c.y + 7.0f);
            p.startNewSubPath (c.x - 18.0f, c.y); p.lineTo (c.x + 18.0f, c.y);
            g.strokePath (p, st);
            g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
            break;
        case Icon::Steps:
            for (int k = 0; k < 4; ++k)
            {
                const juce::Rectangle<float> r (c.x - 12.0f + (float) (k % 2) * 14.0f, c.y - 12.0f + (float) (k / 2) * 14.0f, 10.0f, 10.0f);
                g.drawRoundedRectangle (r, 2.0f, 1.3f);
            }
            break;
        case Icon::Pencil:
            p.startNewSubPath (c.x - 12.0f, c.y + 12.0f); p.lineTo (c.x - 10.5f, c.y + 6.0f); p.lineTo (c.x + 7.5f, c.y - 12.0f);
            p.lineTo (c.x + 12.0f, c.y - 7.5f); p.lineTo (c.x - 6.0f, c.y + 10.5f); p.closeSubPath();
            p.startNewSubPath (c.x + 4.0f, c.y - 8.5f); p.lineTo (c.x + 8.5f, c.y - 4.0f);
            g.strokePath (p, st);
            break;
        case Icon::Sync:
            g.drawEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f, 1.4f);
            g.fillEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
            break;
    }

    // value window (the recess is in the chassis): value + dropdown chevron
    const auto win = windowBounds (b);
    if (hover && clickable)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (win.reduced (1.0f), 4.0f);
    }
    const juce::String value = getValue ? getValue() : juce::String();
    auto textArea = win.reduced (8.0f, 0.0f);
    if (clickable)
    {
        drawChevron (g, { win.getRight() - 12.0f, win.getCentreY() }, colours::muted);
        textArea = textArea.withTrimmedRight (10.0f);
    }
    g.setColour (colours::text);
    g.setFont (skin::numeric (14.0f));
    g.drawFittedText (value, textArea.toNearestInt(), clickable ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.85f);
}

void SideCell::mouseDown (const juce::MouseEvent&)
{
    if (buildMenu == nullptr) return;
    auto menu = buildMenu();
    if (menuLook != nullptr) menu.setLookAndFeel (menuLook);
    juce::Component::SafePointer<SideCell> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe] (int r) { if (safe != nullptr && r != 0 && safe->onMenuResult) { safe->onMenuResult (r); safe->repaint(); } });
}

//==============================================================================
StepGrid::StepGrid (SweepbarProcessor& p, juce::LookAndFeel& lf) : proc (p), menuLook (lf)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> StepGrid::cell (int i, int n) const
{
    const bool multi = isMultiBar (n);
    const float gap = n == 16 ? 5.0f : (multi ? 7.0f : 12.0f);
    const float barGap = multi ? (n == 16 ? 10.0f : 16.0f) : 0.0f;
    const int bars = multi ? n / 4 : 1;
    const auto area = getLocalBounds().toFloat().reduced (9.0f, 8.0f);
    const float w = std::floor ((area.getWidth() - gap * (float) (n - 1) - barGap * (float) (bars - 1)) / (float) n);
    const float used = w * (float) n + gap * (float) (n - 1) + barGap * (float) (bars - 1);
    const float x0 = area.getX() + std::floor ((area.getWidth() - used) * 0.5f);
    return { x0 + (float) i * (w + gap) + (float) (multi ? i / 4 : 0) * barGap, area.getY(), w, area.getHeight() };
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto params = proc.currentParams();
    const int n = params.patternBeats;
    const bool compact = n == 16;
    const bool playing = proc.engine.uiPlaying.load();
    const int current = playing ? (int) std::floor (proc.engine.uiPosition.load()) : -1;
    const float scale = displayScale (this);

    for (int i = 0; i < n; ++i)
    {
        const auto r = cell (i, n);
        const bool on = params.steps[(size_t) i];
        const int fx = (int) params.stepEffect[(size_t) i];
        drawCap (g, r, scale);
        const auto face = r.reduced (3.5f);
        if (on) skin::lens (g, face, 3.5f, skin::accentA (fx), 1.0f);
        if (i == current)
        {
            g.setColour (colours::text.withAlpha (0.9f));
            g.drawRoundedRectangle (r.expanded (3.0f), 7.0f, 1.4f);
        }
        if (on && proc.curves.isDrawn (i))
        {
            g.setColour (skin::colours::ink.withAlpha (0.7f));
            g.fillEllipse (face.getRight() - 7.0f, face.getY() + 3.5f, 4.0f, 4.0f);
        }

        const juce::Colour main = on ? skin::colours::ink : colours::muted;
        auto ta = face.toNearestInt();
        auto top = ta.removeFromTop (ta.getHeight() / 2 + 3);
        g.setColour (main);
        g.setFont (skin::numeric (compact ? 13.5f : 17.0f, skin::Weight::Bold));
        g.drawFittedText (stepLabel (i, n), top.withTrimmedTop (compact ? 2 : 4), juce::Justification::centredBottom, 1);
        g.setFont (skin::label (compact ? 9.5f : 10.5f));
        g.setColour (on ? skin::colours::ink.withAlpha (0.72f) : colours::dim);
        const juce::String sub = on ? juce::String (compact ? effectAbbrev[fx] : juce::String (effectShort[fx]).toUpperCase())
                                    : (n == 6 && i >= 4 ? juce::String ("EXTRA") : juce::String());
        g.drawFittedText (sub, ta.withTrimmedTop (1), juce::Justification::centredTop, 1);
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto params = proc.currentParams();
    const int n = params.patternBeats;
    for (int i = 0; i < n; ++i)
    {
        if (! cell (i, n).expanded (2.5f).contains (e.position)) continue;
        if (e.mods.isPopupMenu()) { showMenu (i); return; }
        // Paint: off -> on with the selected effect; on with another effect -> repaint; same effect -> off.
        const int brush = proc.brushEffect();
        const bool on = params.steps[(size_t) i];
        const int fx = (int) params.stepEffect[(size_t) i];
        if (! on)             { proc.setStepEffect (i, brush); proc.setStepOn (i, true); }
        else if (fx != brush) { proc.setStepEffect (i, brush); }
        else                  { proc.setStepOn (i, false); }
        repaint();
        return;
    }
}

void StepGrid::showMenu (int step)
{
    const auto params = proc.currentParams();
    const bool on = params.steps[(size_t) step];
    const int fx = (int) params.stepEffect[(size_t) step];
    juce::PopupMenu menu;
    menu.setLookAndFeel (&menuLook);
    menu.addSectionHeader ("Beat " + stepLabel (step, params.patternBeats));
    for (int e = 0; e < sweep::kNumEffects; ++e)
    {
        juce::PopupMenu::Item item (SweepbarProcessor::effectNames[e]);
        item.setID (e + 1).setTicked (on && e == fx).setColour (skin::accentA (e));
        menu.addItem (item);
    }
    menu.addSeparator();
    menu.addItem (100, on ? "Turn beat off" : "Turn beat on");
    menu.addItem (101, "Reset drawn curve", proc.curves.isDrawn (step));
    juce::Component::SafePointer<StepGrid> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
        [safe, step, on] (int result)
        {
            if (safe == nullptr || result == 0) return;
            auto& p = safe->proc;
            if (result >= 1 && result <= sweep::kNumEffects) { p.setStepEffect (step, result - 1); if (! on) p.setStepOn (step, true); }
            else if (result == 100) p.setStepOn (step, ! on);
            else if (result == 101) p.resetCurve (step);
            safe->repaint();
        });
}

//==============================================================================
juce::Rectangle<float> CurveView::plotArea() const
{
    auto b = getLocalBounds().toFloat();
    return b.withHeight (b.getHeight() - 22.0f);
}

void CurveView::resized()
{
    const auto p = plotArea().toNearestInt();
    fillLayer  = juce::Image (juce::Image::ARGB, juce::jmax (1, p.getWidth()), juce::jmax (1, p.getHeight()), true);
    bloomWide  = juce::Image (juce::Image::ARGB, juce::jmax (1, p.getWidth() / 4), juce::jmax (1, p.getHeight() / 4), true);
    bloomTight = juce::Image (juce::Image::ARGB, juce::jmax (1, p.getWidth() / 2), juce::jmax (1, p.getHeight() / 2), true);
}

void CurveView::paint (juce::Graphics& g)
{
    const auto p = proc.currentParams();
    const int n = p.patternBeats;
    const auto plot = plotArea();
    const float W = plot.getWidth(), H = plot.getHeight();
    const float bw = W / (float) n;
    const float top = 26.0f, base = H - 12.0f;
    auto yFor = [&] (float v) { return base - v * (base - top); };

    // ---- instrument grid: 16ths, beats, bars, level lines. Visible, never competing.
    for (int i = 1; i < n * 4; ++i)
    {
        const float x = (float) i * bw * 0.25f;
        const bool beat = i % 4 == 0, bar = i % 16 == 0 && n > 6;
        const float a = bar ? 0.13f : (beat ? 0.065f : 0.022f);
        g.setColour (juce::Colours::white.withAlpha (a));
        g.drawLine (x, 0.0f, x, H, bar ? 1.2f : 1.0f);
    }
    for (int k = 1; k <= 4; ++k)
    {
        const float y = yFor ((float) k * 0.25f);
        g.setColour (juce::Colours::white.withAlpha (k == 4 ? 0.05f : 0.028f));
        const float dashes[] = { 2.0f, 4.0f };
        g.drawDashedLine (juce::Line<float> (0.0f, y, W, y), dashes, 2, 1.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (0.0f, base, W, base, 1.0f);

    // ---- playhead: a column of light over the playing beat
    if (proc.engine.uiPlaying.load())
    {
        const float x = (float) std::floor (proc.engine.uiPosition.load()) * bw;
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), 0.0f, 0.0f,
                                                 juce::Colours::white.withAlpha (0.055f), 0.0f, H, false));
        g.fillRect (juce::Rectangle<float> (x, 0.0f, bw, H));
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x, 0.0f, 1.0f, H));
    }

    // ---- curves, one per active beat
    struct Curve { juce::Path line; float x0, x1; juce::Colour a, b; };
    std::vector<Curve> curves;
    const int perBeat = juce::jmax (48, (int) (bw * 2.0f));
    for (int b = 0; b < n; ++b)
    {
        if (! p.steps[(size_t) b]) continue;
        Curve c;
        for (int k = 0; k <= perBeat; ++k)
        {
            const double t = (double) k / perBeat * 0.99999;
            const float x = ((float) b + (float) t) * bw;
            const float y = yFor (sweep::modulationAt (p, &proc.curves, b + t));
            if (k == 0) c.line.startNewSubPath (x, y); else c.line.lineTo (x, y);
        }
        c.x0 = (float) b * bw; c.x1 = c.x0 + bw;
        const int fx = (int) p.stepEffect[(size_t) b];
        c.a = skin::accentA (fx); c.b = skin::accentB (fx);
        curves.push_back (std::move (c));
    }
    // idle baseline for beats that are off
    for (int b = 0; b < n; ++b)
        if (! p.steps[(size_t) b])
        {
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.drawLine ((float) b * bw + 2.0f, base, (float) (b + 1) * bw - 2.0f, base, 1.0f);
        }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // 1) translucent spectral body: horizontal colour shift, fading into the floor
    fillLayer.clear (fillLayer.getBounds());
    {
        juce::Graphics fg (fillLayer);
        for (auto& c : curves)
        {
            juce::Path fill (c.line);
            fill.lineTo (c.x1, H);
            fill.lineTo (c.x0, H);
            fill.closeSubPath();
            fg.setGradientFill (juce::ColourGradient (c.a, c.x0, 0.0f, c.b, c.x1, 0.0f, false));
            fg.fillPath (fill);
        }
    }
    {
        juce::Image::BitmapData d (fillLayer, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < fillLayer.getHeight(); ++y)
        {
            const float t = (float) y / (float) fillLayer.getHeight();
            const float fade = 0.26f * std::pow (1.0f - t, 1.6f) + 0.035f;
            for (int x = 0; x < fillLayer.getWidth(); ++x)
            {
                auto* px = (juce::PixelARGB*) d.getPixelPointer (x, y);
                if (px->getAlpha() == 0) continue;
                px->multiplyAlpha (fade);
            }
        }
    }
    g.drawImageAt (fillLayer, 0, 0);

    // 2) bloom: a wide, soft pass and a tight pass, rendered at reduced resolution and blurred
    auto bloomPass = [&] (juce::Image& img, float scaleDown, float width, int radius, int passes, float opacity)
    {
        img.clear (img.getBounds());
        {
            juce::Graphics bg (img);
            bg.addTransform (juce::AffineTransform::scale (1.0f / scaleDown));
            for (auto& c : curves)
            {
                bg.setGradientFill (juce::ColourGradient (c.a, c.x0, 0.0f, c.b, c.x1, 0.0f, false));
                bg.strokePath (c.line, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        skin::blur (img, radius, passes);
        g.setOpacity (opacity);
        g.drawImage (img, plot);
        g.setOpacity (1.0f);
    };
    bloomPass (bloomWide, 4.0f, 9.0f, 3, 3, 0.62f);
    bloomPass (bloomTight, 2.0f, 4.0f, 2, 2, 0.75f);

    // 3) layered emissive contours: two faint echoes inside each curve, like spectral layers
    for (auto& c : curves)
        for (int k = 1; k <= 2; ++k)
        {
            const float sc = 1.0f - 0.17f * (float) k;
            auto echo = c.line;
            echo.applyTransform (juce::AffineTransform::translation (0.0f, -base).scaled (1.0f, sc).translated (0.0f, base));
            g.setGradientFill (juce::ColourGradient (c.a.withAlpha (0.30f / (float) k), c.x0, 0.0f, c.b.withAlpha (0.30f / (float) k), c.x1, 0.0f, false));
            g.strokePath (echo, juce::PathStrokeType (0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    // 4) crisp core + white-hot centre
    for (auto& c : curves)
    {
        g.setGradientFill (juce::ColourGradient (c.a.interpolatedWith (juce::Colours::white, 0.18f), c.x0, 0.0f,
                                                 c.b.interpolatedWith (juce::Colours::white, 0.18f), c.x1, 0.0f, false));
        g.strokePath (c.line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.78f));
        g.strokePath (c.line, juce::PathStrokeType (0.65f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // light pooling on the floor under the curve
        g.setGradientFill (juce::ColourGradient (c.a.withAlpha (0.22f), (c.x0 + c.x1) * 0.5f, base,
                                                 c.a.withAlpha (0.0f), c.x1, base, true));
        g.fillRect (juce::Rectangle<float> (c.x0, base - 6.0f, c.x1 - c.x0, 12.0f));
    }
    g.restoreState();

    g.setColour (colours::dim.brighter (0.2f));
    g.setFont (skin::font (12.5f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("DRAG TO DRAW   \xc2\xb7   RIGHT-DRAG TO ERASE")), plot.reduced (14.0f, 10.0f), juce::Justification::topRight, false);

    g.setFont (skin::numeric (11.5f, skin::Weight::Medium));
    for (int i = 0; i < n; ++i)
    {
        g.setColour (p.steps[(size_t) i] ? colours::muted : colours::dim);
        g.drawText (stepLabel (i, n), juce::Rectangle<float> ((float) i * bw + 5.0f, H + 4.0f, bw, 14.0f), juce::Justification::centredLeft, false);
    }
}

void CurveView::mouseDown (const juce::MouseEvent& e)
{
    erasing = e.mods.isPopupMenu();
    touched.fill (false);
    last = e.position;
    drawSegment (e.position, e.position);
}

void CurveView::mouseDrag (const juce::MouseEvent& e)
{
    drawSegment (last, e.position);
    last = e.position;
}

void CurveView::drawSegment (juce::Point<float> a, juce::Point<float> b)
{
    const auto p = proc.currentParams();
    const int n = p.patternBeats;
    const auto plot = plotArea();
    const float W = plot.getWidth(), H = plot.getHeight();
    const float top = 26.0f, base = H - 12.0f;
    auto toPos = [&] (juce::Point<float> pt) { return juce::jlimit (0.0, n - 1e-6, (double) pt.x / W * n); };
    auto toVal = [&] (juce::Point<float> pt)
    {
        if (erasing) return 0.0f;
        const float shown = juce::jlimit (0.0f, 1.0f, (base - pt.y) / (base - top));
        return juce::jlimit (0.0f, 1.0f, shown / juce::jmax (0.05f, p.depth));
    };
    double posA = toPos (a), posB = toPos (b);
    float valA = toVal (a), valB = toVal (b);
    if (posB < posA) { std::swap (posA, posB); std::swap (valA, valB); }

    const double step = 1.0 / (sweep::kCurvePoints - 1);
    for (int beat = (int) posA; beat <= (int) posB && beat < n; ++beat)
    {
        if (! touched[(size_t) beat])
        {
            touched[(size_t) beat] = true;
            if (! erasing && ! p.steps[(size_t) beat]) { proc.setStepEffect (beat, proc.brushEffect()); proc.setStepOn (beat, true); }
            proc.beginDrawing (beat);
        }
        for (int i = 0; i < sweep::kCurvePoints; ++i)
        {
            const double pos = beat + i * step;
            if (pos < posA - step * 0.5 || pos > posB + step * 0.5) continue;
            const double f = posB > posA ? juce::jlimit (0.0, 1.0, (pos - posA) / (posB - posA)) : 0.0;
            proc.curves.setPoint (beat, i, valA + (float) f * (valB - valA));
        }
    }
    repaint();
}

//==============================================================================
juce::Rectangle<float> RulerView::cell (int k) const
{
    const auto area = getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (31.0f).withTrimmedBottom (11.0f);
    const float groupGap = 14.0f, gap = 4.0f;
    const float w = std::floor ((area.getWidth() - 3.0f * groupGap - 12.0f * gap) / 16.0f);
    const float used = w * 16.0f + 3.0f * groupGap + 12.0f * gap;
    const float x0 = area.getX() + std::floor ((area.getWidth() - used) * 0.5f);
    const int bar = k / 4, pos = k % 4;
    return { x0 + (float) bar * (4.0f * w + 3.0f * gap + groupGap) + (float) pos * (w + gap), area.getY(), w, area.getHeight() };
}

void RulerView::paint (juce::Graphics& g)
{
    const auto p = proc.currentParams();
    const int n = p.patternBeats;
    const bool playing = proc.engine.uiPlaying.load();
    const double songPpq = juce::jmax (0.0, proc.engine.uiSongPpq.load());
    const long firstBeat = (long) std::floor (songPpq / 16.0) * 16;
    const int nowCell = playing ? (int) ((long) std::floor (songPpq) - firstBeat) : -1;
    const float scale = displayScale (this);

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto c0 = cell (bar * 4);
        legend (g, "BAR " + juce::String ((firstBeat + bar * 4) / 4 + 1), { c0.getX(), 9.0f, 80.0f, 14.0f },
                juce::Justification::centredLeft, skin::label (11.0f), colours::muted);
        if (bar > 0)
        {
            const float x = c0.getX() - 7.5f;
            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.drawVerticalLine ((int) x, 8.0f, (float) getHeight() - 8.0f);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawVerticalLine ((int) x + 1, 8.0f, (float) getHeight() - 8.0f);
        }
    }
    for (int k = 0; k < 16; ++k)
    {
        const long songBeat = firstBeat + k;
        const int pos = (int) (songBeat % n);
        const bool on = p.steps[(size_t) pos];
        const int fx = (int) p.stepEffect[(size_t) pos];
        const auto r = cell (k);
        drawCap (g, r, scale);
        const auto face = r.reduced (3.0f);
        if (on) skin::lens (g, face, 3.0f, skin::accentA (fx), 0.92f);
        if (pos == 0)
        {
            g.setColour (colours::text.withAlpha (0.55f));
            g.fillRect (r.getX() + 3.0f, r.getY() - 6.0f, r.getWidth() - 6.0f, 1.5f);
        }
        if (k == nowCell)
        {
            g.setColour (colours::text.withAlpha (0.9f));
            g.drawRoundedRectangle (r.expanded (2.5f), 6.0f, 1.3f);
        }
        auto ta = face.toNearestInt();
        auto topText = ta.removeFromTop (ta.getHeight() / 2 + 3);
        g.setColour (on ? skin::colours::ink : colours::dim.brighter (0.15f));
        g.setFont (skin::numeric (11.5f, skin::Weight::Bold));
        g.drawFittedText (stepLabel (pos, n), topText.withTrimmedTop (2), juce::Justification::centredBottom, 1);
        if (on)
        {
            g.setColour (skin::colours::ink.withAlpha (0.7f));
            g.setFont (skin::label (8.5f));
            g.drawFittedText (effectAbbrev[fx], ta.withTrimmedTop (1), juce::Justification::centredTop, 1);
        }
    }
}

//==============================================================================
Knob::Knob (SweepbarProcessor& p, const juce::String& id, const juce::String& cap, juce::Colour c, juce::LookAndFeel& lf)
    : param (p.apvts.getParameter (id)), caption (cap), colour (c), attachment (p.apvts, id, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (kKnobStart + 2.0f * kPi, kKnobEnd + 2.0f * kPi, true);
    slider.setLookAndFeel (&lf);
    slider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

Knob::~Knob() { slider.setLookAndFeel (nullptr); }

void Knob::setGeometry (juce::Point<float> c, float d)
{
    centre = c; D = d;
    const float R = d * 0.5f;
    setBounds (juce::Rectangle<float> (c.x - R * 1.95f, c.y - R * 1.95f, R * 3.9f, R * 3.9f + 30.0f).getSmallestIntegerContainer());
}

void Knob::resized()
{
    const auto c = centre - getPosition().toFloat();
    slider.setBounds (juce::Rectangle<float> (D * 1.2f, D * 1.2f).withCentre (c).toNearestInt());
}

void Knob::paint (juce::Graphics& g)
{
    const auto c = centre - getPosition().toFloat();
    const float R = D * 0.5f;
    const float v = (float) param->getValue();
    const float angle = kKnobStart + (kKnobEnd - kKnobStart) * v;

    // value ring: engraved channel on the plate with a luminous fill
    const float rr = R * 1.27f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, rr, rr, 0.0f, kKnobStart, kKnobEnd, true);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.strokePath (track, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.strokePath (track, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (0.0f, 1.0f));
    if (v > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, kKnobStart, angle, true);
        skin::glowStroke (g, arc, colour, 1.8f, 6.0f, 0.55f);
    }
    skin::knobPointer (g, c, D, angle);

    const float ly = c.y + R * 1.88f;
    legend (g, caption.toUpperCase(), { 0.0f, ly, (float) getWidth(), 15.0f }, juce::Justification::centred, skin::label (12.5f), colours::text);
    g.setColour (colours::muted);
    g.setFont (skin::numeric (12.5f, skin::Weight::Medium));
    g.drawText (param->getCurrentValueAsText(), juce::Rectangle<float> (0.0f, ly + 15.0f, (float) getWidth(), 15.0f), juce::Justification::centred, false);
}

//==============================================================================
void LevelMeter::tick()
{
    const float v = source.exchange (0.0f);
    const float db = v > 1.0e-6f ? 20.0f * std::log10 (v) : -100.0f;
    const float pos = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 51.0f);   // -48 dB .. +3 dB
    level = juce::jmax (pos, level - 0.03f);
    if (pos >= peak) { peak = pos; peakHold = 36; }
    else if (peakHold > 0) --peakHold;
    else peak = juce::jmax (0.0f, peak - 0.012f);
    if (std::abs (level - shownLevel) > 0.003f || std::abs (peak - shownPeak) > 0.003f)
    {
        shownLevel = level; shownPeak = peak;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    skin::ledMeter (g, getLocalBounds().toFloat().reduced (4.0f, 3.0f), 28, level, peak);
}

//==============================================================================
SettingsCard::SettingsCard (SweepbarProcessor& p, juce::LookAndFeel& lf) : proc (p), look (lf)
{
    setWantsKeyboardFocus (true);
}

void SettingsCard::open (int fx, juce::Rectangle<int> area)
{
    rows.clear();
    knobs.clear();
    current = fx;
    setBounds (area);

    const auto colour = skin::accentA (fx);
    auto shortName = [] (juce::RangedAudioParameter* prm)
    {
        auto s = prm->getName (64).fromFirstOccurrenceOf (" ", false, false);
        return s.substring (0, 1).toUpperCase() + s.substring (1);
    };
    std::vector<juce::String> knobIds, knobNames;
    for (const auto& id : SweepbarProcessor::effectSettingIds[(size_t) fx])
    {
        auto* param = proc.apvts.getParameter (id);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
            rows.push_back ({ shortName (param), std::make_unique<ChoiceButtons> (*param, choice->choices, colour) });
        else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
            rows.push_back ({ shortName (param), std::make_unique<ChoiceButtons> (*param, juce::StringArray { "Off", "On" }, colour) });
        else { knobIds.push_back (id); knobNames.push_back (shortName (param)); }
    }

    const int cardW = 600, rowH = 40, knobBlock = 150;
    const int cardH = 74 + (int) rows.size() * (rowH + 10) + (knobIds.empty() ? 0 : knobBlock) + 12;
    card = juce::Rectangle<int> (cardW, cardH).withCentre (getLocalBounds().getCentre());

    // ---- render the card as hardware: plate, row slots, knob mounts and bodies
    const float disp = displayScale (this);
    skin::Relief rel (cardW + 24, cardH + 24, juce::jmax (2.0f, disp * 1.34f), juce::Colour (0xff1d1e21), true);
    const juce::Point<float> o (12.0f, 12.0f);
    rel.plate ({ o.x, o.y, (float) cardW, (float) cardH }, 12.0f, 8.0f, 2.6f, skin::Relief::Plate, skin::Relief::Edge);
    rel.grooveLine ({ o.x + 22.0f, o.y + 58.0f }, { o.x + (float) cardW - 22.0f, o.y + 58.0f }, 1.2f, 1.0f);

    int y = 72;
    for (auto& r : rows)
    {
        const juce::Rectangle<int> rb (150, y, cardW - 150 - 26, rowH);
        rel.recess (rb.toFloat() + o, 7.0f, 3.5f, 1.8f, skin::Relief::Floor, skin::Relief::Edge);
        addAndMakeVisible (*r.buttons);
        r.buttons->setBounds (rb + card.getPosition());
        y += rowH + 10;
    }
    std::vector<juce::Point<float>> centres;
    if (! knobIds.empty())
    {
        const float kw = 136.0f, D = 52.0f;
        const float x0 = (float) cardW * 0.5f - (float) knobIds.size() * kw * 0.5f + kw * 0.5f;
        for (size_t k = 0; k < knobIds.size(); ++k)
        {
            const juce::Point<float> c (x0 + (float) k * kw, (float) y + 56.0f);
            rel.mount (c + o, D * 0.5f * 1.16f, 4.0f);
            rel.knobBody (c + o, D * 0.5f, D * 0.34f);
            centres.push_back (c);
            auto knob = std::make_unique<Knob> (proc, knobIds[k], knobNames[k], colour, look);
            addAndMakeVisible (*knob);
            knob->setGeometry (c + card.getPosition().toFloat(), D);
            knobs.push_back (std::move (knob));
        }
    }
    auto img = rel.render();
    cardImage = img.rescaled (juce::roundToInt ((float) (cardW + 24) * disp), juce::roundToInt ((float) (cardH + 24) * disp),
                              juce::Graphics::highResamplingQuality);
    {
        juce::Graphics cg (cardImage);
        cg.addTransform (juce::AffineTransform::scale (disp));
        for (auto c : centres) skin::knobScale (cg, c + o, 52.0f, false);
    }

    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    repaint();
}

void SettingsCard::close()
{
    if (current < 0) return;
    current = -1;
    setVisible (false);
    rows.clear();
    knobs.clear();
    if (onClose) onClose();
}

void SettingsCard::paint (juce::Graphics& g)
{
    if (current < 0) return;
    const auto colour = skin::accentA (current);
    g.setColour (juce::Colours::black.withAlpha (0.62f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);

    const auto c = card.toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (c, 12.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 26, { 0, 12 }).drawForPath (g, shape);
    g.drawImage (cardImage, c.expanded (12.0f));

    // title
    skin::led (g, { c.getX() + 30.0f, c.getY() + 31.0f }, 4.5f, colour, true);
    const juce::String name = SweepbarProcessor::effectNames[current].toUpperCase();
    const auto titleFont = skin::font (18.0f, skin::Weight::SemiBold).withExtraKerningFactor (0.10f);
    legend (g, name, { c.getX() + 46.0f, c.getY() + 18.0f, 320.0f, 26.0f }, juce::Justification::centredLeft, titleFont, colour);
    const float nameW = titleFont.getStringWidthFloat (name);
    legend (g, "SETTINGS", { c.getX() + 56.0f + nameW, c.getY() + 19.0f, 200.0f, 26.0f }, juce::Justification::centredLeft,
            skin::label (14.0f), colours::muted);

    const juce::Rectangle<float> x (c.getRight() - 50.0f, c.getY() + 16.0f, 30.0f, 30.0f);
    g.setColour (colours::muted);
    g.drawLine (x.getX() + 10, x.getY() + 10, x.getRight() - 10, x.getBottom() - 10, 1.6f);
    g.drawLine (x.getRight() - 10, x.getY() + 10, x.getX() + 10, x.getBottom() - 10, 1.6f);

    for (auto& r : rows)
        legend (g, r.label.toUpperCase(), { c.getX() + 28.0f, (float) r.buttons->getY(), 116.0f, (float) r.buttons->getHeight() },
                juce::Justification::centredLeft, skin::label (12.5f), colours::text);
}

void SettingsCard::mouseDown (const juce::MouseEvent& e)
{
    const auto c = card.toFloat();
    const juce::Rectangle<float> x (c.getRight() - 50.0f, c.getY() + 16.0f, 30.0f, 30.0f);
    if (! c.contains (e.position) || x.contains (e.position)) close();
}

bool SettingsCard::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

//==============================================================================
Look::Look()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff141518));
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::headerTextColourId, colours::muted);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff24262b));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff131417));
    g.setColour (juce::Colour (0xff2a2c31));
    g.drawRect (0, 0, w, h, 1);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    // The slot is machined into the chassis. Light the travel up to the cap, then draw the cap.
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (colours::accent.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> ((float) x + 2.0f, cy - 0.75f, juce::jmax (0.0f, pos - (float) x - 2.0f), 1.5f));
    const float scale = displayScale (&s);
    static std::map<int, juce::Image> caps;
    const int key = juce::roundToInt (scale * 100.0f);
    auto it = caps.find (key);
    if (it == caps.end()) it = caps.emplace (key, skin::faderCap (14, 26, scale)).first;
    const juce::Rectangle<float> cap (pos - 7.0f, cy - 13.0f, 14.0f, 26.0f);
    juce::Path sp;
    sp.addRoundedRectangle (cap.reduced (1.0f), 2.5f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.85f), 5, { 1, 4 }).drawForPath (g, sp);
    g.drawImage (it->second, cap);
}

//==============================================================================
SweepbarEditor::SweepbarEditor (SweepbarProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      tiles (p), stepGrid (p, look), curve (p), ruler (p),
      depthKnob (p, "depth", "Depth", skin::accentA (0), look),
      mixKnob (p, "mix", "Mix", skin::accentA (1), look),
      smoothKnob (p, "smooth", "Smooth", skin::accentA (2), look),
      inAttach (p.apvts, "inGain", inSlider), outAttach (p.apvts, "outGain", outSlider),
      inMeter (p.inLevel), outMeter (p.outLevel),
      settings (p, look)
{
    setOpaque (true);
    for (auto* c : std::initializer_list<juce::Component*> { &tiles, &patternCell, &shapeCell, &lengthCell, &stepsCell, &drawCell, &syncCell,
                                                             &stepGrid, &curve, &displayGlass, &ruler, &depthKnob, &mixKnob, &smoothKnob,
                                                             &inSlider, &outSlider, &inMeter, &outMeter, &transportGlass })
        addAndMakeVisible (c);
    addChildComponent (settings);

    for (auto* s : { &inSlider, &outSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setLookAndFeel (&look);
        s->setDoubleClickReturnValue (true, 0.0);
        s->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        s->onValueChange = [this] { repaint (0, 790, getWidth(), 50); };
    }

    tiles.onGear = [this] (int fx) { openSettings (fx); };
    settings.onClose = [this] { tiles.openSettings = -1; tiles.repaint(); };

    auto bindChoice = [this] (ui::SideCell& cell, const juce::String& id)
    {
        cell.menuLook = &look;
        cell.getValue = [this, id] { return proc.apvts.getParameter (id)->getCurrentValueAsText(); };
        cell.buildMenu = [this, id] { return choiceMenu (id); };
        cell.onMenuResult = [this, id] (int r) { setChoice (id, r - 1); };
    };
    bindChoice (patternCell, "pattern");
    bindChoice (shapeCell, "shape");
    bindChoice (lengthCell, "length");

    stepsCell.menuLook = &look;
    stepsCell.getValue = [this]
    {
        const auto p = proc.currentParams();
        int on = 0;
        for (int i = 0; i < p.patternBeats; ++i) on += p.steps[(size_t) i] ? 1 : 0;
        return juce::String (on) + " of " + juce::String (p.patternBeats);
    };
    stepsCell.buildMenu = [this]
    {
        const auto p = proc.currentParams();
        juce::PopupMenu m;
        m.addSectionHeader ("Steps");
        m.addItem (1, "All beats on");
        m.addItem (2, "Clear all beats");
        m.addItem (3, "Paint every beat with " + SweepbarProcessor::effectNames[proc.brushEffect()]);
        m.addItem (4, "Copy bar 1 to every bar", p.patternBeats == 8 || p.patternBeats == 16);
        return m;
    };
    stepsCell.onMenuResult = [this] (int r)
    {
        const auto p = proc.currentParams();
        for (int i = 0; i < p.patternBeats; ++i)
        {
            if (r == 1 && ! p.steps[(size_t) i]) { proc.setStepEffect (i, proc.brushEffect()); proc.setStepOn (i, true); }
            if (r == 2) proc.setStepOn (i, false);
            if (r == 3) proc.setStepEffect (i, proc.brushEffect());
            if (r == 4 && i >= 4) { proc.setStepOn (i, p.steps[(size_t) (i % 4)]); proc.setStepEffect (i, (int) p.stepEffect[(size_t) (i % 4)]); }
        }
    };

    drawCell.menuLook = &look;
    drawCell.getValue = [this]
    {
        int n = 0;
        for (int i = 0; i < sweep::kMaxSteps; ++i) n += proc.curves.isDrawn (i) ? 1 : 0;
        return n == 0 ? juce::String ("Presets") : juce::String (n) + " drawn";
    };
    drawCell.buildMenu = [this]
    {
        bool any = false;
        for (int i = 0; i < sweep::kMaxSteps; ++i) any = any || proc.curves.isDrawn (i);
        juce::PopupMenu m;
        m.addSectionHeader ("Drag on the curve to draw");
        m.addItem (1, "Reset all drawing", any);
        return m;
    };
    drawCell.onMenuResult = [this] (int r) { if (r == 1) proc.resetAllCurves(); };

    syncCell.getValue = [this]
    {
        return proc.engine.uiPlaying.load() ? juce::String (proc.engine.uiBpm.load(), 1) + " BPM" : juce::String ("Stopped");
    };

    setSize (1040, 840);
    startTimerHz (30);
}

SweepbarEditor::~SweepbarEditor()
{
    inSlider.setLookAndFeel (nullptr);
    outSlider.setLookAndFeel (nullptr);
}

int SweepbarEditor::finishIndex() const
{
    return juce::jlimit (0, (int) skin::finishes().size() - 1, (int) proc.apvts.state.getProperty ("finish", 0));
}

juce::PopupMenu SweepbarEditor::choiceMenu (const juce::String& id) const
{
    juce::PopupMenu m;
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
    {
        m.addSectionHeader (choice->getName (32));
        for (int i = 0; i < choice->choices.size(); ++i)
            m.addItem (i + 1, choice->choices[i], true, i == choice->getIndex());
    }
    return m;
}

void SweepbarEditor::setChoice (const juce::String& id, int index)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
}

void SweepbarEditor::openSettings (int fx)
{
    if (settings.effect() == fx) { settings.close(); return; }
    settings.open (fx, screenPlate);
    tiles.openSettings = fx;
    tiles.repaint();
}

void SweepbarEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! finishButton.contains (e.getPosition())) return;
    juce::PopupMenu m;
    m.setLookAndFeel (&look);
    m.addSectionHeader ("Finish");
    for (int i = 0; i < (int) skin::finishes().size(); ++i)
        m.addItem (i + 1, skin::finishes()[(size_t) i].name, true, i == finishIndex());
    juce::Component::SafePointer<SweepbarEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (finishButton)),
                     [safe] (int r)
                     {
                         if (safe == nullptr || r == 0) return;
                         safe->proc.apvts.state.setProperty ("finish", r - 1, nullptr);
                         safe->rebuildBackground();
                         safe->repaint();
                     });
}

void SweepbarEditor::resized()
{
    logoPlate     = { 20, 16, 52, 52 };
    transportWell = { 378, 18, 284, 48 };
    finishButton  = { 842, 22, 178, 40 };
    effectTray    = { 20, 84, 1000, 58 };
    for (int k = 0; k < 3; ++k)
    {
        sideCells[(size_t) k]     = { 20, 156 + k * 148, 104, 136 };
        sideCells[(size_t) k + 3] = { 916, 156 + k * 148, 104, 136 };
    }
    screenPlate = { 136, 156, 768, 432 };
    stepTray    = { 150, 170, 740, 76 };
    displayWell = { 150, 258, 740, 316 };
    deckPlate   = { 20, 602, 492, 178 };
    rulerPlate  = { 524, 602, 496, 178 };
    rulerTray   = { 538, 648, 468, 108 };

    tiles.setBounds (effectTray);
    ui::SideCell* cells[] = { &patternCell, &shapeCell, &lengthCell, &stepsCell, &drawCell, &syncCell };
    for (int k = 0; k < 6; ++k) cells[k]->setBounds (sideCells[(size_t) k]);
    stepGrid.setBounds (stepTray);
    curve.setBounds (displayWell.reduced (4));
    displayGlass.setBounds (displayWell.reduced (3));
    transportGlass.setBounds (transportWell.reduced (3));
    ruler.setBounds (rulerTray);

    depthKnob.setGeometry ({ 104.0f, 680.0f }, 62.0f);
    mixKnob.setGeometry ({ 266.0f, 676.0f }, 74.0f);
    smoothKnob.setGeometry ({ 428.0f, 680.0f }, 62.0f);

    inMeter.setBounds (88, 802, 172, 20);
    inSlider.setBounds (274, 798, 120, 28);
    outSlider.setBounds (646, 798, 120, 28);
    outMeter.setBounds (780, 802, 172, 20);

    rebuildBackground();
}

void SweepbarEditor::rebuildBackground()
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    backgroundFinish = finishIndex();
    backgroundScale = displayScale (this);
    const float disp = backgroundScale;

    // A shared cache, so reopening the window (or a second instance) is instant.
    static std::map<juce::String, juce::Image> cache;
    const juce::String key = juce::String (backgroundFinish) + "@" + juce::String (disp, 2) + "/" + juce::String (w) + "x" + juce::String (h);
    if (auto it = cache.find (key); it != cache.end()) { background = it->second; return; }

    skin::Relief rel (w, h, juce::jmax (2.0f, disp * 1.34f), skin::finishes()[(size_t) backgroundFinish].tint);   // supersampled
    using R = skin::Relief;

    // chassis edge and machined seams
    rel.plate ({ 0.0f, 0.0f, (float) w, (float) h }, 12.0f, 3.0f, 3.5f, R::Chassis, R::Edge);
    rel.grooveLine ({ 18.0f, 77.0f }, { (float) w - 18.0f, 77.0f }, 1.3f, 1.2f);
    rel.grooveLine ({ 18.0f, 789.0f }, { (float) w - 18.0f, 789.0f }, 1.3f, 1.2f);

    // header hardware
    rel.plate (logoPlate.toFloat(), 10.0f, 6.0f, 2.2f, R::Plate);
    rel.recess (transportWell.toFloat(), 10.0f, 9.0f, 2.8f, R::Glass);
    rel.plate (finishButton.toFloat(), 7.0f, 5.0f, 2.0f, R::Cap);

    // effect switches in their tray
    rel.recess (effectTray.toFloat(), 11.0f, 4.0f, 2.0f, R::Floor);
    for (int i = 0; i < sweep::kNumEffects; ++i)
    {
        const auto t = tiles.tileBounds (i) + effectTray.getPosition().toFloat();
        rel.plate (t, 6.0f, 6.5f, 2.2f, R::Cap);
        rel.recess (t.reduced (3.5f), 4.0f, 1.0f, 7.0f, R::Cap, R::Cap);   // shallow dish, like a real switch cap
        rel.recess (tiles.gearBounds (i) + effectTray.getPosition().toFloat(), 7.0f, 2.6f, 1.6f, R::Floor);
    }

    // side modules
    for (auto& c : sideCells)
    {
        rel.plate (c.toFloat(), 9.0f, 6.5f, 2.3f, R::Plate);
        rel.recess (ui::SideCell::windowBounds (c.toFloat()), 6.0f, 3.2f, 1.6f, R::Glass);
    }

    // main display housing: step tray + deep glass well
    rel.plate (screenPlate.toFloat(), 11.0f, 7.5f, 2.5f, R::Plate);
    rel.recess (stepTray.toFloat(), 8.0f, 4.5f, 2.0f, R::Floor);
    rel.recess (displayWell.toFloat(), 9.0f, 12.0f, 3.4f, R::Glass);

    // knob deck: mounts and machined bodies
    rel.plate (deckPlate.toFloat(), 11.0f, 7.5f, 2.5f, R::Plate);
    for (auto* k : { &depthKnob, &mixKnob, &smoothKnob })
    {
        const float R0 = k->diameter() * 0.5f;
        rel.mount (k->centreInParent(), R0 * 1.16f, 4.0f);
        rel.knobBody (k->centreInParent(), R0, k->diameter() * 0.34f);
    }

    // four-bar overview
    rel.plate (rulerPlate.toFloat(), 11.0f, 7.5f, 2.5f, R::Plate);
    rel.recess (rulerTray.toFloat(), 8.0f, 4.5f, 2.0f, R::Floor);

    // footer: meter windows and fader slots
    for (auto* m : { &inMeter, &outMeter })
        rel.recess (m->getBounds().toFloat(), 5.0f, 3.5f, 1.5f, R::Glass);
    for (auto* s : { &inSlider, &outSlider })
    {
        const auto sb = s->getBounds().toFloat();
        rel.recess ({ sb.getX() + 2.0f, sb.getCentreY() - 3.0f, sb.getWidth() - 4.0f, 6.0f }, 3.0f, 4.0f, 1.4f, R::Floor);
    }

    auto img = rel.render();
    background = img.rescaled (juce::roundToInt ((float) w * disp), juce::roundToInt ((float) h * disp), juce::Graphics::highResamplingQuality);

    // silk-screened legends that never change
    {
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale (disp));
        legend (g, "SWEEP BAR", { 88.0f, 18.0f, 240.0f, 28.0f }, juce::Justification::centredLeft,
                skin::font (24.0f, skin::Weight::SemiBold).withExtraKerningFactor (0.16f), colours::text);
        legend (g, "creative movement effect", { 89.0f, 45.0f, 240.0f, 18.0f }, juce::Justification::centredLeft,
                skin::font (13.5f).withExtraKerningFactor (0.05f), colours::muted);
        for (auto* k : { &depthKnob, &mixKnob, &smoothKnob })
            skin::knobScale (g, k->centreInParent(), k->diameter(), true);
        legend (g, "ACROSS 4 BARS", { (float) rulerPlate.getX() + 16.0f, (float) rulerPlate.getY() + 14.0f, 200.0f, 18.0f },
                juce::Justification::centredLeft, skin::label (13.0f), colours::text);
        legend (g, "INPUT", { 22.0f, 800.0f, 62.0f, 24.0f }, juce::Justification::centredLeft, skin::label (12.5f), colours::text);
        legend (g, "OUTPUT", { 956.0f, 800.0f, 64.0f, 24.0f }, juce::Justification::centredRight, skin::label (12.5f), colours::text);
        legend (g, "SWEEPBAR  " JucePlugin_VersionString, { 420.0f, 800.0f, 200.0f, 24.0f }, juce::Justification::centred,
                skin::label (11.5f), colours::dim.brighter (0.2f));
    }
    cache[key] = background;
}

juce::String SweepbarEditor::visualSignature() const
{
    const auto p = proc.currentParams();
    juce::String s;
    s << p.patternBeats << '|' << (int) p.shape << '|' << p.hitLength << '|' << juce::roundToInt (p.depth * 1000) << '|';
    for (int i = 0; i < sweep::kMaxSteps; ++i) s << (p.steps[(size_t) i] ? '1' : '0') << (int) p.stepEffect[(size_t) i];
    float sum = 0.0f;
    for (int b = 0; b < sweep::kMaxSteps; ++b)
    {
        s << (proc.curves.isDrawn (b) ? 'd' : '-');
        for (int i = 0; i < sweep::kCurvePoints; i += 3) sum += proc.curves.point (b, i) * (float) (i + 1 + b * 7);
    }
    s << juce::roundToInt (sum * 1000.0f) << '|';
    s << (proc.engine.uiPlaying.load() ? 'P' : 'S') << (int) std::floor (proc.engine.uiSongPpq.load());
    s << '|' << juce::roundToInt (proc.engine.uiBpm.load() * 10.0);
    return s;
}

void SweepbarEditor::paint (juce::Graphics& g)
{
    if (backgroundFinish != finishIndex() || std::abs (backgroundScale - displayScale (this)) > 0.01f)
        rebuildBackground();
    g.drawImage (background, getLocalBounds().toFloat());

    const bool playing = proc.engine.uiPlaying.load();

    // logo: a luminous movement curve
    {
        const auto lb = logoPlate.toFloat().reduced (11.0f, 14.0f);
        juce::Path wave;
        for (int k = 0; k <= 40; ++k)
        {
            const float t = (float) k / 40.0f;
            const float y = lb.getCentreY() - std::sin (t * kPi * 2.2f + 0.3f) * lb.getHeight() * 0.45f * (0.6f + 0.4f * std::sin (t * kPi));
            if (k == 0) wave.startNewSubPath (lb.getX() + t * lb.getWidth(), y); else wave.lineTo (lb.getX() + t * lb.getWidth(), y);
        }
        juce::Path thick;
        juce::PathStrokeType (3.0f).createStrokedPath (thick, wave);
        juce::DropShadow (skin::accentA (1).withAlpha (0.6f), 6, {}).drawForPath (g, thick);
        g.setGradientFill (juce::ColourGradient (skin::accentA (1), lb.getX(), 0.0f, skin::accentA (0), lb.getRight(), 0.0f, false));
        g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.strokePath (wave, juce::PathStrokeType (0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // transport display behind glass
    {
        const auto tw = transportWell.toFloat().reduced (4.0f);
        const auto left = tw.withWidth (tw.getWidth() * 0.5f);
        const auto right = tw.withTrimmedLeft (tw.getWidth() * 0.5f);
        g.setColour (colours::text);
        g.setFont (skin::numeric (17.0f, skin::Weight::SemiBold).withExtraKerningFactor (0.10f));
        g.drawText (juce::String (proc.engine.uiBpm.load(), 1) + " BPM", left, juce::Justification::centred, false);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawVerticalLine ((int) tw.getCentreX(), tw.getY() + 9.0f, tw.getBottom() - 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine ((int) tw.getCentreX() + 1, tw.getY() + 9.0f, tw.getBottom() - 9.0f);
        const auto stateCol = playing ? skin::accentA (3) : colours::dim.brighter (0.3f);
        skin::led (g, { right.getX() + 26.0f, right.getCentreY() }, 3.6f, stateCol, playing);
        g.setColour (stateCol);
        g.setFont (skin::label (15.0f).withExtraKerningFactor (0.22f));
        g.drawText (playing ? "PLAYING" : "STOPPED", right.withTrimmedLeft (38.0f), juce::Justification::centredLeft, false);
    }

    // finish selector
    {
        const auto fb = finishButton.toFloat().reduced (3.0f);
        g.setColour (colours::text);
        g.setFont (skin::font (15.0f, skin::Weight::Medium));
        g.drawText (skin::finishes()[(size_t) finishIndex()].name, fb.withTrimmedLeft (14.0f), juce::Justification::centredLeft, false);
        drawChevron (g, { fb.getRight() - 16.0f, fb.getCentreY() }, colours::muted);
    }

    // pattern note on the overview plate
    {
        static const char* notes[] = { "Loops every bar", "Drifts, realigns every 3 bars", "Spans 2 bars", "Spans 4 bars: a full phrase" };
        const int n = proc.currentParams().patternBeats;
        g.setColour (colours::muted);
        g.setFont (skin::font (13.0f));
        g.drawText (notes[n == 4 ? 0 : n == 6 ? 1 : n == 8 ? 2 : 3],
                    juce::Rectangle<float> ((float) rulerPlate.getX() + 200.0f, (float) rulerPlate.getY() + 14.0f, (float) rulerPlate.getWidth() - 216.0f, 18.0f),
                    juce::Justification::centredRight, false);
    }

    // gain readouts
    g.setFont (skin::numeric (12.5f, skin::Weight::Medium));
    g.setColour (colours::muted);
    g.drawText (proc.apvts.getParameter ("inGain")->getCurrentValueAsText(), 400, 800, 64, 24, juce::Justification::centredLeft);
    g.drawText (proc.apvts.getParameter ("outGain")->getCurrentValueAsText(), 578, 800, 64, 24, juce::Justification::centredRight);
}

void SweepbarEditor::timerCallback()
{
    inMeter.tick();
    outMeter.tick();

    const auto sig = visualSignature();
    if (sig == lastSignature) return;
    lastSignature = sig;
    repaint (transportWell);
    repaint (rulerPlate.withHeight (40));
    stepGrid.repaint();
    curve.repaint();
    ruler.repaint();
    for (auto* c : { &patternCell, &shapeCell, &lengthCell, &stepsCell, &drawCell, &syncCell }) c->repaint();
}
