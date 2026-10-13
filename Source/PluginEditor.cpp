#include "PluginEditor.h"
#include "Presets.h"
#include "Skin.h"
#include "EqPanel.h"
#include <cmath>

using namespace skin;

namespace {

// ---------------------------------------------------------------- pixel alien (left half, mirrored)
const char* const kAlienHalf[21] = {
    "....######", "..########", ".#########", "##########", "##########", "##########",
    "##....####", "##......##", "###......#", "#####....#",
    "##########", "##########", "##########",
    "######.###", "#######...",
    ".#########", "..########", "...#######", "....######", ".....#####", "......####" };

void drawAlienFace(juce::Graphics& g, juce::Rectangle<float> area)
{
    const int cols = 20, rows = 21;
    const float px = std::floor(std::min(area.getWidth() / (float)cols, area.getHeight() / (float)rows));
    if (px < 1.f) return;
    const float ox = area.getCentreX() - px * (float)cols * 0.5f;
    const float oy = area.getCentreY() - px * (float)rows * 0.5f;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const char ch = (c < 10) ? kAlienHalf[r][c] : kAlienHalf[r][19 - c];
            if (ch == '#') {
                g.setColour(kNeon.withAlpha(0.28f));
                g.fillRect(ox + (float)c * px - 1.f, oy + (float)r * px - 1.f, px + 2.f, px + 2.f);
            }
        }
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const char ch = (c < 10) ? kAlienHalf[r][c] : kAlienHalf[r][19 - c];
            g.setColour(ch == '#' ? kNeon : kLcdDim.withAlpha(0.55f));
            g.fillRect(ox + (float)c * px + 0.6f, oy + (float)r * px + 0.6f, px - 1.2f, px - 1.2f);
        }
}

// ---------------------------------------------------------------- procedural "slime" texture
float hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / 16777215.f;
}
float vnoise(float x, float y)
{
    const int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    fx = fx * fx * (3.f - 2.f * fx); fy = fy * fy * (3.f - 2.f * fy);
    const float a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
juce::Image makeSlimeTexture(int w, int h)
{
    std::vector<float> H((size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float fx = (float)x, fy = (float)y;
            H[(size_t)y * (size_t)w + (size_t)x] = 0.50f * vnoise(fx / 70.f, fy / 70.f) + 0.28f * vnoise(fx / 28.f, fy / 28.f)
                                                 + 0.14f * vnoise(fx / 11.f, fy / 11.f) + 0.08f * vnoise(fx / 4.f, fy / 4.f);
        }
    juce::Image img(juce::Image::RGB, w, h, false);
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
    auto at = [&](int x, int y) { x = juce::jlimit(0, w - 1, x); y = juce::jlimit(0, h - 1, y); return H[(size_t)y * (size_t)w + (size_t)x]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float dx = at(x + 1, y) - at(x - 1, y), dy = at(x, y + 1) - at(x, y - 1);
            const float shade = juce::jlimit(0.30f, 1.25f, 0.64f + (-dx - dy) * 5.5f);
            const float t = juce::jlimit(0.f, 1.f, at(x, y) * 1.25f - 0.12f);
            const float spec = std::pow(std::max(0.f, shade - 0.98f) * 3.f, 2.f) * 0.5f;
            const float ex = std::fabs((float)x - (float)w * 0.5f) / ((float)w * 0.5f);
            const float ey = std::fabs((float)y - (float)h * 0.5f) / ((float)h * 0.5f);
            const float vig = 1.f - 0.20f * (ex * ex + ey * ey) * 0.5f;
            const float r = (0.09f + 0.16f * t) * shade * vig + spec;
            const float gg = (0.36f + 0.29f * t) * shade * vig + spec;
            const float b = (0.27f + 0.21f * t) * shade * vig + spec;
            bd.setPixelColour(x, y, juce::Colour::fromFloatRGBA(juce::jlimit(0.f, 1.f, r), juce::jlimit(0.f, 1.f, gg), juce::jlimit(0.f, 1.f, b), 1.f));
        }
    return img;
}

// ---------------------------------------------------------------- look and feel
class AlienLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AlienLookAndFeel()
    {
        setColour(juce::PopupMenu::backgroundColourId, kLcdBg);
        setColour(juce::PopupMenu::textColourId, kLcdText);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kLcdEdge);
        setColour(juce::PopupMenu::highlightedTextColourId, kWhite);
        setColour(juce::ComboBox::textColourId, kLcdText);
        setColour(juce::ComboBox::outlineColourId, kLcdEdge);
        setColour(juce::TooltipWindow::backgroundColourId, kLcdBg);
        setColour(juce::TooltipWindow::textColourId, kLcdText);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                          float startA, float endA, juce::Slider&) override
    {
        auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(3.f);
        const float d = std::min(b.getWidth(), b.getHeight());
        const auto c = b.getCentre();
        const float radius = d * 0.5f;
        const float angle = startA + pos * (endA - startA);
        const auto stroke = juce::PathStrokeType(4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track; track.addCentredArc(c.x, c.y, radius - 2.f, radius - 2.f, 0.f, startA, endA, true);
        g.setColour(kDark); g.strokePath(track, juce::PathStrokeType(7.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(kLcdDim); g.strokePath(track, stroke);

        juce::Path val; val.addCentredArc(c.x, c.y, radius - 2.f, radius - 2.f, 0.f, startA, angle, true);
        g.setColour(kNeon.withAlpha(0.30f)); g.strokePath(val, juce::PathStrokeType(9.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(kNeon); g.strokePath(val, stroke);

        const float r2 = radius - 9.f;
        g.setColour(juce::Colour(0xff021008)); g.fillEllipse(c.x - r2 - 2.f, c.y - r2 - 1.f, 2.f * r2 + 4.f, 2.f * r2 + 4.f);
        juce::ColourGradient grad(juce::Colour(0xff8fe8b4), c.x - r2 * 0.45f, c.y - r2 * 0.55f,
                                  juce::Colour(0xff0f4a30), c.x + r2 * 0.65f, c.y + r2 * 0.85f, true);
        g.setGradientFill(grad); g.fillEllipse(c.x - r2, c.y - r2, 2.f * r2, 2.f * r2);
        g.setColour(juce::Colour(0x55ffffff)); g.fillEllipse(c.x - r2 * 0.55f, c.y - r2 * 0.80f, r2 * 0.7f, r2 * 0.35f);

        juce::Path ptr; ptr.addRoundedRectangle(-2.f, -r2 + 3.f, 4.f, r2 * 0.5f, 2.f);
        g.setColour(kDark);
        g.fillPath(ptr, juce::AffineTransform::rotation(angle).translated(c.x, c.y));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float)y + (float)h * 0.5f;
        const float slotH = 22.f;
        auto slot = juce::Rectangle<float>((float)x + 4.f, cy - slotH * 0.5f, (float)w - 8.f, slotH);
        g.setColour(kDark); g.fillRoundedRectangle(slot, slotH * 0.5f);
        g.setColour(kLcdEdge); g.drawRoundedRectangle(slot, slotH * 0.5f, 1.8f);

        const int segs = 26;
        const float segW = (slot.getWidth() - 20.f) / (float)segs;
        for (int i = 0; i < segs; ++i) {
            const float x0 = slot.getX() + 10.f + (float)i * segW;
            const bool lit = (x0 + segW * 0.5f) <= pos;
            if (lit) { g.setColour(kNeon.withAlpha(0.25f)); g.fillRoundedRectangle(x0 - 0.5f, cy - 8.f, segW + 1.f, 16.f, 2.f); }
            g.setColour(lit ? kNeon : kLcdDim);
            g.fillRoundedRectangle(x0 + 1.f, cy - 6.f, segW - 2.f, 12.f, 1.5f);
        }
        const float tw = 36.f, th = 30.f;
        auto thumb = juce::Rectangle<float>(pos - tw * 0.5f, cy - th * 0.5f, tw, th);
        g.setColour(juce::Colour(0x88000000)); g.fillRoundedRectangle(thumb.translated(0.f, 2.5f), 11.f);
        juce::ColourGradient grad(juce::Colour(0xff9be8bb), thumb.getX(), thumb.getY(),
                                  juce::Colour(0xff22674a), thumb.getX(), thumb.getBottom(), false);
        g.setGradientFill(grad); g.fillRoundedRectangle(thumb, 11.f);
        g.setColour(kRim); g.drawRoundedRectangle(thumb, 11.f, 2.f);
        g.setColour(juce::Colour(0x66000000));
        for (int k = -1; k <= 1; ++k) g.drawLine(thumb.getCentreX() + (float)k * 6.f, thumb.getY() + 7.f, thumb.getCentreX() + (float)k * 6.f, thumb.getBottom() - 7.f, 1.6f);
        g.setColour(juce::Colour(0x55ffffff)); g.fillRoundedRectangle(thumb.reduced(5.f, 2.f).withHeight(5.f), 2.5f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1.f);
        const float dia = std::min(r.getWidth(), r.getHeight());
        const auto c = r.getCentre();
        g.setColour(kRim); g.fillEllipse(c.x - dia * 0.5f, c.y - dia * 0.5f, dia, dia);
        juce::ColourGradient rim(juce::Colour(0xff8fe8b4), c.x, c.y - dia * 0.5f, juce::Colour(0xff0f4a30), c.x, c.y + dia * 0.5f, false);
        g.setGradientFill(rim); g.fillEllipse(c.x - dia * 0.5f + 2.f, c.y - dia * 0.5f + 2.f, dia - 4.f, dia - 4.f);
        const float ri = dia * 0.5f - 6.f;
        g.setColour(kDark); g.fillEllipse(c.x - ri - 1.f, c.y - ri - 1.f, 2.f * ri + 2.f, 2.f * ri + 2.f);
        if (b.getToggleState()) {
            g.setColour(kNeon.withAlpha(0.30f)); g.fillEllipse(c.x - ri - 4.f, c.y - ri - 4.f, 2.f * ri + 8.f, 2.f * ri + 8.f);
            g.setColour(kNeon); g.fillEllipse(c.x - ri, c.y - ri, 2.f * ri, 2.f * ri);
            g.setColour(juce::Colour(0xaaffffff)); g.fillEllipse(c.x - ri * 0.55f, c.y - ri * 0.75f, ri * 0.7f, ri * 0.45f);
        } else {
            g.setColour(juce::Colour(0xff0b2a1c)); g.fillEllipse(c.x - ri, c.y - ri, 2.f * ri, 2.f * ri);
        }
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto r = juce::Rectangle<float>(0.f, 0.f, (float)w, (float)h);
        drawLcdBox(g, r);
        juce::Path p; const float ax = (float)w - 20.f, ay = (float)h * 0.5f;
        p.addTriangle(ax - 6.f, ay - 3.f, ax + 6.f, ay - 3.f, ax, ay + 5.f);
        g.setColour(kLcdText); g.fillPath(p);
    }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(10, 2, box.getWidth() - 34, box.getHeight() - 4);
        label.setFont(getComboBoxFont(box));
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return monoFont(17.f); }
    juce::Font getPopupMenuFont() override { return monoFont(16.f); }
    juce::Font getLabelFont(juce::Label& l) override
    {
        if (dynamic_cast<juce::Slider*>(l.getParentComponent()) != nullptr) return monoFont(15.f);
        return l.getFont();
    }
};

// ---------------------------------------------------------------- small components
struct MeterLeds : juce::Component {
    float level = 0.f;
    void setLevel(float l) { if (std::fabs(l - level) > 0.002f) { level = l; repaint(); } }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawLcdBox(g, r);
        auto in = r.reduced(8.f, 6.f);
        const int segs = 28; const float w = in.getWidth() / (float)segs;
        for (int i = 0; i < segs; ++i) {
            const float f = ((float)i + 0.5f) / (float)segs;
            const bool lit = f <= level;
            juce::Colour on = f < 0.72f ? kNeon : (f < 0.9f ? juce::Colour(0xffe8ff5a) : juce::Colour(0xffff5a4a));
            g.setColour(lit ? on : kLcdDim);
            g.fillRoundedRectangle(in.getX() + (float)i * w + 1.f, in.getY(), w - 2.f, in.getHeight(), 1.5f);
        }
    }
};

struct AlienButton : juce::Button {
    AlienButton() : juce::Button("adv") { setClickingTogglesState(true); }
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        drawLcdBox(g, r);
        if (getToggleState() || over) { g.setColour(kNeon.withAlpha(getToggleState() ? 0.9f : 0.4f)); g.drawRoundedRectangle(r.reduced(1.5f), 10.f, 2.f); }
        auto top = r.reduced(4.f);
        auto txt = top.removeFromBottom(11.f);
        drawAlienFace(g, top);
        g.setFont(monoFont(10.f)); g.setColour(kLcdText);
        g.drawText(getToggleState() ? "MAIN" : "ADV", txt, juce::Justification::centred, false);
    }
};

struct EqButton : juce::Button {
    std::atomic<float>* eqOn = nullptr;
    EqButton() : juce::Button("eq") { setClickingTogglesState(true); }
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        drawLcdBox(g, r);
        const bool lit = (eqOn != nullptr) && eqOn->load() > 0.5f;
        if (getToggleState() || over) { g.setColour(kNeon.withAlpha(getToggleState() ? 0.9f : 0.45f)); g.drawRoundedRectangle(r.reduced(1.5f), 10.f, 2.f); }
        juce::Path p; const float x0 = r.getX() + 12.f, w = 44.f, cy = r.getCentreY();
        p.startNewSubPath(x0, cy + 6.f);
        p.cubicTo(x0 + w * 0.25f, cy + 6.f, x0 + w * 0.35f, cy - 12.f, x0 + w * 0.5f, cy - 12.f);
        p.cubicTo(x0 + w * 0.65f, cy - 12.f, x0 + w * 0.75f, cy + 6.f, x0 + w, cy + 6.f);
        g.setColour(lit ? kNeon : kLcdDim.brighter(0.4f)); g.strokePath(p, juce::PathStrokeType(2.4f));
        g.setFont(monoFont(22.f)); g.setColour(lit ? kLcdText : kLcdText.withAlpha(0.45f));
        g.drawText("EQ", r.withTrimmedLeft(62.f), juce::Justification::centred, false);
    }
};

// ---------------------------------------------------------------- the UI
class OneChainUI : public juce::Component, private juce::Timer {
public:
    explicit OneChainUI(OneChainProcessor& p) : proc(p), eqPanel(p)
    {
        setLookAndFeel(&laf);
        setSize(626, 1024);
        bodyTex = makeSlimeTexture(626, 1024);

        // --- toggles (LEDs)
        led(pitchOn, "pitch_on"); led(optoOn, "opto_on"); led(optoLimit, "opto_limit");
        led(masterOn, "mast_on"); led(autoGain, "mast_autogain");

        // --- combos
        fillCombo(keyBox, "pitch_key"); fillCombo(scaleBox, "pitch_scale");
        for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem(proc.getProgramName(i), i + 1);
        presetBox.setSelectedItemIndex(proc.getCurrentProgram(), juce::dontSendNotification);
        presetBox.onChange = [this] { proc.setCurrentProgram(presetBox.getSelectedItemIndex()); };
        addAndMakeVisible(presetBox);

        // --- sliders
        linear(sSpeed, "pitch_speed"); linear(sAmount, "pitch_amount");
        knob(kPeak, "opto_peak"); knob(kGain, "opto_gain"); knob(kLink, "opto_link"); knob(kMix, "opto_mix");
        linear(sLow, "mast_low"); linear(sHigh, "mast_high"); linear(sDrive, "mast_drive");
        linear(sWidth, "mast_width"); linear(sGlue, "mast_glue"); linear(sCeil, "mast_ceiling");

        // --- advanced knobs
        static const char* const ids[22][2] = {
            {"mast_in","Input"},{"mast_lowcut","Low Cut"},{"mast_mid","Mid Bell"},{"mast_midfreq","Mid Freq"},
            {"mast_x1","Xover 1"},{"mast_x2","Xover 2"},
            {"mast_b1_comp","Low Comp"},{"mast_b1_sat","Low Sat"},{"mast_b1_gain","Low Level"},
            {"mast_b2_comp","Mid Comp"},{"mast_b2_sat","Mid Sat"},{"mast_b2_gain","Mid Level"},
            {"mast_b3_comp","High Comp"},{"mast_b3_sat","High Sat"},{"mast_b3_gain","High Level"},
            {"mast_glue_thr","Glue Thresh"},{"mast_glue_ratio","Glue Ratio"},{"mast_glue_atk","Glue Attack"},
            {"mast_glue_rel","Glue Release"},{"mast_drive_mix","Drive Mix"},{"mast_bassmono","Bass Mono"},{"mast_out","Output"} };
        for (auto& e : ids) {
            auto c = std::make_unique<AdvCtl>();
            knob(c->s, e[0]);
            c->name.setText(e[1], juce::dontSendNotification);
            c->name.setFont(sansFont(12.f));
            c->name.setJustificationType(juce::Justification::centred);
            c->name.setColour(juce::Label::textColourId, kWhite);
            c->name.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(c->name);
            adv.push_back(std::move(c));
        }

        addAndMakeVisible(meter);
        addAndMakeVisible(alienBtn);
        alienBtn.onClick = [this] { setAdvanced(alienBtn.getToggleState()); };
        addAndMakeVisible(eqButton);
        eqButton.eqOn = proc.apvts.getRawParameterValue("eq_on");
        eqButton.onClick = [this] { setEqOpen(eqButton.getToggleState()); };
        addChildComponent(eqPanel);
        eqPanel.onClose = [this] { eqButton.setToggleState(false, juce::dontSendNotification); setEqOpen(false); };
        setAdvanced(false);
        startTimerHz(30);
    }

    ~OneChainUI() override { stopTimer(); setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff070b16));
        juce::Random rnd(42);
        for (int i = 0; i < 90; ++i) {
            g.setColour(juce::Colours::white.withAlpha(0.15f + rnd.nextFloat() * 0.5f));
            const float s = 1.f + rnd.nextFloat() * 1.5f;
            g.fillEllipse(rnd.nextFloat() * 626.f, rnd.nextFloat() * 1024.f, s, s);
        }
        juce::Path body; body.addRoundedRectangle(8.f, 8.f, 610.f, 1008.f, 50.f);
        g.saveState(); g.reduceClipRegion(body); g.drawImageAt(bodyTex, 0, 0); g.restoreState();
        g.setColour(kRim); g.strokePath(body, juce::PathStrokeType(6.f));
        juce::Path hl; hl.addRoundedRectangle(12.f, 12.f, 602.f, 1000.f, 46.f);
        g.setColour(juce::Colour(0x66c8ffe0)); g.strokePath(hl, juce::PathStrokeType(1.6f));

        // --- header
        drawName(g, "Pitch On", { 36, 20, 120, 40 });
        drawName(g, "Key",   { 486, 62, 114, 20 }, juce::Justification::centred, 15.f);
        drawName(g, "Scale", { 486, 126, 114, 20 }, juce::Justification::centred, 15.f);

        // AUTO-TUNE badge
        {
            juce::Path tri; tri.startNewSubPath(90.f, 74.f); tri.lineTo(150.f, 176.f); tri.lineTo(30.f, 176.f); tri.closeSubPath();
            tri = tri.createPathWithRoundedCorners(16.f);
            g.setColour(juce::Colour(0xff062014)); g.fillPath(tri);
            g.setColour(kNeon.withAlpha(0.8f)); g.strokePath(tri, juce::PathStrokeType(2.f));
            drawAlienFace(g, juce::Rectangle<float>(62.f, 100.f, 56.f, 52.f));
            g.setFont(monoFont(11.f)); g.setColour(kLcdText);
            g.drawText("AUTO-TUNE", juce::Rectangle<float>(30.f, 152.f, 120.f, 18.f), juce::Justification::centred, false);
        }
        // title LCD
        drawLcdBox(g, { 176.f, 62.f, 296.f, 48.f });
        g.setFont(monoFont(26.f));
        g.setColour(kNeon.withAlpha(0.30f)); g.drawText("ONECHAIN VST", juce::Rectangle<int>(176, 62, 296, 48).translated(0, 1), juce::Justification::centred, false);
        g.setColour(kNeon);                  g.drawText("ONECHAIN VST", juce::Rectangle<int>(176, 62, 296, 48), juce::Justification::centred, false);
        // alien LCD
        drawLcdBox(g, { 228.f, 120.f, 170.f, 106.f });
        drawAlienFace(g, juce::Rectangle<float>(238.f, 126.f, 150.f, 94.f));

        if (!eqOpen) {
        // --- pitch panel
        drawPanel(g, { 20.f, 238.f, 586.f, 142.f }, "PITCH");
        drawName(g, "Retune Speed", { 38, 272, 150, 40 });
        drawName(g, "Pitch Amount", { 38, 322, 150, 40 });

        // --- opto panel
        drawPanel(g, { 20.f, 390.f, 586.f, 170.f }, "OPTO COMP");
        drawName(g, "On", { 78, 392, 60, 36 });
        drawName(g, "Limit Mode", { 506, 392, 110, 36 }, juce::Justification::centredLeft, 15.f);
        static const char* const on[4] = { "Peak Reduction", "Opto Gain", "Link", "Mix" };
        for (int i = 0; i < 4; ++i)
            drawName(g, on[i], { 20 + 146 * i + 0, 430, 146, 20 }, juce::Justification::centred, 14.f);

        // --- master panel
        drawPanel(g, { 20.f, 570.f, 586.f, 440.f }, advanced ? "MASTER  -  ADVANCED" : "MASTER");
        drawName(g, "Master On", { 78, 572, 130, 36 }, juce::Justification::centredLeft, 16.f);
        if (!advanced) {
            static const char* const mn[6] = { "Low Shelf (dB)", "High Shelf (dB)", "Drive (dB)", "Width (%)", "Glue (%)", "Ceiling (dB)" };
            for (int i = 0; i < 6; ++i) drawName(g, mn[i], { 38, 622 + i * 54, 150, 40 });
        } else {
            drawName(g, "Auto Gain", { 31 + 94 * 4 + 0, 606 + 86 * 3 + 14, 94, 20 }, juce::Justification::centred, 12.f);
        }
        }
        // bottom strip
        drawName(g, "OUT", { 232, 958, 40, 28 }, juce::Justification::centredLeft, 14.f);
    }

    void resized() override
    {
        pitchOn.setBounds(150, 20, 40, 40);
        keyBox.setBounds(486, 84, 114, 34);
        scaleBox.setBounds(486, 148, 114, 34);

        sSpeed.setBounds(190, 272, 400, 40);
        sAmount.setBounds(190, 322, 400, 40);

        optoOn.setBounds(40, 392, 36, 36);
        optoLimit.setBounds(466, 392, 36, 36);
        juce::Slider* ks[4] = { &kPeak, &kGain, &kLink, &kMix };
        for (int i = 0; i < 4; ++i) ks[i]->setBounds(20 + 146 * i + 23, 452, 100, 100);

        masterOn.setBounds(40, 572, 36, 36);
        juce::Slider* ms[6] = { &sLow, &sHigh, &sDrive, &sWidth, &sGlue, &sCeil };
        for (int i = 0; i < 6; ++i) ms[i]->setBounds(190, 622 + i * 54, 400, 40);

        for (size_t i = 0; i < adv.size(); ++i) {
            const int col = (int)i % 6, row = (int)i / 6;
            const int cx = 31 + col * 94, cy = 612 + row * 86;
            adv[i]->name.setBounds(cx, cy, 94, 14);
            adv[i]->s.setBounds(cx + 9, cy + 14, 76, 72);
        }
        autoGain.setBounds(31 + 94 * 4 + 29, 612 + 86 * 3 - 2, 36, 36);

        eqButton.setBounds(486, 16, 114, 40);
        eqPanel.setBounds(20, 236, 586, 700);
        presetBox.setBounds(36, 954, 184, 36);
        meter.setBounds(276, 956, 252, 32);
        alienBtn.setBounds(544, 946, 52, 52);
    }

private:
    struct AdvCtl { juce::Slider s; juce::Label name; };
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void style(juce::Slider& s)
    {
        s.setColour(juce::Slider::textBoxTextColourId, kLcdText);
        s.setColour(juce::Slider::textBoxBackgroundColourId, kLcdBg);
        s.setColour(juce::Slider::textBoxOutlineColourId, kLcdEdge);
        s.setColour(juce::Slider::textBoxHighlightColourId, kLcdEdge);
        s.setScrollWheelEnabled(false);
        addAndMakeVisible(s);
    }
    void linear(juce::Slider& s, const char* id)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 120, 34);
        style(s);
        sAtt.push_back(std::make_unique<SA>(proc.apvts, id, s));
    }
    void knob(juce::Slider& s, const char* id)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 20);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        style(s);
        sAtt.push_back(std::make_unique<SA>(proc.apvts, id, s));
    }
    void led(juce::ToggleButton& b, const char* id)
    {
        addAndMakeVisible(b);
        bAtt.push_back(std::make_unique<BA>(proc.apvts, id, b));
    }
    void fillCombo(juce::ComboBox& box, const char* id)
    {
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(id)))
            box.addItemList(cp->choices, 1);
        addAndMakeVisible(box);
        cAtt.push_back(std::make_unique<CA>(proc.apvts, id, box));
    }
    void setAdvanced(bool a)
    {
        advanced = a;
        updateVisibility();
        repaint();
    }
    void setEqOpen(bool open)
    {
        eqOpen = open;
        eqPanel.setVisible(open);
        updateVisibility();
        repaint();
    }
    void updateVisibility()
    {
        const bool mainPage = !eqOpen;
        sSpeed.setVisible(mainPage); sAmount.setVisible(mainPage);
        optoOn.setVisible(mainPage); optoLimit.setVisible(mainPage); masterOn.setVisible(mainPage);
        juce::Slider* knobsOpto[4] = { &kPeak, &kGain, &kLink, &kMix };
        for (auto* k : knobsOpto) k->setVisible(mainPage);
        juce::Slider* simple[6] = { &sLow, &sHigh, &sDrive, &sWidth, &sGlue, &sCeil };
        for (auto* s : simple) s->setVisible(mainPage && !advanced);
        for (auto& c : adv) { c->s.setVisible(mainPage && advanced); c->name.setVisible(mainPage && advanced); }
        autoGain.setVisible(mainPage && advanced);
    }
    void timerCallback() override
    {
        const float db = proc.meterOut.exchange(-100.f);
        const float lv = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);
        meterLevel = lv > meterLevel ? lv : juce::jmax(lv, meterLevel - 0.04f);
        meter.setLevel(meterLevel);
        eqButton.repaint();
    }

    OneChainProcessor& proc;
    AlienLookAndFeel laf;
    juce::Image bodyTex;
    bool advanced = false;
    bool eqOpen = false;
    float meterLevel = 0.f;

    juce::ToggleButton pitchOn, optoOn, optoLimit, masterOn, autoGain;
    juce::ComboBox keyBox, scaleBox, presetBox;
    juce::Slider sSpeed, sAmount, kPeak, kGain, kLink, kMix, sLow, sHigh, sDrive, sWidth, sGlue, sCeil;
    std::vector<std::unique_ptr<AdvCtl>> adv;
    MeterLeds meter;
    AlienButton alienBtn;
    EqButton eqButton;
    EqPanel eqPanel;

    // attachments last: destroyed first
    std::vector<std::unique_ptr<SA>> sAtt;
    std::vector<std::unique_ptr<BA>> bAtt;
    std::vector<std::unique_ptr<CA>> cAtt;
};

} // namespace

// ---------------------------------------------------------------- editor wrapper
OneChainEditor::OneChainEditor(OneChainProcessor& p) : juce::AudioProcessorEditor(p)
{
    ui = std::make_unique<OneChainUI>(p);
    addAndMakeVisible(*ui);
    setResizable(true, true);
    setResizeLimits(313, 512, 940, 1536);
    getConstrainer()->setFixedAspectRatio(626.0 / 1024.0);
    setSize(501, 819);
}

OneChainEditor::~OneChainEditor() = default;

void OneChainEditor::resized()
{
    if (ui == nullptr) return;
    ui->setBounds(0, 0, 626, 1024);
    ui->setTransform(juce::AffineTransform::scale((float)getWidth() / 626.f));
}
