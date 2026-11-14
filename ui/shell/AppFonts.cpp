#include "AppFonts.h"
#include "BinaryData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <mutex>

namespace sonicator::fonts
{
namespace
{
// The faces must stay alive: a face created from memory keeps its "Manrope"/style registration
// only while the returned pointer lives. A shutdown-deleted store gives JUCE back its objects
// before it checks for leaks.
struct FaceStore final : juce::DeletedAtShutdown
{
    juce::Typeface::Ptr faces[4];

    static FaceStore& get()
    {
        static FaceStore* store = new FaceStore();
        return *store;
    }

    ~FaceStore() override { clear(); }

    void load()
    {
        if (faces[(int) Weight::regular] != nullptr) return;
        faces[(int) Weight::regular] = juce::Typeface::createSystemTypefaceFor(
            BinaryData::ManropeRegular_ttf, (size_t) BinaryData::ManropeRegular_ttfSize);
        faces[(int) Weight::medium] = juce::Typeface::createSystemTypefaceFor(
            BinaryData::ManropeMedium_ttf, (size_t) BinaryData::ManropeMedium_ttfSize);
        faces[(int) Weight::semiBold] = juce::Typeface::createSystemTypefaceFor(
            BinaryData::ManropeSemiBold_ttf, (size_t) BinaryData::ManropeSemiBold_ttfSize);
        faces[(int) Weight::bold] = juce::Typeface::createSystemTypefaceFor(
            BinaryData::ManropeBold_ttf, (size_t) BinaryData::ManropeBold_ttfSize);
    }

    void clear()
    {
        for (auto& face : faces) face.reset();
    }
};

std::once_flag installFlag;

void ensureFaces()
{
    std::call_once(installFlag, [] {
        FaceStore::get().load();
        // Every Font created from a size alone asks the default LookAndFeel for the "<Sans-Serif>"
        // placeholder. Pointing it at the family selects the embedded Regular or Bold by style,
        // so direct juce::Font(FontOptions(size)) calls follow the interface font too.
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName("Manrope");
    });
}
} // namespace

void install()
{
    ensureFaces();
}

juce::FontOptions options(float height, Weight weight)
{
    ensureFaces();
    if (auto face = FaceStore::get().faces[(int) weight]; face != nullptr)
        return juce::FontOptions(face).withHeight(height * heightCalibration);
    return juce::FontOptions(height * heightCalibration);
}

juce::Font font(float height, Weight weight)
{
    return juce::Font(options(height, weight));
}

juce::Typeface::Ptr typeface(Weight weight)
{
    ensureFaces();
    return FaceStore::get().faces[(int) weight];
}
} // namespace sonicator::fonts
