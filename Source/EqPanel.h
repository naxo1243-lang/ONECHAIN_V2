#pragma once
#include <functional>
#include <vector>
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Skin.h"
#include "Dsp/Fft.h"
#include "Dsp/LinearPhaseEq.h"

// Pro-Q style linear-phase EQ window:
//  * interactive curve: drag a node (freq / gain), mouse wheel = Q,
//    double-click empty space = add a band, double-click a node = remove it
//  * spectrum analyzer behind the curve (post-EQ)
//  * band selector + shape / slope / placement + Freq / Gain / Q knobs
class EqPanel : public juce::Component, private juce::Timer
{
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // ----------------------------------------------------------------- nested components
    class Display : public juce::Component {
    public:
        explicit Display(EqPanel& o) : owner(o) {}
        void paint(juce::Graphics& g) override { owner.paintDisplay(g); }
        void mouseDown(const juce::MouseEvent& e) override { owner.displayDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override { owner.displayDrag(e); }
        void mouseUp(const juce::MouseEvent& e) override { owner.displayUp(e); }
        void mouseDoubleClick(const juce::MouseEvent& e) override { owner.displayDouble(e); }
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { owner.displayWheel(e, w); }
    private:
        EqPanel& owner;
    };

    class Chips : public juce::Component {
    public:
        explicit Chips(EqPanel& o) : owner(o) {}
        void paint(juce::Graphics& g) override { owner.paintChips(g, getLocalBounds().toFloat()); }
        void mouseDown(const juce::MouseEvent& e) override { owner.chipsDown(e); }
        void mouseDoubleClick(const juce::MouseEvent& e) override { owner.chipsDouble(e); }
    private:
        EqPanel& owner;
    };

    class LcdButton : public juce::Button {
    public:
        LcdButton() : juce::Button("lcd") {}
        juce::String text;
        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat();
            skin::drawLcdBox(g, r);
            if (over || down) { g.setColour(skin::kNeon.withAlpha(down ? 0.9f : 0.5f)); g.drawRoundedRectangle(r.reduced(1.5f), 10.f, 2.f); }
            g.setFont(skin::monoFont(16.f)); g.setColour(skin::kLcdText);
            g.drawText(text, r, juce::Justification::centred, false);
        }
    };

public:
    std::function<void()> onClose;

    explicit EqPanel(OneChainProcessor& p) : proc(p), display(*this), chips(*this)
    {
        auto& ap = proc.apvts;
        eqOnRaw = ap.getRawParameterValue("eq_on");
        eqGainRaw = ap.getRawParameterValue("eq_gain");
        anaRaw = ap.getRawParameterValue("eq_analyzer");
        modeRaw = ap.getRawParameterValue("lat_mode");
        for (int b = 0; b < oc::kEqBands; ++b) {
            const juce::String n = juce::String("eq") + juce::String(b + 1) + "_";
            bp[b].on = ap.getParameter(n + "on");       raw[b].on = ap.getRawParameterValue(n + "on");
            bp[b].shape = ap.getParameter(n + "shape"); raw[b].shape = ap.getRawParameterValue(n + "shape");
            bp[b].freq = ap.getParameter(n + "freq");   raw[b].freq = ap.getRawParameterValue(n + "freq");
            bp[b].gain = ap.getParameter(n + "gain");   raw[b].gain = ap.getRawParameterValue(n + "gain");
            bp[b].q = ap.getParameter(n + "q");         raw[b].q = ap.getRawParameterValue(n + "q");
            bp[b].slope = ap.getParameter(n + "slope"); raw[b].slope = ap.getRawParameterValue(n + "slope");
            bp[b].place = ap.getParameter(n + "place"); raw[b].place = ap.getRawParameterValue(n + "place");
        }

        addAndMakeVisible(display);
        addAndMakeVisible(chips);

        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("eq1_shape"))) shapeBox.addItemList(cp->choices, 1);
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("eq1_slope"))) slopeBox.addItemList(cp->choices, 1);
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("eq1_place"))) placeBox.addItemList(cp->choices, 1);
        addAndMakeVisible(shapeBox); addAndMakeVisible(slopeBox); addAndMakeVisible(placeBox);
        shapeBox.onChange = [this] { updateEnablement(); display.repaint(); chips.repaint(); };

        setupKnob(freqK); setupKnob(gainK); setupKnob(qK); setupKnob(outK);
        outAtt = std::make_unique<SA>(ap, "eq_gain", outK);

        addAndMakeVisible(eqOnBtn);    eqOnAtt = std::make_unique<BA>(ap, "eq_on", eqOnBtn);
        addAndMakeVisible(anaBtn);     anaAtt = std::make_unique<BA>(ap, "eq_analyzer", anaBtn);
        addAndMakeVisible(bandOnBtn);

        addAndMakeVisible(closeBtn); closeBtn.text = "X";
        closeBtn.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(rangeBtn); rangeBtn.text = "12 dB";
        rangeBtn.onClick = [this] { cycleRange(); };

        fft.init(kFft);
        win.resize((size_t)kFft); fbuf.assign((size_t)kFft, oc::cpx(0.f, 0.f));
        samples.assign((size_t)kFft, 0.f); specDb.assign((size_t)(kFft / 2), -120.f);
        for (int i = 0; i < kFft; ++i) win[(size_t)i] = 0.5f - 0.5f * std::cos(2.f * juce::MathConstants<float>::pi * (float)i / (float)kFft);

        select(0);
    }

    ~EqPanel() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        skin::drawPanel(g, getLocalBounds().toFloat().reduced(2.f), {});
        skin::drawName(g, "On", { 52, 8, 40, 34 }, juce::Justification::centredLeft, 16.f);
        skin::drawName(g, modeRaw->load() > 1.5f ? "LINEAR-PHASE EQ" : "ZERO-LATENCY EQ", { 96, 8, 230, 34 }, juce::Justification::centred, 17.f);
        skin::drawName(g, "Spectrum", { 326, 8, 94, 34 }, juce::Justification::centredRight, 15.f);

        skin::drawName(g, "Shape", { 12, 488, 176, 22 }, juce::Justification::centredLeft, 14.f);
        skin::drawName(g, "Slope", { 198, 488, 176, 22 }, juce::Justification::centredLeft, 14.f);
        skin::drawName(g, "Placement", { 384, 488, 130, 22 }, juce::Justification::centredLeft, 14.f);
        skin::drawName(g, "Band", { 522, 488, 58, 22 }, juce::Justification::centred, 14.f);

        const int cell = (getWidth() - 24) / 4;
        static const char* const cap[4] = { "Freq", "Gain", "Q", "EQ Output" };
        for (int i = 0; i < 4; ++i)
            skin::drawName(g, juce::String(cap[i]), { 12 + cell * i, 566, cell, 22 }, juce::Justification::centred, 15.f);
    }

    void resized() override
    {
        const int w = getWidth();
        eqOnBtn.setBounds(14, 8, 34, 34);
        anaBtn.setBounds(424, 8, 34, 34);
        rangeBtn.setBounds(w - 120, 8, 60, 34);
        closeBtn.setBounds(w - 56, 8, 44, 34);
        display.setBounds(12, 52, w - 24, 380);
        chips.setBounds(12, 440, w - 24, 38);
        shapeBox.setBounds(12, 512, 176, 34);
        slopeBox.setBounds(198, 512, 176, 34);
        placeBox.setBounds(384, 512, 130, 34);
        bandOnBtn.setBounds(534, 512, 34, 34);
        const int cell = (w - 24) / 4;
        juce::Slider* ks[4] = { &freqK, &gainK, &qK, &outK };
        for (int i = 0; i < 4; ++i) ks[i]->setBounds(12 + cell * i + (cell - 100) / 2, 590, 100, 104);
    }

    void visibilityChanged() override
    {
        if (isVisible()) startTimerHz(30); else stopTimer();
    }

private:
    // ----------------------------------------------------------------- helpers
    struct BandPtr { juce::RangedAudioParameter *on = nullptr, *shape = nullptr, *freq = nullptr, *gain = nullptr, *q = nullptr, *slope = nullptr, *place = nullptr; };
    struct BandRaw { std::atomic<float> *on = nullptr, *shape = nullptr, *freq = nullptr, *gain = nullptr, *q = nullptr, *slope = nullptr, *place = nullptr; };

    static juce::Colour bandColour(int b)
    {
        static const juce::uint32 c[8] = { 0xff5dff8f, 0xff5de8ff, 0xffe8ff5a, 0xffff9a5a, 0xffff5ab4, 0xffb45aff, 0xff5a8aff, 0xffffffff };
        return juce::Colour(c[b & 7]);
    }
    static const char* shortShape(int s)
    {
        static const char* n[8] = { "BELL", "LSH", "HSH", "LCUT", "HCUT", "NTCH", "BP", "TILT" };
        return n[juce::jlimit(0, 7, s)];
    }
    static void gestureSet(juce::RangedAudioParameter* p, float v)
    {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
    }
    static void plainSet(juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost(p->convertTo0to1(v)); }

    bool bandOn(int b) const { return raw[b].on->load() > 0.5f; }
    int  bandShape(int b) const { return (int)(raw[b].shape->load() + 0.5f); }

    oc::EqParams readParams() const
    {
        oc::EqParams ep;
        ep.on = eqOnRaw->load() > 0.5f; ep.gainDb = eqGainRaw->load();
        for (int b = 0; b < oc::kEqBands; ++b) {
            oc::EqBand& d = ep.band[b];
            d.on = bandOn(b); d.shape = bandShape(b); d.freq = raw[b].freq->load(); d.gainDb = raw[b].gain->load();
            d.q = raw[b].q->load(); d.slope = (int)(raw[b].slope->load() + 0.5f); d.place = (int)(raw[b].place->load() + 0.5f);
        }
        return ep;
    }

    void setupKnob(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 92, 20);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        skin::styleSlider(s);
        addAndMakeVisible(s);
    }

    void select(int b)
    {
        sel = juce::jlimit(0, oc::kEqBands - 1, b);
        freqAtt.reset(); gainAtt.reset(); qAtt.reset(); shapeAtt.reset(); slopeAtt.reset(); placeAtt.reset(); onAtt.reset();
        auto& ap = proc.apvts;
        const juce::String n = juce::String("eq") + juce::String(sel + 1) + "_";
        freqAtt = std::make_unique<SA>(ap, n + "freq", freqK);
        gainAtt = std::make_unique<SA>(ap, n + "gain", gainK);
        qAtt = std::make_unique<SA>(ap, n + "q", qK);
        shapeAtt = std::make_unique<CA>(ap, n + "shape", shapeBox);
        slopeAtt = std::make_unique<CA>(ap, n + "slope", slopeBox);
        placeAtt = std::make_unique<CA>(ap, n + "place", placeBox);
        onAtt = std::make_unique<BA>(ap, n + "on", bandOnBtn);
        updateEnablement();
        display.repaint(); chips.repaint(); repaint();
    }

    void updateEnablement()
    {
        const int shape = bandShape(sel);
        const bool hasGain = oc::eqShapeHasGain(shape);
        const bool cut = (shape == oc::EqLowCut || shape == oc::EqHighCut);
        gainK.setEnabled(hasGain);  gainK.setAlpha(hasGain ? 1.f : 0.35f);
        slopeBox.setEnabled(cut);   slopeBox.setAlpha(cut ? 1.f : 0.35f);
    }

    void cycleRange()
    {
        static const float r[4] = { 6.f, 12.f, 24.f, 30.f };
        int i = 0; for (int k = 0; k < 4; ++k) if (std::fabs(r[k] - rangeDb) < 0.1f) i = k;
        rangeDb = r[(i + 1) % 4];
        rangeBtn.text = juce::String((int)rangeDb) + " dB";
        rangeBtn.repaint(); display.repaint();
    }

    // ----------------------------------------------------------------- geometry
    juce::Rectangle<float> plot() const { return display.getLocalBounds().toFloat().reduced(10.f, 12.f); }
    float freqToX(float f) const { auto r = plot(); return r.getX() + r.getWidth() * std::log(f / 20.f) / std::log(1000.f); }
    float xToFreq(float x) const { auto r = plot(); const float t = juce::jlimit(0.f, 1.f, (x - r.getX()) / r.getWidth()); return 20.f * std::pow(1000.f, t); }
    float dbToY(float d) const { auto r = plot(); return r.getCentreY() - d / rangeDb * r.getHeight() * 0.5f; }
    float yToDb(float y) const { auto r = plot(); return (r.getCentreY() - y) / (r.getHeight() * 0.5f) * rangeDb; }

    juce::Point<float> nodePos(int b) const
    {
        const float f = raw[b].freq->load();
        const float g = oc::eqShapeHasGain(bandShape(b)) ? raw[b].gain->load() : 0.f;
        auto r = plot();
        return { freqToX(f), juce::jlimit(r.getY() + 4.f, r.getBottom() - 4.f, dbToY(g)) };
    }
    int hitNode(juce::Point<float> p) const
    {
        for (int b = oc::kEqBands - 1; b >= 0; --b)
            if (bandOn(b) && nodePos(b).getDistanceFrom(p) <= 13.f) return b;
        return -1;
    }

    // ----------------------------------------------------------------- display painting
    float spectrumAt(float f) const
    {
        const float sr = (float)std::max(8000.0, proc.getSampleRate());
        const float binHz = sr / (float)kFft;
        const int nb = kFft / 2;
        const float lo = f * 0.9439f / binHz, hi = f * 1.0595f / binHz;
        const int i0 = std::max(1, (int)std::floor(lo)), i1 = std::min(nb - 2, (int)std::ceil(hi));
        if (i1 <= i0) {
            const float pos = f / binHz; const int k = juce::jlimit(1, nb - 2, (int)std::floor(pos)); const float fr = pos - (float)k;
            return specDb[(size_t)k] * (1.f - fr) + specDb[(size_t)(k + 1)] * fr;
        }
        float s = 0.f; for (int k = i0; k <= i1; ++k) s += specDb[(size_t)k];
        return s / (float)(i1 - i0 + 1);
    }

    void paintDisplay(juce::Graphics& g)
    {
        using namespace skin;
        auto full = display.getLocalBounds().toFloat();
        drawLcdBox(g, full);
        auto r = plot();
        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;

        g.saveState();
        g.reduceClipRegion(r.toNearestInt());

        // grid
        static const float gf[] = { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                    1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 };
        for (float f : gf) {
            const bool major = (f == 100.f || f == 1000.f || f == 10000.f);
            g.setColour(juce::Colour(major ? 0x33ffffff : 0x14ffffff));
            const float x = freqToX(f);
            g.drawLine(x, r.getY(), x, r.getBottom(), 1.f);
        }
        const float step = rangeDb <= 6.f ? 3.f : (rangeDb <= 12.f ? 6.f : (rangeDb <= 24.f ? 12.f : 10.f));
        for (float d = -std::floor(rangeDb / step) * step; d <= rangeDb + 0.01f; d += step) {
            const float y = dbToY(d);
            g.setColour(juce::Colour(std::fabs(d) < 0.01f ? 0x55ffffff : 0x1effffff));
            g.drawLine(r.getX(), y, r.getRight(), y, 1.f);
        }

        // spectrum analyzer
        if (anaRaw->load() > 0.5f) {
            juce::Path sp;
            sp.startNewSubPath(r.getX(), r.getBottom());
            for (float x = r.getX(); x <= r.getRight(); x += 2.f) {
                const float db = spectrumAt(xToFreq(x));
                const float norm = juce::jlimit(0.f, 1.f, (db + 90.f) / 80.f);
                sp.lineTo(x, r.getBottom() - norm * r.getHeight());
            }
            sp.lineTo(r.getRight(), r.getBottom()); sp.closeSubPath();
            g.setColour(kNeon.withAlpha(0.13f)); g.fillPath(sp);
            g.setColour(kNeon.withAlpha(0.30f)); g.strokePath(sp, juce::PathStrokeType(1.2f));
        }

        // band curves
        const oc::EqParams ep = readParams();
        oc::EqSecs secs[oc::kEqBands];
        for (int b = 0; b < oc::kEqBands; ++b) if (ep.band[b].on) secs[b] = oc::eqSections(ep.band[b], sr);

        juce::Path total, selPath;
        bool first = true, firstSel = true;
        const double PI = 3.14159265358979323846;
        for (float x = r.getX(); x <= r.getRight() + 0.5f; x += 2.f) {
            const double w = 2.0 * PI * (double)xToFreq(x) / sr;
            const double cw = std::cos(w), sw = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
            double tot = ep.on ? (double)ep.gainDb : 0.0, selDb = 0.0;
            for (int b = 0; b < oc::kEqBands; ++b) {
                if (!ep.band[b].on || !ep.on) { continue; }
                double m2 = 1.0;
                for (int q = 0; q < secs[b].n; ++q) m2 *= oc::eqSecMag2(secs[b].s[q], cw, sw, c2, s2);
                const double db = 10.0 * std::log10(m2 + 1e-30);
                tot += db; if (b == sel) selDb = db;
            }
            const float yt = juce::jlimit(r.getY() - 20.f, r.getBottom() + 20.f, dbToY((float)tot));
            if (first) { total.startNewSubPath(x, yt); first = false; } else total.lineTo(x, yt);
            if (bandOn(sel)) {
                const float ys = juce::jlimit(r.getY() - 20.f, r.getBottom() + 20.f, dbToY((float)selDb));
                if (firstSel) { selPath.startNewSubPath(x, dbToY(0.f)); selPath.lineTo(x, ys); firstSel = false; } else selPath.lineTo(x, ys);
            }
        }
        if (bandOn(sel) && !firstSel) {
            selPath.lineTo(r.getRight(), dbToY(0.f)); selPath.closeSubPath();
            g.setColour(bandColour(sel).withAlpha(0.20f)); g.fillPath(selPath);
        }
        const juce::Colour curveCol = ep.on ? kNeon : kDimCurve;
        g.setColour(curveCol.withAlpha(0.30f)); g.strokePath(total, juce::PathStrokeType(6.f));
        g.setColour(curveCol);                  g.strokePath(total, juce::PathStrokeType(2.4f));

        g.restoreState();

        // axis labels
        g.setFont(monoFont(11.f)); g.setColour(kLcdText.withAlpha(0.65f));
        static const float lf[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        static const char* const ln[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
        for (int i = 0; i < 8; ++i)
            g.drawText(ln[i], (int)freqToX(lf[i]) - 14, (int)(r.getBottom() - 15.f), 28, 14, juce::Justification::centred, false);
        for (float d = -std::floor(rangeDb / step) * step; d <= rangeDb + 0.01f; d += step) {
            const juce::String t = juce::String(d > 0.01f ? "+" : "") + juce::String((int)std::round(d));
            g.drawText(t, (int)r.getX() + 2, (int)dbToY(d) - 12, 30, 12, juce::Justification::centredLeft, false);
        }

        // nodes
        for (int b = 0; b < oc::kEqBands; ++b) {
            if (!bandOn(b)) continue;
            const auto p = nodePos(b);
            const float rad = (b == sel) ? 11.f : 9.f;
            g.setColour(juce::Colours::black.withAlpha(0.55f)); g.fillEllipse(p.x - rad, p.y - rad + 2.f, 2.f * rad, 2.f * rad);
            g.setColour(bandColour(b)); g.fillEllipse(p.x - rad, p.y - rad, 2.f * rad, 2.f * rad);
            g.setColour(b == sel ? kWhite : kRim); g.drawEllipse(p.x - rad, p.y - rad, 2.f * rad, 2.f * rad, b == sel ? 2.4f : 1.6f);
            g.setFont(monoFont(12.f)); g.setColour(kDark);
            g.drawText(juce::String(b + 1), (int)(p.x - rad), (int)(p.y - rad), (int)(2.f * rad), (int)(2.f * rad), juce::Justification::centred, false);
        }
        if (!ep.on) {
            g.setFont(monoFont(15.f)); g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.drawText("EQ OFF", full.reduced(10.f), juce::Justification::topRight, false);
        }
    }

    // ----------------------------------------------------------------- display mouse
    void displayDown(const juce::MouseEvent& e)
    {
        const int h = hitNode(e.position);
        if (h >= 0) {
            select(h);
            dragBand = h;
            bp[h].freq->beginChangeGesture(); bp[h].gain->beginChangeGesture();
        } else dragBand = -1;
    }
    void displayDrag(const juce::MouseEvent& e)
    {
        if (dragBand < 0) return;
        plainSet(bp[dragBand].freq, juce::jlimit(20.f, 20000.f, xToFreq(e.position.x)));
        if (oc::eqShapeHasGain(bandShape(dragBand)))
            plainSet(bp[dragBand].gain, juce::jlimit(-30.f, 30.f, yToDb(e.position.y)));
        display.repaint();
    }
    void displayUp(const juce::MouseEvent&)
    {
        if (dragBand >= 0) { bp[dragBand].freq->endChangeGesture(); bp[dragBand].gain->endChangeGesture(); dragBand = -1; }
    }
    void displayDouble(const juce::MouseEvent& e)
    {
        const int h = hitNode(e.position);
        if (h >= 0) { gestureSet(bp[h].on, 0.f); display.repaint(); chips.repaint(); return; }
        for (int b = 0; b < oc::kEqBands; ++b) {
            if (bandOn(b)) continue;
            gestureSet(bp[b].shape, 0.f);
            gestureSet(bp[b].freq, juce::jlimit(20.f, 20000.f, xToFreq(e.position.x)));
            gestureSet(bp[b].gain, juce::jlimit(-30.f, 30.f, yToDb(e.position.y)));
            gestureSet(bp[b].q, 1.f);
            gestureSet(bp[b].place, 0.f);
            gestureSet(bp[b].on, 1.f);
            select(b);
            break;
        }
    }
    void displayWheel(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        int b = hitNode(e.position);
        if (b < 0) b = bandOn(sel) ? sel : -1;
        if (b < 0) return;
        if (b != sel) select(b);
        const float q = juce::jlimit(0.1f, 40.f, raw[b].q->load() * std::exp(w.deltaY * 0.8f));
        gestureSet(bp[b].q, q);
        display.repaint();
    }

    // ----------------------------------------------------------------- band chips
    int chipAt(float x) const
    {
        const float gap = 6.f, cw = ((float)chips.getWidth() - gap * 7.f) / 8.f;
        const int i = (int)(x / (cw + gap));
        return (i >= 0 && i < oc::kEqBands) ? i : -1;
    }
    void paintChips(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        using namespace skin;
        const float gap = 6.f, cw = (bounds.getWidth() - gap * 7.f) / 8.f;
        for (int b = 0; b < oc::kEqBands; ++b) {
            juce::Rectangle<float> r((float)b * (cw + gap), 0.f, cw, bounds.getHeight());
            drawLcdBox(g, r);
            if (b == sel) { g.setColour(kNeon.withAlpha(0.9f)); g.drawRoundedRectangle(r.reduced(1.5f), 10.f, 2.f); }
            const bool on = bandOn(b);
            g.setColour(on ? bandColour(b) : kLcdDim);
            g.fillEllipse(r.getX() + 9.f, r.getCentreY() - 6.f, 12.f, 12.f);
            g.setFont(monoFont(16.f)); g.setColour(on ? kLcdText : kLcdText.withAlpha(0.35f));
            g.drawText(juce::String(b + 1), r.withTrimmedLeft(24.f).withTrimmedRight(cw * 0.45f), juce::Justification::centredLeft, false);
            if (on) {
                g.setFont(monoFont(10.f)); g.setColour(kLcdText.withAlpha(0.8f));
                g.drawText(shortShape(bandShape(b)), r.withTrimmedLeft(cw * 0.5f), juce::Justification::centred, false);
            }
        }
    }
    void chipsDown(const juce::MouseEvent& e) { const int i = chipAt(e.position.x); if (i >= 0) select(i); }
    void chipsDouble(const juce::MouseEvent& e)
    {
        const int i = chipAt(e.position.x);
        if (i < 0) return;
        gestureSet(bp[i].on, bandOn(i) ? 0.f : 1.f);
        select(i);
    }

    // ----------------------------------------------------------------- analyzer + timer
    void timerCallback() override
    {
        if (anaRaw->load() > 0.5f) {
            proc.readAnalyzer(samples.data(), kFft);
            for (int i = 0; i < kFft; ++i) fbuf[(size_t)i] = oc::cpx(samples[(size_t)i] * win[(size_t)i], 0.f);
            fft.forward(fbuf.data());
            const float binHz = (float)std::max(8000.0, proc.getSampleRate()) / (float)kFft;
            for (int k = 1; k < kFft / 2; ++k) {
                const float mag = std::abs(fbuf[(size_t)k]) / ((float)kFft * 0.25f);
                float db = 20.f * std::log10(mag + 1e-9f);
                const float f = (float)k * binHz;
                if (f > 20.f) db += 4.5f * std::log2(f / 1000.f);     // pink-noise style tilt
                float& s = specDb[(size_t)k];
                s = db > s ? db : s * 0.82f + db * 0.18f;
            }
        }
        const int md = (int)(modeRaw->load() + 0.5f);
        if (md != lastMode) { lastMode = md; repaint(96, 8, 230, 34); }
        display.repaint();
        chips.repaint();
    }

    // ----------------------------------------------------------------- members
    static constexpr int kFft = 4096;

    OneChainProcessor& proc;
    std::atomic<float> *eqOnRaw = nullptr, *eqGainRaw = nullptr, *anaRaw = nullptr, *modeRaw = nullptr;
    int lastMode = -1;
    BandPtr bp[oc::kEqBands];
    BandRaw raw[oc::kEqBands];

    float rangeDb = 12.f;
    int sel = 0, dragBand = -1;

    oc::Fft fft;
    std::vector<float> win, samples, specDb;
    std::vector<oc::cpx> fbuf;

    Display display;
    Chips chips;
    juce::ComboBox shapeBox, slopeBox, placeBox;
    juce::Slider freqK, gainK, qK, outK;
    juce::ToggleButton eqOnBtn, anaBtn, bandOnBtn;
    LcdButton closeBtn, rangeBtn;

    // attachments last: destroyed first
    std::unique_ptr<SA> freqAtt, gainAtt, qAtt, outAtt;
    std::unique_ptr<CA> shapeAtt, slopeAtt, placeAtt;
    std::unique_ptr<BA> onAtt, eqOnAtt, anaAtt;
};
