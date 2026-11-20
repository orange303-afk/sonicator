#include "AudioEditWriter.h"
#include "AudioFileWriter.h"
#include "core/settings/AppSettings.h"

namespace sonicator::edit
{
namespace
{
constexpr int chunkFrames = 65536;

juce::CriticalSection& replaceJournalLock()
{
    static juce::CriticalSection lock;
    return lock;
}
juce::File replaceJournalFile()
{
    const auto testDirectory = AppSettings::testSettingsDirectory();
    const auto directory = testDirectory != juce::File() ? testDirectory
        : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
              .getChildFile("Sonicator");
    return directory.getChildFile("replace-in-place.json");
}
void writeReplaceJournal(const juce::File& target, const juce::File& staged,
                         const files::DisposalPolicy policy)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("target", target.getFullPathName());
    root->setProperty("staged", staged.getFullPathName());
    root->setProperty("policy", files::disposalPolicyToString(policy));
    auto file = replaceJournalFile();
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(file);
    if (temporary.getFile().replaceWithText(juce::JSON::toString(juce::var(root), false)))
        temporary.overwriteTargetFileWithTemporary();
}
void clearReplaceJournal() { replaceJournalFile().deleteFile(); }

juce::String extensionOf(const juce::File& file)
{
    return file.getFileExtension().toLowerCase();
}

bool isSamplerKey(const juce::String& key)
{
    return key.startsWith("Loop") || key == "NumSampleLoops" || key == "MidiUnityNote"
        || key == "SamplerData" || key == "Manufacturer" || key == "Product"
        || key == "SamplePeriod" || key == "MidiPitchFraction" || key == "SmpteFormat"
        || key == "SmpteOffset" || key == "NumCuePoints" || key.startsWith("Cue");
}

juce::StringPairArray writerMetadata(const AudioEditBuffer& buffer, const juce::String& extension)
{
    const auto& markers = buffer.markers();
    juce::StringPairArray values;
    const auto& source = buffer.sourceMetadata();
    for (int i = 0; i < source.size(); ++i)
        if (! isSamplerKey(source.getAllKeys()[i]))
            values.set(source.getAllKeys()[i], source.getAllValues()[i]);
    if (extension != ".wav" && extension != ".wave") return values;
    if (markers.hasRootKey) values.set("MidiUnityNote", juce::String(markers.rootKey));
    if (markers.hasLoop && markers.loopEndExclusive > markers.loopStart)
    {
        values.set("NumSampleLoops", "1");
        values.set("Loop0Start", juce::String(markers.loopStart));
        // JUCE stores the loop end as the last frame that plays.
        values.set("Loop0End", juce::String(markers.loopEndExclusive - 1));
        values.set("Loop0Type", "0");
    }
    if (! markers.cuePositions.empty())
    {
        values.set("NumCuePoints", juce::String((int) markers.cuePositions.size()));
        for (size_t i = 0; i < markers.cuePositions.size(); ++i)
        {
            values.set("Cue" + juce::String((int) i) + "Identifier", juce::String((int) i + 1));
            values.set("Cue" + juce::String((int) i) + "Order", juce::String((int) i));
            values.set("Cue" + juce::String((int) i) + "Offset",
                       juce::String(markers.cuePositions[i]));
        }
    }
    return values;
}

SaveResult writeAudioFile(const juce::File& destination, const AudioEditBuffer& buffer,
                          const juce::String& extension, const bool replaceExisting,
                          CancelCheck cancel)
{
    if (buffer.frames() <= 0 || buffer.sampleRate() <= 0)
        return { false, destination, "Nothing to write" };
    if (! replaceExisting && destination.existsAsFile())
        return { false, destination, "Destination already exists: " + destination.getFileName() };

    AudioWriteSpec spec;
    spec.sampleRate = buffer.sampleRate();
    spec.channels = buffer.channels();
    spec.bitsPerSample = buffer.bitsPerSample();
    spec.frames = buffer.frames();
    spec.floatingPoint = buffer.floatingPoint();
    spec.metadata = writerMetadata(buffer, extension);
    return writeAudioStream(destination, extension, spec,
        [&buffer, cancel, channels = spec.channels](juce::AudioFormatWriter& writer)
        {
            juce::AudioBuffer<float> chunk(channels, chunkFrames);
            int64_t frame = 0;
            while (frame < buffer.frames())
            {
                if (cancel && cancel()) return false;
                const auto count = (int) juce::jmin<int64_t>(chunkFrames, buffer.frames() - frame);
                chunk.copyFrom(0, 0, buffer.channel(0).data() + frame, count);
                if (channels > 1)
                    chunk.copyFrom(1, 0, buffer.channel(1).data() + frame, count);
                if (! writer.writeFromFloatArrays(chunk.getArrayOfReadPointers(), channels, count))
                    return false;
                frame += count;
            }
            return true;
        }, cancel);
}
} // namespace

bool canOverwriteInPlace(const juce::File& target)
{
    const auto extension = extensionOf(target);
    return extension == ".wav" || extension == ".wave" || extension == ".aif"
        || extension == ".aiff" || extension == ".flac";
}

juce::File uniqueCopyPath(const juce::File& original, const juce::String& stem)
{
    const auto extension = canOverwriteInPlace(original) ? original.getFileExtension() : ".wav";
    auto candidate = original.getSiblingFile(stem + " edit" + extension);
    for (int suffix = 2; candidate.existsAsFile(); ++suffix)
        candidate = original.getSiblingFile(stem + " edit " + juce::String(suffix) + extension);
    return candidate;
}

juce::String recoverInterruptedReplacement(const juce::File& journal)
{
    const juce::ScopedLock lock(replaceJournalLock());
    const auto file = journal == juce::File() ? replaceJournalFile() : journal;
    if (! file.existsAsFile()) return {};
    const auto parsed = juce::JSON::parse(file);
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
    {
        file.deleteFile();
        return "Discarded an unreadable replace-in-place journal";
    }
    const juce::File target(root->getProperty("target").toString());
    const juce::File staged(root->getProperty("staged").toString());
    const auto policy = files::disposalPolicyFromString(root->getProperty("policy").toString(),
                                                        files::DisposalPolicy::Trash);
    juce::String message;
    if (staged.existsAsFile())
    {
        if (! target.exists())
        {
            if (staged.moveFileTo(target)) message = "Restored " + target.getFullPathName();
            else message = "Could not restore " + target.getFullPathName()
                + "; the previous file is at " + staged.getFullPathName();
        }
        else
        {
            const auto outcome = files::disposeFile(staged, policy);
            message = outcome.ok ? "Removed an old copy left by an interrupted save"
                                 : outcome.error;
        }
    }
    // Keep the journal only when the staged file still needs attention.
    if (! staged.existsAsFile()) file.deleteFile();
    return message;
}

PublishResult publishReplacement(const juce::File& target, juce::TemporaryFile& prepared,
                                 const files::DisposalPolicy policy)
{
    if (! prepared.getFile().existsAsFile())
        return { false, false, "The prepared file is missing", {} };
    const juce::ScopedLock lock(replaceJournalLock());
    // The old file moves aside first: a failed rename of the new file can then restore it
    // instead of leaving the path empty. The journal covers a crash between the two renames.
    const auto staged = target.getSiblingFile(
        "." + target.getFileName() + ".sonicator-old-" + juce::Uuid().toString());
    writeReplaceJournal(target, staged, policy);
    if (! target.moveFileTo(staged))
    {
        clearReplaceJournal();
        return { false, false, "Cannot move the original aside", {} };
    }
    if (! prepared.getFile().moveFileTo(target))
    {
        staged.moveFileTo(target);
        clearReplaceJournal();
        return { false, false, "Cannot replace the file", {} };
    }
    const auto disposed = files::disposeFile(staged, policy);
    clearReplaceJournal();
    if (! disposed.ok)
    {
        // The replacement is published; only the old copy could not be disposed of.
        return { true, true, disposed.error, staged };
    }
    return { true, false, {}, {} };
}

SaveResult overwrite(const juce::File& target, const AudioEditBuffer& buffer, CancelCheck cancel,
                     const files::DisposalPolicy policy)
{
    if (! canOverwriteInPlace(target))
        return { false, target, "This format cannot be overwritten; save a copy instead" };
    if (! target.existsAsFile()) return { false, target, "File not found" };
    if (cancel && cancel()) return { false, target, "Cancelled" };
    const auto writeTarget = target.isSymbolicLink() ? target.getLinkedTarget() : target;
    juce::TemporaryFile temporary(writeTarget);
    auto result = writeAudioFile(temporary.getFile(), buffer, extensionOf(writeTarget), true, cancel);
    if (! result.success)
    {
        temporary.getFile().deleteFile();
        return { false, target, result.error };
    }
    copyFileAttributes(writeTarget, temporary.getFile());
    auto published = publishReplacement(writeTarget, temporary, policy);
    if (! published.success) return { false, target, published.error };
    // A disposal failure is reported as a warning on an otherwise successful save.
    return { true, writeTarget, published.error };
}

SaveResult saveCopy(const juce::File& original, const juce::String& stem,
                    const AudioEditBuffer& buffer, CancelCheck cancel)
{
    if (cancel && cancel()) return { false, {}, "Cancelled" };
    const auto destination = uniqueCopyPath(original, stem);
    auto result = writeAudioFile(destination, buffer, extensionOf(destination), false, cancel);
    if (! result.success) result.file.deleteFile();
    return result;
}
} // namespace sonicator::edit
