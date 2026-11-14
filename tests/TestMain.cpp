#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>
#include "core/settings/AppSettings.h"
#include "ui/shell/AppFonts.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    // Screenshots and layout tests must see the same typeface the application ships.
    sonicator::fonts::install();
    // Every UI component that builds its own settings store must land here, never in the
    // owner's live ~/Library/Sonicator/settings.json.
    const auto testSettings = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("sonicator-test-settings-" + juce::String::toHexString(juce::Time::currentTimeMillis()));
    testSettings.createDirectory();
    sonicator::AppSettings::setTestSettingsDirectory(testSettings);
    const auto result = Catch::Session().run(argc, argv);
    sonicator::AppSettings::setTestSettingsDirectory({});
    testSettings.deleteRecursively();
    return result;
}
