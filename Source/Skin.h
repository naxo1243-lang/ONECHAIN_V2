#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Shared look of the alien skin (palette, fonts and small drawing helpers).
namespace skin {

inline const juce::Colour kLcdBg   (0xff06140d);
inline const juce::Colour kLcdEdge (0xff1c5a3a);
inline const juce::Colour kLcdText (0xff7dffa0);
inline const juce::Colour kLcdDim  (0xff0f3a24);
inline const juce::Colour kNeon    (0xff5dff8f);
inline const juce::Colour kDark    (0xff03100a);
inline const juce::Colour kWhite   (0xfff1fff6);
inline const juce::Colour kRim     (0xff0a2e20);

inline juce::Font monoFont(float h) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold)); }
inline juce::Font sansFont(float h) { return juce::Font(juce::FontOptions(h, juce::Font::bold)); }

inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    juce::Path p; p.addRoundedRectangle(r, 30.f);
    g.setColour(juce::Colour(0x66000000)); g.fillPath(p);
    g.setColour(kRim); g.strokePath(p, juce::PathStrokeType(4.f));
    juce::Path q; q.addRoundedRectangle(r.reduced(3.f), 28.f);
    g.setColour(juce::Colour(0x66c8ffe0)); g.strokePath(q, juce::PathStrokeType(1.2f));
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
    s.setColour(juce::Slider::textBoxTextColourId, kLcdText);
    s.setColour(juce::Slider::textBoxBackgroundColourId, kLcdBg);
    s.setColour(juce::Slider::textBoxOutlineColourId, kLcdEdge);
    s.setColour(juce::Slider::textBoxHighlightColourId, kLcdEdge);
    s.setScrollWheelEnabled(false);
}

} // namespace skin
