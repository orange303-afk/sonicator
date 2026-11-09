#include "ReasonPatch.h"
#include "core/database/SampleRepository.h"
#include "core/settings/AppSettings.h"
#include <algorithm>
#include <map>
#include <set>
namespace sonicator {
bool registerReasonPacks(SampleRepository& repo, const SettingsStore& settings, const juce::File& root)
{
    if (settings.load().reasonPacksAdded || !root.isDirectory()) return false;
    const auto roots=repo.roots();
    const bool covered=std::any_of(roots.begin(),roots.end(),[&](const auto& r)
    { return root==juce::File(r) || root.isAChildOf(juce::File(r)); });
    if (!covered) repo.addRoot(root.getFullPathName());
    settings.update([](AppSettings& s){s.reasonPacksAdded=true;});
    return !covered;
}
namespace {
juce::String operator""_utf8(const char* text, size_t length) { return juce::String::fromUTF8(text, (int)length); }
constexpr int64_t metadataLimit = 4 * 1024 * 1024;
constexpr size_t chunkLimit = 20000;
juce::uint32 be(const unsigned char* p)
{
    return ((juce::uint32)p[0]<<24) | ((juce::uint32)p[1]<<16) | ((juce::uint32)p[2]<<8) | p[3];
}
// REFE mixes versioned binary fields with length-prefixed UTF-8 strings. Only inspect
// the bounded metadata chunk, never BODY or the contents of a ReFill.
juce::StringArray strings(const juce::MemoryBlock& block)
{
    juce::StringArray out;
    const auto* p = static_cast<const unsigned char*>(block.getData());
    for (size_t i=0; i+4<=block.getSize(); ++i)
    {
        const auto n=be(p+i);
        if (n==0 || n>4096 || n>block.getSize()-i-4) continue;
        auto* s=reinterpret_cast<const char*>(p+i+4);
        bool printable=true;
        for (size_t j=0;j<n;++j) if ((unsigned char)s[j]<32 && s[j]!='\r' && s[j]!='\n') { printable=false; break; }
        if (!printable || !juce::CharPointer_UTF8::isValidString(s,(int)n)) continue;
        out.add(juce::String::fromUTF8(s,(int)n).trim()); i+=3+n;
    }
    return out;
}
juce::String basename(juce::String s)
{
    s=s.replaceCharacter('\\','/').replaceCharacter(':','/');
    return s.fromLastOccurrenceOf("/",false,false);
}
bool audioName(const juce::String& s)
{
    return s.endsWithIgnoreCase(".wav") || s.endsWithIgnoreCase(".aif")
        || s.endsWithIgnoreCase(".aiff") || s.endsWithIgnoreCase(".rx2");
}
juce::File packRoot(const juce::File& patch)
{
    auto folder=patch.getParentDirectory();
    for (int depth=0;depth<12;++depth)
    {
        if (folder.getChildFile("_samples").isDirectory() || folder.getParentDirectory().getFileName()=="Packs") return folder;
        auto parent=folder.getParentDirectory(); if (parent==folder) break; folder=parent;
    }
    return patch.getParentDirectory();
}
bool previewFolder(const juce::File& f)
{
    return f.getFileName().equalsIgnoreCase("_audio previews") || f.getFileName().equalsIgnoreCase("Audio Previews");
}
void resolve(ReasonPatch& result,const juce::File& patch)
{
    const auto root=packRoot(patch);
    std::map<juce::String,std::vector<juce::File>> files;
    std::vector<juce::File> pending {root}; size_t count=0; bool limited=false;
    while (!pending.empty() && !limited)
    {
        auto folder=pending.back(); pending.pop_back();
        if (folder.isSymbolicLink()) continue;
        for (const auto& entry: juce::RangedDirectoryIterator(folder,false,"*",juce::File::findFilesAndDirectories,juce::File::FollowSymlinks::no))
        {
            if (++count>chunkLimit) { limited=true; break; }
            const auto f=entry.getFile(); if (f.isSymbolicLink()) continue;
            if (f.isDirectory()) { if (!previewFolder(f)) pending.push_back(f); }
            else if (audioName(f.getFileName())) files[f.getFileName().toLowerCase()].push_back(f);
        }
    }
    for (auto& ref:result.references)
    {
        if (ref.refill.isNotEmpty()) continue;
        const auto& matches=files[ref.name.toLowerCase()];
        if (limited) ref.message="Превышен лимит поиска сэмплов в паке"_utf8;
        else if (matches.size()==1) { ref.file=matches.front(); ref.message.clear(); }
        else ref.message=matches.empty() ? "Сэмпл не найден в паке"_utf8 : "Неоднозначное имя сэмпла в паке"_utf8;
    }
}
}
bool isReasonPackPreview(const juce::File& file)
{
    auto folder=file;
    for (int depth=0;depth<32;++depth)
    {
        if (previewFolder(folder))
        {
            const auto root=packRoot(folder.getChildFile("placeholder"));
            return root!=folder && (root.getChildFile("_samples").isDirectory()
                || root.getParentDirectory().getFileName()=="Packs");
        }
        const auto parent=folder.getParentDirectory(); if (parent==folder) break; folder=parent;
    }
    return false;
}
ReasonPatch readReasonPatch(const juce::File& file, bool resolveSamples)
{
    ReasonPatch result; result.name=file.getFileNameWithoutExtension();
    juce::FileInputStream input(file);
    auto fail=[&](const char* message) { result.error=juce::String::fromUTF8(message); result.references.clear(); return result; };
    if (!input.openedOk()) return fail("Не удалось открыть Reason-файл");
    if (file.hasFileExtension("rfl"))
    {
        const char signature[]="Propellerheads ReFill File\x1a";
        char header[sizeof(signature)-1] {};
        if (input.read(header,sizeof(header))!=(int)sizeof(header) || std::memcmp(header,signature,sizeof(header))!=0)
            return fail("Некорректная сигнатура Reason ReFill");
        result.message="Reason ReFill: закрытый формат, содержимое недоступно"_utf8; return result;
    }
    if (file.hasFileExtension("repatch"))
    {
        if (input.getTotalLength()>metadataLimit) return fail("Reason XML превышает лимит чтения");
        const auto text=input.readEntireStreamAsString();
        // No DTD/external entity access for untrusted patches.
        if (text.containsIgnoreCase("<!DOCTYPE") || text.containsIgnoreCase("<!ENTITY")) return fail("Reason XML: DTD не поддерживается");
        juce::XmlDocument document(text); auto xml=document.getDocumentElement();
        if (!xml || !xml->hasTagName("JukeboxPatch"))
            return fail("Некорректный Reason repatch XML: нужен deviceProduct");
        const auto product=xml->getStringAttribute("deviceProduct",xml->getStringAttribute("deviceProductID"));
        if (product.isEmpty()) return fail("Некорректный Reason repatch XML: нужен deviceProduct");
        result.devices.add(product);
        result.message="Reason device patch: только метаданные, без аудио"_utf8; return result;
    }
    unsigned char header[12] {};
    if (input.read(header,12)!=12 || std::memcmp(header,"FORM",4)!=0 || std::memcmp(header+8,"PTCH",4)!=0)
        return fail("Некорректный Reason Combinator: ожидается FORM/PTCH");
    const int64_t end=8+(int64_t)be(header+4);
    if (end<12 || end>input.getTotalLength()) return fail("Обрезанный Reason IFF");
    struct Range { int64_t pos,end; juce::String type; int depth; };
    std::vector<Range> pending {{12,end,"PTCH",0}};
    size_t chunks=0; int64_t readBytes=0; std::set<juce::String> seen;
    while (!pending.empty())
    {
        auto range=pending.back(); pending.pop_back();
        while (range.pos<range.end)
        {
            if (++chunks>chunkLimit || range.end-range.pos<8) return fail("Некорректный Reason IFF: границы/лимит чанков");
            input.setPosition(range.pos); unsigned char h[8];
            if (input.read(h,8)!=8) return fail("Обрезанный Reason IFF");
            const auto tag=juce::String::fromUTF8((const char*)h,4);
            const int64_t size=be(h+4), start=range.pos+8, next=start+size+(size%2);
            if (next>range.end) return fail("Обрезанный чанк Reason IFF");
            if (tag=="FORM" || tag=="CAT ")
            {
                if (size<4) return fail("Некорректный контейнер Reason IFF");
                char typeBytes[4]; if (input.read(typeBytes,4)!=4) return fail("Обрезанный контейнер Reason IFF");
                auto type=juce::String::fromUTF8(typeBytes,4);
                const bool enter=(range.depth==0 && (type=="REFS" || type=="DEVL"))
                    || (range.type=="DEVL" && type=="DEVI");
                if (enter) pending.push_back({start+4,start+size,type,range.depth+1});
            }
            else if ((tag=="REFE" && range.type=="REFS") || (tag=="DESC" && (range.type=="PTCH" || range.type=="DEVI")))
            {
                readBytes+=size;
                if (size>metadataLimit || readBytes>metadataLimit) return fail("Reason metadata превышают лимит чтения");
                juce::MemoryBlock block;
                if (input.readIntoMemoryBlock(block,(size_t)size)!=(size_t)size) return fail("Обрезанные Reason metadata");
                const auto values=strings(block);
                if (tag=="DESC" && !values.isEmpty())
                {
                    if (range.depth==0) result.name=values[0]; else result.devices.addIfNotAlreadyThere(values[0]);
                }
                if (tag=="REFE")
                {
                    ReasonReference ref;
                    for (const auto& value:values)
                    {
                        const auto name=basename(value);
                        if (audioName(name) && ref.name.isEmpty()) ref.name=name;
                        if (name.endsWithIgnoreCase(".rfl")) ref.refill=name;
                    }
                    if (ref.name.isNotEmpty() || ref.refill.isNotEmpty())
                    {
                        const auto key=(ref.refill+"/"+ref.name).toLowerCase();
                        if (seen.insert(key).second)
                        {
                            ref.message=ref.refill.isNotEmpty() ? "нужен ReFill: "_utf8+ref.refill : "Сэмпл не найден в паке"_utf8;
                            result.references.push_back(std::move(ref));
                        }
                    }
                }
            }
            range.pos=next;
        }
    }
    result.message="Reason Combinator: только метаданные и локальные сэмплы"_utf8;
    if (resolveSamples && std::any_of(result.references.begin(),result.references.end(),[](const auto& r){return r.refill.isEmpty();})) resolve(result,file);
    return result;
}
}
