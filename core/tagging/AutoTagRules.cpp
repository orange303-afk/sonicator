#include "AutoTagRules.h"
#include <algorithm>
namespace sonicator {
namespace {
struct Alias { const char* type; const char* tokens; };
// Exact tokens only. "bd"/"sd"/"hh" are safe here because the tokenizer never matches a
// substring, unlike the broader smart-analysis heuristics.
constexpr Alias instrumentAliases[] = {
    { "Kick", "kick|kicks|kickdrum|kik|bd|bassdrum|k|kickl|kickr|bdl|bdr" },
    { "Snare", "snare|snares|snr|sd|snaredrum|snrl|snrr|sdl|sdr" },
    { "Rim", "rim|rimshot|rimshots|sidestick" },
    { "Clap", "clap|claps|handclap" },
    { "Snap", "snap|snaps|fingersnap" },
    { "Click", "click|clicks|tick" },
    { "Blip", "blip|blips|bleep" },
    { "Zap", "zap|zaps|laser" },
    { "Glitch", "glitch|stutter" },
    { "Hat", "hat|hats|hihat|hh|hhl|hhr|openhat|closedhat" },
    { "Shaker", "shaker|shakers" },
    { "Tamb", "tamb|tambourine|tambourines" },
    { "Cowbell", "cowbell|cbell" },
    { "Bongo", "bongo|bongos" },
    { "Conga", "conga|congas|tumba" },
    { "Timbale", "timbale|timbales" },
    { "Agogo", "agogo" },
    { "Clave", "clave|claves|woodblock" },
    { "Tom", "tom|toms" },
    { "Crash", "crash|crashes" },
    { "Ride", "ride|rides" },
    { "Percussion", "perc|percussion|djembe|tabla|triangle|cabasa|guiro|cymbal|cymbals|splash" },
    { "Bass", "bass|bassline" },
    { "Sub", "sub|subbass" },
    { "Lead", "lead|leads|arp|arpeggio|pluck|plucks|stab|riff" },
    { "Pad", "pad|pads|atmos|atmosphere|texture|textures|drone|ambient" },
    { "Keys", "keys|piano|organ|rhodes|wurli|clav" },
    { "Guitar", "guitar|gtr" },
    { "Vocal", "vocal|vocals|vox|choir|acapella" },
    { "FX", "fx|sfx|effect|effects|noise|sweep|impact|foley" },
    { "Fill", "fill|fills|drumfill" },
    { "Riser", "riser|risers" },
    { "Upfilter", "upfilter|upfilters|uplifter|uplift" },
    { "Downfilter", "downfilter|downfilters|downlifter|downlift" }
};
struct CategoryAlias { const char* canonical; const char* tokens; };
constexpr CategoryAlias gearAliases[] = {
    { "909", "909|tr909|tr-909" },
    { "808", "808|tr808|tr-808" },
    { "707", "707|tr707|tr-707" },
    { "606", "606|tr606|tr-606" },
    { "727", "727|tr727|tr-727" },
    { "MPC", "mpc|mpc60|mpc3000|mpc2000|mpc1000|mpc5000" },
    { "Linn", "linn|linndrum|lm1|lm-1|lm2" },
    { "SP1200", "sp1200|sp-1200|sp12" },
    { "CR78", "cr78|cr-78" },
    { "DMX", "dmx" },
    { "303", "303|tb303|tb-303" }
};
constexpr CategoryAlias genreAliases[] = {
    { "House", "house|deephouse|techhouse|electrohouse|progressivehouse|afrohouse" },
    { "Techno", "techno|acidtechno|dubtechno|hardtechno|industrialtechno" },
    { "Hip Hop", "hiphop|hip-hop|hip_hop|boombap|boom_bap" },
    { "Trap", "trap" },
    { "DnB", "dnb|drumandbass|drum_and_bass|jungle|breakbeat|breaks" },
    { "Dubstep", "dubstep|riddim" },
    { "Ambient", "ambient|downtempo|chill|chillout" },
    { "Trance", "trance|psytrance|goa" },
    { "Disco", "disco|nudisco|nu_disco" },
    { "Funk", "funk|funky|soul" },
    { "Lo-Fi", "lofi|lo-fi|lo_fi" },
    { "Garage", "garage|ukg|2step|2-step" },
    { "Pop", "pop" },
    { "Rock", "rock" }
};
constexpr const char* unsupportedTokens = "synth|chords|chord|poly|brass|trumpet|sax|saxophone|horn|horns|"
                                          "strings|string|violin|cello|viola";
juce::String canonicalFromAliases(const juce::String& token)
{
    if (token.isEmpty()) return {};
    for (const auto& alias : instrumentAliases)
        for (const auto& candidate : juce::StringArray::fromTokens(alias.tokens, "|", ""))
            if (token.equalsIgnoreCase(candidate)) return juce::String(alias.type);
    if (token.length() > 3 && token.endsWithIgnoreCase("s"))
        return canonicalFromAliases(token.dropLastCharacters(1));
    return {};
}
bool isUnsupported(const juce::String& token)
{
    for (const auto& candidate : juce::StringArray::fromTokens(unsupportedTokens, "|", ""))
        if (token.equalsIgnoreCase(candidate)) return true;
    if (token.length() > 3 && token.endsWithIgnoreCase("s"))
        return isUnsupported(token.dropLastCharacters(1));
    return false;
}
struct Compound { const char* first; const char* second; const char* joined; const char* type; };
constexpr Compound compoundRules[] = {
    { "bass", "drum", "bassdrum", "Kick" },
    { "kick", "drum", "kickdrum", "Kick" },
    { "snare", "drum", "snaredrum", "Snare" },
    { "sub", "bass", "subbass", "Sub" },
    { "808", "kick", "808kick", "Kick" },
    { "808", "bass", "808bass", "Bass" }
};
struct TokenEvidence
{
    std::set<juce::String> classes;
    std::set<juce::String> unsupported;
    std::vector<juce::String> details;
    bool ambiguous808 = false;
    juce::String compoundDetail;
    juce::String compoundType;
};
TokenEvidence evidenceForTokens(const juce::StringArray& inputTokens)
{
    TokenEvidence evidence;
    auto tokens = inputTokens;
    std::vector<bool> consumed((size_t) tokens.size(), false);
    for (int i = 0; i + 1 < tokens.size(); ++i)
    {
        if (consumed[(size_t) i] || consumed[(size_t) (i + 1)]) continue;
        const auto first = tokens[i].toLowerCase();
        const auto second = tokens[(size_t) (i + 1)].toLowerCase();
        for (const auto& rule : compoundRules)
        {
            const auto joined = (first + second).toStdString();
            if ((first == rule.first && second == rule.second) || joined == rule.joined)
            {
                evidence.classes.insert(rule.type);
                evidence.compoundDetail = first + " " + second;
                evidence.compoundType = rule.type;
                consumed[(size_t) i] = consumed[(size_t) (i + 1)] = true;
                break;
            }
        }
    }
    for (int i = 0; i < tokens.size(); ++i)
    {
        if (consumed[(size_t) i]) continue;
        const auto& token = tokens[i];
        if (token.equalsIgnoreCase("808"))
        {
            // 808 alone is a kick or a bass depending on the sound; refuse to guess.
            evidence.ambiguous808 = true;
            continue;
        }
        if (auto type = canonicalFromAliases(token); type.isNotEmpty())
        {
            evidence.classes.insert(type);
            evidence.details.push_back(token);
        }
        else if (isUnsupported(token))
            evidence.unsupported.insert(token);
    }
    return evidence;
}
juce::String quote(const juce::String& value) { return "\"" + value + "\""; }
AutoTagDecision skip(const AutoTagInput& in, const juce::String& code, const juce::String& reason,
                     std::vector<juce::String> candidates = {})
{
    AutoTagDecision decision;
    AutoTagSkip s; s.sampleId = in.sampleId; s.filePath = in.filePath;
    s.fileName = juce::File(in.filePath).getFileName();
    s.code = code; s.reason = reason; s.candidates = std::move(candidates);
    decision.skip = std::move(s);
    return decision;
}
juce::String joinClasses(const std::set<juce::String>& classes)
{
    juce::StringArray names;
    for (const auto& name : classes) names.add(name);
    return names.joinIntoString(", ");
}
} // namespace
juce::String autoTagRuleKey(AutoTagRule rule)
{
    switch (rule)
    {
        case AutoTagRule::FilenameToken: return "filename-token";
        case AutoTagRule::FilenameCompound: return "filename-compound";
        case AutoTagRule::MetadataType: return "metadata-type";
        case AutoTagRule::SmartType: return "smart-type";
        case AutoTagRule::Gear: return "gear";
        case AutoTagRule::Genre: return "genre";
        case AutoTagRule::Structure: return "structure";
        case AutoTagRule::Folder: return "folder";
        case AutoTagRule::None: break;
    }
    return "none";
}
juce::StringArray AutoTagRules::supportedTypes()
{
    juce::StringArray types;
    for (const auto& alias : instrumentAliases)
        if (! types.contains(alias.type)) types.add(alias.type);
    for (const auto& gear : gearAliases)
        if (! types.contains(gear.canonical)) types.add(gear.canonical);
    for (const auto& genre : genreAliases)
        if (! types.contains(genre.canonical)) types.add(genre.canonical);
    return types;
}
juce::StringArray AutoTagRules::splitTokens(const juce::String& text)
{
    juce::StringArray raw;
    juce::String current;
    for (auto c : text)
    {
        if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '#')
            current += juce::String::charToString(c);
        else if (current.isNotEmpty())
        {
            raw.add(current);
            current.clear();
        }
    }
    if (current.isNotEmpty()) raw.add(current);

    juce::StringArray out;
    for (const auto& token : raw)
    {
        out.add(token);
        bool hasLetter = false, hasDigit = false;
        for (auto c : token)
        {
            if (juce::CharacterFunctions::isLetter(c)) hasLetter = true;
            else if (juce::CharacterFunctions::isDigit(c)) hasDigit = true;
        }
        if (hasLetter && hasDigit)
        {
            juce::String sub;
            bool lastDigit = false;
            for (int i = 0; i < token.length(); ++i)
            {
                const auto c = token[i];
                const bool isDigit = juce::CharacterFunctions::isDigit(c);
                if (i > 0 && isDigit != lastDigit && sub.isNotEmpty())
                {
                    out.add(sub);
                    sub.clear();
                }
                sub += juce::String::charToString(c);
                lastDigit = isDigit;
            }
            if (sub.isNotEmpty()) out.add(sub);
        }
    }
    return out;
}
juce::String AutoTagRules::canonicalTypeForToken(const juce::String& token)
{
    return canonicalFromAliases(token);
}
AutoTagDecision AutoTagRules::evaluate(const AutoTagInput& in, const AutoTagOptions& options)
{
    if (! in.inputCurrent)
        return skip(in, "changed-file", "the file changed after its metadata was recorded");
    const auto filenameTokens = splitTokens(juce::File(in.filePath).getFileNameWithoutExtension());
    const auto filename = evidenceForTokens(filenameTokens);

    // Stored smart result: only a completed, current resolution with known provenance counts.
    std::set<juce::String> smartClasses;
    juce::String smartSource, smartDetail;
    bool staleEvidence = false, folderProvenance = false;
    if (in.result.has_value())
    {
        const auto& result = *in.result;
        if (result.pipelineState == SmartPipelineState::Stale
            || result.extractorVersion != options.extractorVersion
            || result.resolverVersion != options.resolverVersion)
            staleEvidence = true;
        else if (result.pipelineState == SmartPipelineState::Complete
                 && result.typeState == SmartState::Resolved && result.type.isNotEmpty())
        {
            if (result.typeSource == "folder")
                folderProvenance = true;
            else if (result.typeSource == "filename")
            {
                // AutoTag parses the filename itself with a richer, instrument-specific table.
                // The stored smart candidate may still use a broader legacy class.
            }
            else if (result.typeSource.isNotEmpty())
            {
                const auto type = canonicalFromAliases(result.type);
                if (type.isNotEmpty())
                {
                    smartClasses.insert(type);
                    smartSource = result.typeSource;
                    smartDetail = result.type;
                }
            }
        }
    }

    // Stored candidates from metadata, sidecars or the filename. Folder candidates are context.
    std::set<juce::String> metadataClasses;
    juce::String metadataSource, metadataDetail;
    std::set<juce::String> acousticClasses;
    juce::String acousticSource, acousticDetail;
    const bool smartResultStale = in.result.has_value()
        && (in.result->pipelineState == SmartPipelineState::Stale
            || in.result->extractorVersion != options.extractorVersion
            || in.result->resolverVersion != options.resolverVersion);
    for (const auto& candidate : in.candidates)
    {
        if (candidate.field != SmartField::Type) continue;
        if (candidate.status != CandidateStatus::Valid) continue;
        if (candidate.source == "filename") continue; // redundant with the AutoTag tokenizer
        if (candidate.source == "folder")
        {
            folderProvenance = true;
            continue;
        }
        const bool isAcoustic = (candidate.source == "dsp_acoustic" || candidate.source == "ml_classifier" || candidate.source == "user_adaptive");
        const bool versionOk = isAcoustic
            ? (candidate.extractorVersion == options.extractorVersion
               || candidate.extractorVersion == options.dspVersion
               || candidate.extractorVersion == smartDspVersion)
            : (candidate.extractorVersion == options.extractorVersion);
        if (! versionOk || smartResultStale)
        {
            staleEvidence = true;
            continue;
        }
        const auto type = canonicalFromAliases(candidate.textValue);
        if (type.isEmpty()) continue;
        if (isAcoustic)
        {
            acousticClasses.insert(type);
            if (acousticSource.isEmpty())
            {
                acousticSource = candidate.source;
                acousticDetail = candidate.textValue;
            }
        }
        else
        {
            metadataClasses.insert(type);
            if (metadataSource.isEmpty())
            {
                metadataSource = candidate.source;
                metadataDetail = candidate.textValue;
            }
        }
    }

    // Folder names are context only: they never establish a type by themselves, and they cannot
    // veto a type that the filename or metadata states.
    const auto folderName = juce::File(in.filePath).getParentDirectory().getFileName();
    const auto folder = evidenceForTokens(splitTokens(folderName));

    std::set<juce::String> evidenceClasses = filename.classes;
    AutoTagRule rule = AutoTagRule::FilenameToken;
    juce::String reason, source = "filename";
    if (filename.compoundType.isNotEmpty())
    {
        reason = "filename compound " + quote(filename.compoundDetail);
        rule = AutoTagRule::FilenameCompound;
    }
    else if (! filename.details.empty())
        reason = "filename token " + quote(filename.details.front());
    else if (! metadataClasses.empty())
    {
        evidenceClasses = metadataClasses;
        rule = AutoTagRule::MetadataType;
        source = metadataSource;
        reason = "stored metadata type " + quote(metadataDetail) + " from " + metadataSource;
    }
    else if (! smartClasses.empty())
    {
        evidenceClasses = smartClasses;
        rule = AutoTagRule::SmartType;
        source = smartSource;
        reason = "resolved smart type " + quote(smartDetail) + " from " + smartSource;
    }
    else if (! acousticClasses.empty())
    {
        evidenceClasses = acousticClasses;
        rule = AutoTagRule::SmartType;
        source = acousticSource;
        reason = "acoustic classification " + quote(acousticDetail) + " from " + acousticSource;
    }

    if (filename.classes.size() > 1)
        return skip(in, "ambiguous-types", "the filename names more than one instrument",
                    { joinClasses(filename.classes) });
    if (filename.ambiguous808 && ! filename.classes.empty())
        return skip(in, "ambiguous-types", "808 is not enough to choose between kick and bass",
                    { "Bass", "Kick" });

    auto appendExtraTags = [&](AutoTagDecision& dec)
    {
        std::set<juce::String> alreadyProposed;
        for (const auto& p : dec.proposals)
            alreadyProposed.insert(p.canonicalType.toLowerCase());

        // 1. Gear (e.g. 909, 808, MPC)
        for (const auto& token : filenameTokens)
        {
            for (const auto& gear : gearAliases)
            {
                for (const auto& candidate : juce::StringArray::fromTokens(gear.tokens, "|", ""))
                {
                    if (token.equalsIgnoreCase(candidate))
                    {
                        if (alreadyProposed.insert(juce::String(gear.canonical).toLowerCase()).second)
                        {
                            AutoTagProposal p;
                            p.sampleId = in.sampleId;
                            p.filePath = in.filePath;
                            p.fileName = juce::File(in.filePath).getFileName();
                            p.canonicalType = gear.canonical;
                            const auto m = options.tagIds.find(gear.canonical);
                            p.tagId = m != options.tagIds.end() ? m->second : 0;
                            p.rule = AutoTagRule::Gear;
                            p.reason = "filename token " + quote(token);
                            p.source = "filename";
                            dec.proposals.push_back(std::move(p));
                        }
                        break;
                    }
                }
            }
        }

        // 2. Genre (from folder hierarchy and filename)
        juce::File parentDir = juce::File(in.filePath).getParentDirectory();
        int depth = 0;
        while (parentDir.getFullPathName().isNotEmpty() && parentDir != parentDir.getParentDirectory() && depth < 6)
        {
            const auto folderTokens = AutoTagRules::splitTokens(parentDir.getFileName());
            for (const auto& token : folderTokens)
            {
                for (const auto& genre : genreAliases)
                {
                    for (const auto& candidate : juce::StringArray::fromTokens(genre.tokens, "|", ""))
                    {
                        if (token.equalsIgnoreCase(candidate))
                        {
                            if (alreadyProposed.insert(juce::String(genre.canonical).toLowerCase()).second)
                            {
                                AutoTagProposal p;
                                p.sampleId = in.sampleId;
                                p.filePath = in.filePath;
                                p.fileName = juce::File(in.filePath).getFileName();
                                p.canonicalType = genre.canonical;
                                const auto m = options.tagIds.find(genre.canonical);
                                p.tagId = m != options.tagIds.end() ? m->second : 0;
                                p.rule = AutoTagRule::Genre;
                                p.reason = "folder " + quote(parentDir.getFileName());
                                p.source = "folder";
                                dec.proposals.push_back(std::move(p));
                            }
                            break;
                        }
                    }
                }
            }
            parentDir = parentDir.getParentDirectory();
            ++depth;
        }
    };

    if (! evidenceClasses.empty())
    {
        // A filename answer and a different metadata answer are a conflict, not a vote.
        std::set<juce::String> all = evidenceClasses;
        all.insert(metadataClasses.begin(), metadataClasses.end());
        all.insert(smartClasses.begin(), smartClasses.end());
        if (evidenceClasses.empty() && metadataClasses.empty())
            all.insert(acousticClasses.begin(), acousticClasses.end());
        if (all.size() > 1)
            return skip(in, "conflicting-types", "evidence points at different instrument types",
                        { joinClasses(all) });
        const auto proposed = *evidenceClasses.begin();
        if (in.typeOverride.has_value())
        {
            const auto& override = *in.typeOverride;
            if (override.cleared || override.notApplicable)
                return skip(in, "override-conflict",
                            "a manual Type override marks the type as unknown or not applicable");
            const auto manualEvidence = evidenceForTokens(splitTokens(override.textValue));
            juce::String manual;
            if (manualEvidence.classes.size() == 1) manual = *manualEvidence.classes.begin();
            else if (! manualEvidence.compoundType.isEmpty()) manual = manualEvidence.compoundType;
            if (manual.isEmpty() || manual != proposed)
                return skip(in, "override-conflict",
                            "the manual Type override " + quote(override.textValue)
                                + " differs from the proposal " + quote(proposed),
                            { proposed, manual.isNotEmpty() ? manual : override.textValue });
        }
        const auto mapped = options.tagIds.find(proposed);
        if (mapped == options.tagIds.end() || mapped->second <= 0)
            return skip(in, "tag-missing", "the catalog has no tag named " + quote(proposed), { proposed });
        AutoTagDecision decision;
        AutoTagProposal proposal;
        proposal.sampleId = in.sampleId;
        proposal.filePath = in.filePath;
        proposal.fileName = juce::File(in.filePath).getFileName();
        proposal.canonicalType = proposed;
        proposal.tagId = mapped->second;
        proposal.rule = rule;
        proposal.reason = reason;
        proposal.source = source;
        decision.proposals.push_back(proposal);
        decision.proposal = std::move(proposal);
        appendExtraTags(decision);
        return decision;
    }

    if (filename.ambiguous808)
        return skip(in, "ambiguous-types", "808 is not enough to choose between kick and bass",
                    { "Bass", "Kick" });
    if (staleEvidence)
        return skip(in, "stale-evidence", "stored metadata is stale or was produced by an older extractor");
    if (folderProvenance || ! folder.classes.empty())
        return skip(in, "folder-only", "the folder or pack name suggests a type but the file has no evidence of its own",
                    { joinClasses(folder.classes) });
    if (! filename.unsupported.empty())
    {
        std::vector<juce::String> names(filename.unsupported.begin(), filename.unsupported.end());
        return skip(in, "unsupported-type", "this instrument type has no supported AutoTag category", names);
    }

    AutoTagDecision extraDecision;
    appendExtraTags(extraDecision);
    if (! extraDecision.proposals.empty())
    {
        extraDecision.proposal = extraDecision.proposals.front();
        return extraDecision;
    }
    return skip(in, "no-evidence", "the filename is not enough to identify an instrument type");
}
} // namespace sonicator
