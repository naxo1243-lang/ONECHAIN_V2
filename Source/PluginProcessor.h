#pragma once
#include <atomic>
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/PitchCorrector.h"
#include "Dsp/OptoCompressor.h"
#include "Dsp/MasterChain.h"
#include "Dsp/LinearPhaseEq.h"

class OneChainProcessor : public juce::AudioProcessor {
public:
    OneChainProcessor();
    ~OneChainProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OneChain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // meters (written by the audio thread, read + reset by the GUI)
    std::atomic<float> meterOut { -100.f };

    // spectrum analyzer feed: last n post-EQ mono samples (n <= kAnaSize), safe to call from the GUI thread
    static constexpr int kAnaSize = 8192;
    void readAnalyzer(float* dest, int n) const
    {
        const int pos = anaPos.load();
        for (int i = 0; i < n; ++i) dest[i] = anaBuf[(size_t)((pos - n + i + 2 * kAnaSize) & (kAnaSize - 1))];
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float get(const char* id) const { return apvts.getRawParameterValue(id)->load(); }

    oc::PitchCorrector pitch;
    oc::OptoCompressor opto;
    oc::LinearPhaseEq  eq;
    oc::MasterChain    master;
    std::vector<float> dupR;   // for mono-in/mono-out
    int currentProgram = 0;

    struct EqPtr { std::atomic<float> *on = nullptr, *shape = nullptr, *freq = nullptr, *gain = nullptr, *q = nullptr, *slope = nullptr, *place = nullptr; };
    EqPtr eqPtr[oc::kEqBands];
    std::atomic<float> *eqOnPtr = nullptr, *eqGainPtr = nullptr;
    std::array<float, kAnaSize> anaBuf {};
    std::atomic<int> anaPos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OneChainProcessor)
};
