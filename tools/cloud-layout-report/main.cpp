// Layout report: computes the cloud map on a real catalog and writes the material a person needs
// to judge it without a UI — a scatter image, the CSV of coordinates and, for a handful of seeds,
// the five nearest neighbours with their file paths so they can be listened to.
#include "core/cloud/CloudLayoutBuilder.h"
#include "core/cloud/CloudSource.h"
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include <cmath>
#include <cstdio>
#include <map>

using namespace sonicator;

namespace
{
struct Args
{
    juce::File database;
    juce::File out;
    int neighbours = 5;
};

Args parse(int argc, char** argv)
{
    Args args;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg(argv[i]);
        if (arg == "--db" && i + 1 < argc) args.database = juce::File(juce::String(argv[++i]));
        else if (arg == "--out" && i + 1 < argc) args.out = juce::File(juce::String(argv[++i]));
        else if (arg == "--neighbours" && i + 1 < argc) args.neighbours = juce::String(argv[++i]).getIntValue();
    }
    if (args.database.getFullPathName().isEmpty())
        args.database = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                            .getChildFile("Library/Sonicator/sonicator.sqlite");
    if (args.out.getFullPathName().isEmpty())
        args.out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sonicator-cloud-report");
    return args;
}

juce::String escapeSvg(const juce::String& text)
{
    return text.replaceCharacter('&', '+').replaceCharacter('<', '-').replaceCharacter('>', '-');
}

void writeSvg(const juce::File& file, const cloud::CloudInput& input, const cloud::CloudLayout& layout)
{
    constexpr float width = 1400.0f;
    const auto scale = width / juce::jmax(1.0f, layout.bounds.getWidth());
    const auto height = layout.bounds.getHeight() * scale;
    juce::String svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << (int) width << "\" height=\""
        << (int) height << "\" viewBox=\"0 0 " << (int) width << " " << (int) height << "\">\n"
        << "<rect width=\"100%\" height=\"100%\" fill=\"#141518\"/>\n";
    for (const auto& island : layout.islands)
    {
        const auto x = (island.x - layout.bounds.getX()) * scale;
        const auto y = (island.y - layout.bounds.getY()) * scale;
        const auto r = island.radius * scale;
        juce::String colour;
        colour << "#" << juce::String::toHexString((int) (island.colour & 0xffffff)).paddedLeft('0', 6);
        svg << "<circle cx=\"" << (int) x << "\" cy=\"" << (int) y << "\" r=\"" << (int) r
            << "\" fill=\"" << colour << "\" fill-opacity=\"0.10\" stroke=\"" << colour
            << "\" stroke-opacity=\"0.55\"/>\n";
        svg << "<text x=\"" << (int) x << "\" y=\"" << (int) (y - r - 5)
            << "\" fill=\"" << colour << "\" font-size=\"13\" font-family=\"sans-serif\" text-anchor=\"middle\">"
            << escapeSvg(island.name + " (" + juce::String(island.count) + ")") << "</text>\n";
    }
    for (const auto& point : layout.points)
    {
        const auto x = (point.x - layout.bounds.getX()) * scale;
        const auto y = (point.y - layout.bounds.getY()) * scale;
        juce::String colour;
        colour << "#" << juce::String::toHexString((int) (point.colour & 0xffffff)).paddedLeft('0', 6);
        const auto alpha = point.has(cloud::cloudNoPosition) ? 0.35f : (point.has(cloud::cloudHasV2) ? 0.95f : 0.7f);
        svg << "<circle cx=\"" << (int) x << "\" cy=\"" << (int) y << "\" r=\"2.2\" fill=\"" << colour
            << "\" fill-opacity=\"" << alpha << "\"/>\n";
    }
    svg << "</svg>\n";
    file.replaceWithText(svg);
}

void writeCsv(const juce::File& file, const cloud::CloudInput& input, const cloud::CloudLayout& layout)
{
    juce::StringArray lines;
    lines.add("sample_id,x,y,island,tags,has_v2,has_basic,path");
    for (size_t i = 0; i < layout.points.size(); ++i)
    {
        const auto& point = layout.points[i];
        const auto& node = input.nodes[i];
        juce::StringArray tags;
        for (auto tagId : node.tagIds)
            for (const auto& tag : input.tags)
                if (tag.id == tagId) tags.add(tag.name);
        lines.add(juce::String(point.sampleId) + "," + juce::String(point.x, 2) + "," + juce::String(point.y, 2) + ","
                  + juce::String(point.islandIndex) + ",\"" + tags.joinIntoString("+") + "\","
                  + (point.has(cloud::cloudHasV2) ? "1" : "0") + ","
                  + (point.has(cloud::cloudHasBasic) ? "1" : "0") + ",\"" + node.path + "\"");
    }
    file.replaceWithText(lines.joinIntoString("\n"));
}

void writeNeighbours(const juce::File& file, const cloud::CloudInput& input, const cloud::CloudLayout& layout,
                     int wanted)
{
    juce::StringArray report;
    // One seed per tag that matters: the map is judged on whether its nearest sounds are the same
    // kind of sound, and the owner listens, not the machine.
    const juce::StringArray seedTags { "Kick", "Snare", "Hat", "Bass", "Pad", "Chord", "FX",
                                      "Loop", "Vocal", "Dubtechno", "Stab", "Riser" };
    size_t printed = 0;
    for (const auto& tagName : seedTags)
    {
        int64_t tagId = 0;
        for (const auto& tag : input.tags) if (tag.name.equalsIgnoreCase(tagName)) tagId = tag.id;
        if (tagId == 0) continue;
        size_t seed = layout.points.size();
        for (size_t i = 0; i < input.nodes.size(); ++i)
        {
            const auto& node = input.nodes[i];
            if (std::find(node.tagIds.begin(), node.tagIds.end(), tagId) == node.tagIds.end()) continue;
            if (node.hasV2) { seed = i; break; } // prefer a file with the sharper neighbourhood
            if (seed == layout.points.size() && node.hasBasic) seed = i;
        }
        if (seed >= layout.points.size()) continue;
        report.add("== " + tagName + " ==");
        report.add("seed: " + input.nodes[seed].path + (input.nodes[seed].hasV2 ? " [v2]" : " [basic]"));
        for (const auto& [index, distance] : cloud::nearestNeighbours(layout, seed, wanted))
        {
            juce::StringArray tags;
            for (auto id : input.nodes[index].tagIds)
                for (const auto& tag : input.tags)
                    if (tag.id == id) tags.add(tag.name);
            report.add("  " + juce::String(distance, 1) + " px  [" + tags.joinIntoString("+") + "]  "
                       + input.nodes[index].path);
        }
        report.add("");
        ++printed;
    }
    file.replaceWithText(report.joinIntoString("\n"));
    std::printf("seeds printed: %d\n", (int) printed);
}
} // namespace

int main(int argc, char** argv)
{
    const auto args = parse(argc, argv);
    if (! args.database.existsAsFile())
    {
        std::fprintf(stderr, "catalog not found: %s\n", args.database.getFullPathName().toRawUTF8());
        return 1;
    }
    args.out.createDirectory();
    Database database(args.database);
    SampleRepository repository(database);

    const auto loadStart = juce::Time::getMillisecondCounterHiRes();
    const auto input = cloud::loadCloudInput(repository);
    const auto loadMs = juce::Time::getMillisecondCounterHiRes() - loadStart;

    const auto layoutStart = juce::Time::getMillisecondCounterHiRes();
    const auto layout = cloud::buildCloudLayout(input);
    const auto layoutMs = juce::Time::getMillisecondCounterHiRes() - layoutStart;

    int withV2 = 0, basicOnly = 0, noPosition = 0;
    for (const auto& point : layout.points)
    {
        if (point.has(cloud::cloudNoPosition)) ++noPosition;
        else if (point.has(cloud::cloudHasV2)) ++withV2;
        else ++basicOnly;
    }
    std::printf("nodes: %d  islands: %d  v2: %d  basic-only: %d  no-position: %d\n",
                (int) input.nodes.size(), (int) layout.islands.size(), withV2, basicOnly, noPosition);
    std::printf("load: %.0f ms  layout: %.0f ms\n", loadMs, layoutMs);
    std::printf("bounds: %.0f x %.0f\n", layout.bounds.getWidth(), layout.bounds.getHeight());

    writeSvg(args.out.getChildFile("layout.svg"), input, layout);
    writeCsv(args.out.getChildFile("layout.csv"), input, layout);
    writeNeighbours(args.out.getChildFile("neighbours.txt"), input, layout, args.neighbours);
    std::printf("written to %s\n", args.out.getFullPathName().toRawUTF8());
    return 0;
}
