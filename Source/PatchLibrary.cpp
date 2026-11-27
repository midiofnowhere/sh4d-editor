#include "PatchLibrary.h"

using namespace juce;
using namespace sh4d;

namespace
{
    File& folderOverride()
    {
        static File f;
        return f;
    }

    String sanitise (const String& s)
    {
        auto clean = File::createLegalFileName (s.trim());
        return clean.isEmpty() ? String ("Untitled") : clean;
    }

    bool readInfo (const File& f, PatchInfo& info, XmlElement** keep = nullptr)
    {
        std::unique_ptr<XmlElement> xml (XmlDocument::parse (f));

        if (xml == nullptr || ! xml->hasTagName ("SH4DPATCH"))
            return false;

        info.file     = f;
        info.name     = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
        info.category = xml->getStringAttribute ("category");
        info.bankMsb  = xml->getIntAttribute ("msb", kToneBankMsb);
        info.bankLsb  = xml->getIntAttribute ("lsb", kToneBankLsb);
        info.program  = xml->getIntAttribute ("pc", 0);

        if (keep != nullptr)
            *keep = xml.release();

        return true;
    }
}

File PatchLibrary::getFolder()
{
    File dir = folderOverride() != File() ? folderOverride()
             : File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("SH4dEditor").getChildFile ("Patches");
    dir.createDirectory();
    return dir;
}

void PatchLibrary::setFolderOverride (const File& f) { folderOverride() = f; }

Array<PatchInfo> PatchLibrary::scan()
{
    Array<PatchInfo> out;

    for (const auto& entry : RangedDirectoryIterator (getFolder(), false, "*.sh4dpatch"))
    {
        PatchInfo info;
        if (readInfo (entry.getFile(), info))
            out.add (info);
    }

    std::sort (out.begin(), out.end(), [] (const PatchInfo& a, const PatchInfo& b)
    {
        const int c = a.category.compareIgnoreCase (b.category);
        return c != 0 ? c < 0 : a.name.compareIgnoreCase (b.name) < 0;
    });

    return out;
}

bool PatchLibrary::save (const String& name, const String& category, const Values& values,
                         int bankMsb, int bankLsb, int program, File* writtenTo)
{
    XmlElement xml ("SH4DPATCH");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("name", name.trim().isEmpty() ? String ("Untitled") : name.trim());
    xml.setAttribute ("category", category.trim());
    xml.setAttribute ("msb", jlimit (0, 127, bankMsb));
    xml.setAttribute ("lsb", jlimit (0, 127, bankLsb));
    xml.setAttribute ("pc",  jlimit (0, 127, program));

    for (int i = 0; i < kNumCc; ++i)
    {
        auto* v = xml.createNewChildElement ("V");
        v->setAttribute ("cc", kCc[i].cc);
        v->setAttribute ("v", jlimit (0, 127, values[(size_t) i]));
    }

    const File target = getFolder().getChildFile (sanitise (name) + ".sh4dpatch");

    if (! xml.writeTo (target))
        return false;

    if (writtenTo != nullptr)
        *writtenTo = target;

    return true;
}

bool PatchLibrary::load (const File& f, Values& values, PatchInfo& info)
{
    XmlElement* raw = nullptr;
    if (! readInfo (f, info, &raw))
        return false;

    std::unique_ptr<XmlElement> xml (raw);

    for (auto* v : xml->getChildWithTagNameIterator ("V"))
    {
        const int cc = v->getIntAttribute ("cc", -1);

        for (int i = 0; i < kNumCc; ++i)
            if (kCc[i].cc == cc)
                values[(size_t) i] = jlimit (0, 127, v->getIntAttribute ("v", 0));
    }

    return true;
}

bool PatchLibrary::remove (const File& f)
{
    return f.existsAsFile() && (f.moveToTrash() || f.deleteFile());
}
