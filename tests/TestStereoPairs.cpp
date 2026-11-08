#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace
{
int64_t addMono(SampleRepository& repo, const juce::String& folder, const juce::String& name,
                double duration, int channels = 1)
{
    SampleRecord r;
    r.filePath = folder + "/" + name;
    r.fileName = name;
    r.fileHash = name;
    r.fileSize = 100;
    r.version = basicFeaturesVersion;
    r.numChannels = channels;
    r.sampleRate = 44100.0;
    r.duration = duration;
    return repo.upsert(r);
}
}
TEST_CASE("A valid mono L/R pair is one library row with its partner path", "[stereo-pairs][database]")
{
    TestDirectory t; Database db(t.dir.getChildFile("pairs.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto folder = t.dir.getChildFile("lib").getFullPathName();
    const auto left = addMono(repo, folder, "10A01DRM14-L.wav", 3.428571);
    const auto right = addMono(repo, folder, "10A01DRM14-R.wav", 3.428571);
    repo.refreshDuplicates();

    LibraryFilter f;
    REQUIRE(repo.queryIds(f) == std::vector<int64_t>{ left });
    REQUIRE(repo.count() == 1);
    REQUIRE(repo.countInFolder(folder) == 1);
    REQUIRE(repo.idsInFolder(folder) == std::vector<int64_t>{ left });
    REQUIRE(repo.visibleIds({ left, right }) == std::vector<int64_t>{ left });
    const auto partners = repo.stereoPairPartners({ left });
    REQUIRE(partners.size() == 1);
    REQUIRE(partners.at(left) == folder + "/10A01DRM14-R.wav");
    REQUIRE(repo.stereoPairPartners({ right }).empty());
}

TEST_CASE("Mismatched duration or stereo files do not pair", "[stereo-pairs][database]")
{
    TestDirectory t;
    {
        Database db(t.dir.getChildFile("mismatch.sqlite")); SampleRepository repo(db); repo.createTables();
        const auto folder = t.dir.getChildFile("lib").getFullPathName();
        const auto left = addMono(repo, folder, "X-L.wav", 3.4);
        const auto right = addMono(repo, folder, "X-R.wav", 3.5);
        repo.refreshDuplicates();
        LibraryFilter f;
        const auto ids = repo.queryIds(f);
        REQUIRE(ids.size() == 2);
        REQUIRE(std::find(ids.begin(), ids.end(), left) != ids.end());
        REQUIRE(std::find(ids.begin(), ids.end(), right) != ids.end());
    }
    {
        Database db(t.dir.getChildFile("stereo.sqlite")); SampleRepository repo(db); repo.createTables();
        const auto folder = t.dir.getChildFile("lib2").getFullPathName();
        addMono(repo, folder, "Y-L.wav", 3.4, 2);
        addMono(repo, folder, "Y-R.wav", 3.4, 2);
        repo.refreshDuplicates();
        LibraryFilter f;
        REQUIRE(repo.queryIds(f).size() == 2);
    }
}

TEST_CASE("Ignoring one half breaks the pair and leaves the other visible", "[stereo-pairs][ignore]")
{
    TestDirectory t; Database db(t.dir.getChildFile("ignore-pair.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto folder = t.dir.getChildFile("lib").getFullPathName();
    const auto left = addMono(repo, folder, "Z-L.wav", 2.0);
    const auto right = addMono(repo, folder, "Z-R.wav", 2.0);
    repo.refreshDuplicates();
    LibraryFilter f;
    REQUIRE(repo.queryIds(f) == std::vector<int64_t>{ left });

    repo.ignorePath(folder + "/Z-L.wav", false);
    repo.refreshDuplicates();
    REQUIRE(repo.queryIds(f) == std::vector<int64_t>{ right });
    REQUIRE(repo.stereoPairPartners({ left }).empty());

    repo.unignorePath(folder + "/Z-L.wav");
    repo.refreshDuplicates();
    REQUIRE(repo.queryIds(f) == std::vector<int64_t>{ left });
}

TEST_CASE("A pair with identical bytes is not hidden by Hide duplicates", "[stereo-pairs][duplicates]")
{
    TestDirectory t; Database db(t.dir.getChildFile("identical-pair.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto folder = t.dir.getChildFile("lib").getFullPathName();
    SampleRecord left; left.filePath = folder + "/I-L.wav"; left.fileName = "I-L.wav";
    left.fileHash = "same-bytes"; left.fileSize = 100; left.version = basicFeaturesVersion;
    left.numChannels = 1; left.sampleRate = 44100.0; left.duration = 2.0;
    SampleRecord right = left; right.filePath = folder + "/I-R.wav"; right.fileName = "I-R.wav";
    const auto leftId = repo.upsert(left);
    const auto rightId = repo.upsert(right);
    repo.refreshDuplicates();
    LibraryFilter f; f.hideDuplicates = true;
    REQUIRE(repo.queryIds(f) == std::vector<int64_t>{ leftId });
    REQUIRE(repo.duplicateInfo(rightId) == std::nullopt);
}
