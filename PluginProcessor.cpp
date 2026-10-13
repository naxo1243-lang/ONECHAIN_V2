#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout OneChainProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto addF = [&](const char* id, const char* name, float lo, float hi, float def, int decimals, const char* suffix = "") {
        auto attr = AudioParameterFloatAttributes()
            .withStringFromValueFunction([decimals, s = String(suffix)](float v, int) { return String(v, decimals) + s; })
            .withValueFromStringFunction([](const String& t) { return t.getFloatValue(); });
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, name,
                    NormalisableRange<float>(lo, hi, 0.01f), def, attr));
    };
    auto addB = [&](const char* id, const char* name, bool def) {
        p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ id, 1 }, name, def));
    };

    // ---------------- Pitch (original engine) ----------------
    addB("pitch_on", "Pitch On", true);
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "pitch_key", 1 }, "Key",
        StringArray{ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "pitch_scale", 1 }, "Scale",
        StringArray{ "Chromatic","Major","Minor" }, 0));
    addF("pitch_speed", "Retune Speed", 1.f, 200.f, 20.f, 1, " ms");
    {
        auto attr = AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return String(roundToInt(v * 100.f)) + "%"; })
            .withValueFromStringFunction([](const String& t) { return t.getFloatValue() / 100.f; });
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "pitch_amount", 1 }, "Pitch Amount",
                    NormalisableRange<float>(0.f, 1.f, 0.001f), 1.f, attr));
    }

    // ---------------- Opto compressor ----------------
    addB("opto_on", "Opto On", true);
    addF("opto_peak", "Peak Reduction", 0.f, 100.f, 40.f, 1);
    addF("opto_gain", "Opto Gain", -12.f, 24.f, 0.f, 1, " dB");
    addB("opto_limit", "Limit Mode", false);
    addF("opto_link", "Opto Link", 0.f, 100.f, 100.f, 0, "%");
    addF("opto_mix", "Opto Mix", 0.f, 100.f, 100.f, 0, "%");

    // ---------------- Master: main ----------------
    addB("mast_on", "Master On", true);
    addF("mast_low", "Low Shelf", -6.f, 6.f, 0.f, 1, " dB");
    addF("mast_high", "High Shelf", -6.f, 6.f, 0.f, 1, " dB");
    addF("mast_drive", "Drive", 0.f, 12.f, 0.f, 1, " dB");
    addF("mast_width", "Width", 0.f, 200.f, 100.f, 0, "%");
    addF("mast_glue", "Glue", 0.f, 100.f, 50.f, 0, "%");          // glue compressor mix
    addF("mast_ceiling", "Ceiling", -6.f, 0.f, -0.3f, 1, " dB");

    // ---------------- Master: advanced ----------------
    addF("mast_in", "Input", -12.f, 12.f, 0.f, 1, " dB");
    addF("mast_lowcut", "Low Cut", 20.f, 200.f, 20.f, 0, " Hz");
    addF("mast_mid", "Mid Bell", -6.f, 6.f, 0.f, 1, " dB");
    addF("mast_midfreq", "Mid Freq", 200.f, 8000.f, 2500.f, 0, " Hz");
    addF("mast_x1", "Xover 1", 60.f, 400.f, 150.f, 0, " Hz");
    addF("mast_x2", "Xover 2", 1500.f, 8000.f, 3500.f, 0, " Hz");
    addF("mast_b1_comp", "Low Comp", 0.f, 100.f, 20.f, 0, "%");
    addF("mast_b1_sat",  "Low Sat",  0.f, 100.f, 15.f, 0, "%");
    addF("mast_b1_gain", "Low Level", -12.f, 12.f, 0.f, 1, " dB");
    addF("mast_b2_comp", "Mid Comp", 0.f, 100.f, 25.f, 0, "%");
    addF("mast_b2_sat",  "Mid Sat",  0.f, 100.f, 10.f, 0, "%");
    addF("mast_b2_gain", "Mid Level", -12.f, 12.f, 0.f, 1, " dB");
    addF("mast_b3_comp", "High Comp", 0.f, 100.f, 20.f, 0, "%");
    addF("mast_b3_sat",  "High Sat",  0.f, 100.f, 15.f, 0, "%");
    addF("mast_b3_gain", "High Level", -12.f, 12.f, 0.f, 1, " dB");
    addF("mast_glue_thr", "Glue Thresh", -30.f, 0.f, -12.f, 1, " dB");
    addF("mast_glue_ratio", "Glue Ratio", 1.5f, 10.f, 2.f, 1, ":1");
    addF("mast_glue_atk", "Glue Attack", 1.f, 100.f, 30.f, 0, " ms");
    addF("mast_glue_rel", "Glue Release", 50.f, 1000.f, 200.f, 0, " ms");
    addF("mast_drive_mix", "Drive Mix", 0.f, 100.f, 100.f, 0, "%");
    addF("mast_bassmono", "Bass Mono", 0.f, 300.f, 0.f, 0, " Hz");
    addF("mast_out", "Output", -12.f, 12.f, 0.f, 1, " dB");
    addB("mast_autogain", "Auto Gain", true);

    return { p.begin(), p.end() };
}

OneChainProcessor::OneChainProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createLayout())
{
}

bool OneChainProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void OneChainProcessor::prepareToPlay(double sr, int blockSize)
{
    pitch.prepare((float)sr);
    opto.prepare((float)sr);
    master.prepare((float)sr);
    dupR.assign((size_t)blockSize, 0.f);
    // pitch window + limiter look-ahead: constant, whichever sections are switched on or off
    setLatencySamples(pitch.latency() + master.latency());
}

void OneChainProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    const int ch = buf.getNumChannels();
    if (ch < 1) return;

    float* l = buf.getWritePointer(0);
    float* r = nullptr;
    const bool mono = (ch == 1);
    if (mono) {
        if ((int)dupR.size() < n) dupR.resize((size_t)n);
        std::copy(l, l + n, dupR.begin());
        r = dupR.data();
    } else {
        r = buf.getWritePointer(1);
    }

    // ---- Pitch (always runs: keeps latency constant; on=false -> ratio 1) ----
    oc::PitchParams pp;
    pp.on = get("pitch_on") > 0.5f;
    pp.key = (int)get("pitch_key");
    pp.scale = (int)get("pitch_scale");
    pp.speedMs = get("pitch_speed");
    pp.amount = get("pitch_amount");
    pitch.process(l, r, n, pp);

    // ---- Opto ----
    if (get("opto_on") > 0.5f)
        opto.process(l, r, n, get("opto_peak"), get("opto_gain"), get("opto_limit") > 0.5f,
                     get("opto_link") / 100.f, get("opto_mix") / 100.f);

    // ---- Master ----
    oc::MasterChain::Params mp;
    mp.enabled = get("mast_on") > 0.5f;
    mp.inDb = get("mast_in");
    mp.lowCut = get("mast_lowcut");
    mp.lowDb = get("mast_low");
    mp.midFreq = get("mast_midfreq");
    mp.midDb = get("mast_mid");
    mp.highDb = get("mast_high");
    mp.mb.x1 = get("mast_x1");
    mp.mb.x2 = get("mast_x2");
    mp.mb.b[0].comp = get("mast_b1_comp"); mp.mb.b[0].sat = get("mast_b1_sat"); mp.mb.b[0].gainDb = get("mast_b1_gain");
    mp.mb.b[1].comp = get("mast_b2_comp"); mp.mb.b[1].sat = get("mast_b2_sat"); mp.mb.b[1].gainDb = get("mast_b2_gain");
    mp.mb.b[2].comp = get("mast_b3_comp"); mp.mb.b[2].sat = get("mast_b3_sat"); mp.mb.b[2].gainDb = get("mast_b3_gain");
    mp.glueThr = get("mast_glue_thr");
    mp.glueRatio = get("mast_glue_ratio");
    mp.glueAtk = get("mast_glue_atk");
    mp.glueRel = get("mast_glue_rel");
    mp.glueMix = get("mast_glue") / 100.f;
    mp.driveDb = get("mast_drive");
    mp.driveMix = get("mast_drive_mix") / 100.f;
    mp.autoGain = get("mast_autogain") > 0.5f;
    mp.width = get("mast_width") / 100.f;
    mp.bassMono = get("mast_bassmono");
    mp.outDb = get("mast_out");
    mp.ceilingDb = get("mast_ceiling");
    master.process(l, r, n, mp);

    const float outDb = master.meters().outPeakDb;
    if (outDb > meterOut.load()) meterOut.store(outDb);

    if (mono) for (int i = 0; i < n; ++i) l[i] = 0.5f * (l[i] + r[i]);
}

int OneChainProcessor::getNumPrograms() { return (int)oc::presets().size(); }

const juce::String OneChainProcessor::getProgramName(int index)
{
    const auto& P = oc::presets();
    return (index >= 0 && index < (int)P.size()) ? juce::String(P[(size_t)index].name) : juce::String();
}

void OneChainProcessor::setCurrentProgram(int index)
{
    const auto& P = oc::presets();
    if (index < 0 || index >= (int)P.size()) return;
    currentProgram = index;
    for (auto* prm : getParameters())
        prm->setValueNotifyingHost(prm->getDefaultValue());
    for (const auto& kv : P[(size_t)index].values)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(kv.first)))
            rp->setValueNotifyingHost(rp->convertTo0to1(kv.second));
}

juce::AudioProcessorEditor* OneChainProcessor::createEditor()
{
    return new OneChainEditor(*this);
}

void OneChainProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
}

void OneChainProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OneChainProcessor(); }
