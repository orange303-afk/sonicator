#pragma once
#include "core/match/MatchPreferences.h"
#include "core/match/MatchTypes.h"
#include "plugin/HostTempoSnapshot.h"
#include "ui/shell/StudioControls.h"
#include <functional>
#include <optional>

namespace sonicator
{
// Everything the preview needs from the plug-in instance. The application never installs one, so
// the two controls do not exist in the standalone window at all.
struct PluginMatchContext
{
    std::function<PluginMatchPreferences()> getPreferences;
    std::function<void (const PluginMatchPreferences&)> setPreferences;
    std::function<plugin::HostTempoValue()> hostTempo;
    // Catalog identity and effective metadata for one file. Nullopt means the file has no
    // catalog record or its format cannot be matched.
    std::function<std::optional<MatchSource> (const juce::File&, int64_t& sourceSize,
                                              int64_t& sourceModified)> sourceInfo;
};

// The two toggles and the manual target key. Only the plugin builds this component; it sits in
// the existing transport group so the controls follow Play/Stop rather than the library header.
class PluginMatchControls final : public juce::Component
{
public:
    PluginMatchControls();
    void setPreferences (const PluginMatchPreferences&, juce::NotificationType);
    PluginMatchPreferences preferences() const;
    void setStatus (const juce::String& text, bool error = false);
    // The target picker is only meaningful with Match Scale on.
    void setTargetEnabled (bool);
    void refreshFromPreferences();

    std::function<void()> onChange;
    std::function<void()> onTargetChanged;

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int preferredHeight = 46;

private:
    studio::CheckChip matchBpm { "Match BPM" }, matchScale { "Match Scale" };
    juce::ComboBox tonic, mode;
    juce::Label status;
    void notify();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginMatchControls)
};
} // namespace sonicator
