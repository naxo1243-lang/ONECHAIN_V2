#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Shared look of the alien skin (palette, fonts and small drawing helpers).
namespace skin {

// The palette follows the active tuning algorithm: green (Original), blue (Precise), red (Waves).
inline float gHue = 0.39f;
inline juce::Colour tone(float s, float v, float a = 1.f) { return juce::Colour::fromHSV(gHue, s, v, a); }

inline juce::Colour kLcdBg, kLcdEdge, kLcdText, kLcdDim, kNeon, kDark, kRim,
                    kDomeHi, kDomeLo, kDomeBlack, kThumbHi, kThumbLo, kOffLed, kHiLine, kBadgeFill, kDimCurve;
inline const juce::Colour kWhite (0xfff1fff6);

inline void setTheme(int algo)
{
    static const float hue[3] = { 0.39f, 0.60f, 0.0f };      // green, blue, red
    gHue = hue[algo < 0 ? 0 : (algo > 2 ? 2 : algo)];
    kLcdBg = tone(.75f, .08f);   kLcdEdge = tone(.70f, .35f); kLcdText = tone(.51f, 1.f);  kLcdDim = tone(.75f, .23f);
    kNeon = tone(.64f, 1.f);     kDark = tone(.90f, .06f);    kRim = tone(.80f, .18f);
    kDomeHi = tone(.38f, .91f);  kDomeLo = tone(.80f, .29f);  kDomeBlack = tone(.88f, .03f);
    kThumbHi = tone(.33f, .91f); kThumbLo = tone(.67f, .40f); kOffLed = tone(.74f, .165f);
    kHiLine = tone(.20f, 1.f, .40f); kBadgeFill = tone(.81f, .125f); kDimCurve = tone(.20f, .54f);
}
inline const int kThemeInit = (setTheme(0), 0);

inline juce::Font monoFont(float h) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold)); }
inline juce::Font sansFont(float h) { return juce::Font(juce::FontOptions(h, juce::Font::bold)); }

inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    juce::Path p; p.addRoundedRectangle(r, 30.f);
    g.setColour(juce::Colour(0x66000000)); g.fillPath(p);
    g.setColour(kRim); g.strokePath(p, juce::PathStrokeType(4.f));
    juce::Path q; q.addRoundedRectangle(r.reduced(3.f), 28.f);
    g.setColour(kHiLine); g.strokePath(q, juce::PathStrokeType(1.2f));
    if (title.isNotEmpty()) {
        g.setFont(monoFont(15.f));
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawText(title, r.withHeight(26.f).translated(1.f, 7.f), juce::Justification::centred, false);
        g.setColour(kWhite);
        g.drawText(title, r.withHeight(26.f).translated(0.f, 6.f), juce::Justification::centred, false);
    }
}
inline void drawLcdBox(juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour(kLcdBg); g.fillRoundedRectangle(r, 10.f);
    g.setColour(kLcdEdge); g.drawRoundedRectangle(r.reduced(1.f), 10.f, 2.f);
    g.setColour(juce::Colour(0x22ffffff)); g.drawRoundedRectangle(r.reduced(3.f), 8.f, 1.f);
}
inline void drawName(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r,
                     juce::Justification j = juce::Justification::centredLeft, float size = 17.f)
{
    g.setFont(sansFont(size));
    g.setColour(juce::Colours::black.withAlpha(0.65f)); g.drawText(t, r.translated(1, 2), j, false);
    g.setColour(kWhite);                                g.drawText(t, r, j, false);
}
inline void styleSlider(juce::Slider& s)
{
    s.setScrollWheelEnabled(false);
}

} // namespace skin
