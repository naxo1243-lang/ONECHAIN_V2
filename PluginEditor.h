#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Editor: a fixed 626x1024 alien-themed UI that scales to the window size.
class OneChainEditor : public juce::AudioProcessorEditor {
public:
    explicit OneChainEditor(OneChainProcessor&);
    ~OneChainEditor() override;
    void resized() override;

private:
    std::unique_ptr<juce::Component> ui;   // OneChainUI (defined in PluginEditor.cpp)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OneChainEditor)
};
