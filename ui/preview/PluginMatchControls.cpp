#include "ui/preview/PluginMatchControls.h"
#include "ui/shell/DesignTokens.h"
#include "ui/shell/Theme.h"

namespace sonicator
{
PluginMatchControls::PluginMatchControls()
{
    matchBpm.setComponentID ("pluginMatchBpm");
    matchScale.setComponentID ("pluginMatchScale");
    matchBpm.setTooltip ("Stretch this loop to the host project tempo; pitch stays the same");
    matchScale.setTooltip ("Transpose this sample to the chosen tonic and mode");

    tonic.setComponentID ("pluginMatchTonic");
    tonic.setTooltip ("Target tonic for Match Scale");
    for (int i = 0; i < 12; ++i) tonic.addItem (tonicName (i), i + 1);
    tonic.setSelectedId (1, juce::dontSendNotification);
    tonic.setTextWhenNothingSelected ("Key");

    mode.setComponentID ("pluginMatchMode");
    mode.setTooltip ("Target mode for Match Scale; a shift never converts minor to major");
    mode.addItem ("minor", 1);
    mode.addItem ("major", 2);
    mode.setSelectedId (1, juce::dontSendNotification);

    status.setComponentID ("pluginMatchStatus");
    status.setJustificationType (juce::Justification::centredLeft);
    status.setFont (fonts::font (11));
    status.setColour (juce::Label::textColourId, theme::muted);
    status.setInterceptsMouseClicks (false, false);
    status.setText ({}, juce::dontSendNotification);

    matchBpm.onClick = [this] { notify(); };
    matchScale.onClick = [this] { notify(); };
    tonic.onChange = [this] { if (onTargetChanged) onTargetChanged(); };
    mode.onChange = [this] { if (onTargetChanged) onTargetChanged(); };

    for (auto* child : std::initializer_list<juce::Component*> { &matchBpm, &matchScale, &tonic,
                                                                &mode, &status })
        addAndMakeVisible (child);
    setInterceptsMouseClicks (false, true);
    refreshFromPreferences();
}

void PluginMatchControls::setPreferences (const PluginMatchPreferences& prefs,
                                          juce::NotificationType notification)
{
    tonic.setSelectedId (juce::jlimit (0, 11, prefs.targetTonic) + 1, notification);
    mode.setSelectedId (prefs.targetMode == TargetMode::major ? 2 : 1, notification);
    matchBpm.setToggleState (prefs.matchBpm, notification);
    matchScale.setToggleState (prefs.matchScale, notification);
    setTargetEnabled (prefs.matchScale);
}

PluginMatchPreferences PluginMatchControls::preferences() const
{
    PluginMatchPreferences prefs;
    prefs.matchBpm = matchBpm.getToggleState();
    prefs.matchScale = matchScale.getToggleState();
    prefs.targetTonic = juce::jlimit (0, 11, tonic.getSelectedId() - 1);
    prefs.targetMode = mode.getSelectedId() == 2 ? TargetMode::major : TargetMode::minor;
    return prefs;
}

void PluginMatchControls::setStatus (const juce::String& text, bool error)
{
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, error ? theme::accent : theme::muted);
}

void PluginMatchControls::setTargetEnabled (bool enabled)
{
    tonic.setEnabled (enabled);
    mode.setEnabled (enabled);
}

void PluginMatchControls::refreshFromPreferences()
{
    setTargetEnabled (matchScale.getToggleState());
}

void PluginMatchControls::notify()
{
    setTargetEnabled (matchScale.getToggleState());
    if (onChange) onChange();
}

void PluginMatchControls::paint (juce::Graphics&) {}

void PluginMatchControls::resized()
{
    auto area = getLocalBounds().reduced (0, 2);
    auto row = area.removeFromTop (studio::chipSmallHeight);
    const int gap = studio::spacingTight;
    const int chip = juce::jmax (92, row.getWidth() / 5);
    matchBpm.setBounds (row.removeFromLeft (chip));
    row.removeFromLeft (gap);
    matchScale.setBounds (row.removeFromLeft (chip));
    row.removeFromLeft (gap);
    const int picker = juce::jmax (64, row.getWidth() / 2 - gap);
    tonic.setBounds (row.removeFromLeft (picker));
    row.removeFromLeft (gap);
    mode.setBounds (row);
    status.setBounds (area.removeFromTop (18));
}
} // namespace sonicator
