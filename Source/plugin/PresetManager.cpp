#include "PresetManager.h"
#include "ChordismProcessor.h"

namespace chordism
{
    juce::File PresetManager::userFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Chordism").getChildFile ("Presets");
    }

    void PresetManager::rescan()
    {
        entries.clear();
        const auto& factory = factoryPresets();
        for (int i = 0; i < (int) factory.size(); ++i)
            entries.add ({ factory[(size_t) i].category, factory[(size_t) i].name, i, {} });

        juce::Array<Entry> files;
        for (const auto& folder : userFolder().findChildFiles (juce::File::findDirectories, false))
            for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*" + fileExtension()))
                files.add ({ folder.getFileName(), f.getFileNameWithoutExtension(), -1, f });

        // Known categories in their order, then any others by name; names alphabetically.
        const auto cats = categories();
        std::sort (files.begin(), files.end(), [&cats] (const Entry& a, const Entry& b)
        {
            const int ia = cats.contains (a.category) ? cats.indexOf (a.category) : cats.size();
            const int ib = cats.contains (b.category) ? cats.indexOf (b.category) : cats.size();
            if (ia != ib)
                return ia < ib;
            if (a.category != b.category)
                return a.category.compareIgnoreCase (b.category) < 0;
            return a.name.compareNatural (b.name) < 0;
        });
        entries.addArray (files);
        scanned = true;
    }

    const juce::Array<PresetManager::Entry>& PresetManager::getEntries()
    {
        if (! scanned)
            rescan();
        return entries;
    }

    int PresetManager::current()
    {
        const auto& all = getEntries();
        for (int i = 0; i < all.size(); ++i)
            if (all[i].category == processor.getPresetCategory() && all[i].name == processor.getPresetName())
                return i;
        return -1;
    }

    bool PresetManager::load (int index)
    {
        const auto& all = getEntries();
        if (index < 0 || index >= all.size())
            return false;
        const auto& e = all.getReference (index);
        if (e.factory >= 0)
        {
            processor.setCurrentProgram (e.factory);
            return true;
        }
        const auto tree = readFile (e.file);
        if (! tree.isValid())
            return false;
        processor.applyPreset (tree, e.category, e.name);
        return true;
    }

    bool PresetManager::step (int delta)
    {
        const int n = getEntries().size();
        if (n == 0)
            return false;
        const int at = current();
        const int next = at < 0 ? (delta > 0 ? 0 : n - 1) : ((at + delta) % n + n) % n;
        return load (next);
    }

    juce::Result PresetManager::save (const juce::String& name, const juce::String& category)
    {
        const auto cleanName = juce::File::createLegalFileName (name.trim());
        const auto cleanCategory = juce::File::createLegalFileName (category.trim().isEmpty() ? juce::String ("User") : category.trim());
        if (cleanName.isEmpty())
            return juce::Result::fail ("A preset needs a name.");

        const auto folder = userFolder().getChildFile (cleanCategory);
        if (const auto made = folder.createDirectory(); made.failed())
            return made;
        auto tree = processor.presetState();
        tree.setProperty ("name", cleanName, nullptr);
        tree.setProperty ("category", cleanCategory, nullptr);
        const auto file = folder.getChildFile (cleanName + fileExtension());
        const auto xml = tree.createXml();
        if (xml == nullptr || ! xml->writeTo (file))
            return juce::Result::fail ("Couldn't write " + file.getFullPathName());

        processor.setPresetIdentity (cleanCategory, cleanName);
        rescan();
        return juce::Result::ok();
    }

    juce::ValueTree PresetManager::readFile (const juce::File& file)
    {
        if (auto xml = juce::XmlDocument::parse (file))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.hasType ("Chordism"))
                return tree;
        }
        return {};
    }

    juce::ValueTree PresetManager::factoryTree (int index)
    {
        juce::ValueTree tree ("Chordism");
        const auto& all = factoryPresets();
        if (index < 0 || index >= (int) all.size())
            return tree;
        const auto& p = all[(size_t) index];
        tree.setProperty ("name", p.name, nullptr);
        tree.setProperty ("category", p.category, nullptr);
        for (const auto& [id, value] : p.values)
        {
            juce::ValueTree param ("PARAM");
            param.setProperty ("id", id, nullptr);
            param.setProperty ("value", value, nullptr);
            tree.appendChild (param, nullptr);
        }
        return tree;
    }
}
