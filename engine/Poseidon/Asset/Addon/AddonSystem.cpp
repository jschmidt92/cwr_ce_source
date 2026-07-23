#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Asset/Addon/AddonSystem.hpp>
#include <Poseidon/Asset/Addon/AddonInfo.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <filesystem>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <windows.h>
#endif

extern Poseidon::ParamFile Pars;

#include <Poseidon/Core/Version.hpp>

namespace Poseidon
{
void ClearAddonLifecycleHandlers();
void ClearAddonScriptFunctions(const GameState* state);
int RegisterAddonLifecycleHandler(const char* addon, const char* phase, RString body, bool inlineCode);
int RegisterAddonLifecycleHandler(const char* addon, const char* phase, RString body, bool inlineCode,
                                  RString addonPrefix);

AddonSystem::AddonInfoMap AddonSystem::s_addonRegistry;
AutoArray<SRef<ParamFile>> AddonSystem::s_addonConfigs;

namespace
{
struct AddonLifecycleClass
{
    const char* configClass;
    const char* phase;
    const char* serverPhase;
    const char* clientPhase;
};

const AddonLifecycleClass AddonLifecycleClasses[] = {
    {"Extended_PreInit_EventHandlers", "preInit", "serverPreInit", "playerLocalPreInit"},
    {"Extended_Init_EventHandlers", "init", "serverInit", "playerLocalInit"},
    {"Extended_PostInit_EventHandlers", "postInit", "serverPostInit", "playerLocalPostInit"},
    {"Extended_ServerInit_EventHandlers", "serverInit", "serverInit", nullptr},
    {"Extended_ServerPostInit_EventHandlers", "serverPostInit", "serverPostInit", nullptr},
    {"Extended_PlayerLocalInit_EventHandlers", "playerLocalInit", nullptr, "playerLocalInit"},
    {"Extended_PlayerLocalPostInit_EventHandlers", "playerLocalPostInit", nullptr, "playerLocalPostInit"},
    {"Extended_PlayerServerInit_EventHandlers", "playerServerInit", "playerServerInit", nullptr},
    {"Extended_JIPInit_EventHandlers", "jipInit", nullptr, "jipInit"},
};

bool ResolveAddonFile(RStringB dir, const char* name, RString& resolvedPath)
{
    resolvedPath = dir + RString(name);
    if (QIFStreamB::FileExist(resolvedPath))
        return true;

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dirPath(dir.Data());
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec))
        return false;

    for (const auto& entry : fs::directory_iterator(dirPath, ec))
    {
        if (ec)
            return false;
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc))
            continue;
        const std::string leaf = entry.path().filename().string();
        if (stricmp(leaf.c_str(), name) != 0)
            continue;
        resolvedPath = entry.path().string().c_str();
        return true;
    }

    return false;
}

bool LooksLikeScriptPath(RStringB value)
{
    const char* text = value;
    if (!text || *text == 0)
        return false;

    for (const char* pos = text; *pos; ++pos)
    {
        if (*pos == ' ' || *pos == '\t' || *pos == ';' || *pos == '{' || *pos == '}')
            return false;
    }

    const char* dot = strrchr(text, '.');
    return dot && (stricmp(dot, ".sqf") == 0 || stricmp(dot, ".sqs") == 0);
}

RString GetConfigAddonPrefix(const ParamEntry& config)
{
    const ParamEntry* cfg = config.FindEntry("CfgPatches");
    if (!cfg)
        return RString();

    RString addonName;
    for (int i = 0; i < cfg->GetEntryCount(); ++i)
    {
        const ParamEntry& entry = cfg->GetEntry(i);
        if (entry.IsClass())
        {
            addonName = entry.GetName();
            break;
        }
    }
    if (addonName.GetLength() == 0)
        return RString();

    const AddonInfo* info = AddonSystem::FindAddonInfo(addonName);
    return info ? info->GetPrefix() : RString();
}

RString GetTextValue(const ParamEntry& config, const char* name, RString fallback = RString())
{
    const ParamEntry* entry = config.FindEntry(name);
    if (!entry || !entry->IsTextValue())
    {
        return fallback;
    }
    return entry->GetValue();
}

bool GetIntValue(const ParamEntry& config, const char* name, int& value)
{
    const ParamEntry* entry = config.FindEntry(name);
    if (!entry)
    {
        return false;
    }
    value = entry->GetInt();
    return true;
}

RString JoinFunctionPath(RString dir, RString file)
{
    if (dir.GetLength() == 0)
    {
        return file;
    }
    if (file.GetLength() == 0)
    {
        return dir;
    }

    const char last = dir[dir.GetLength() - 1];
    if (last == '\\' || last == '/')
    {
        return dir + file;
    }
    return dir + RString("\\") + file;
}

RString FunctionClassFileName(const ParamEntry& functionClass)
{
    RString fileName = GetTextValue(functionClass, "file");
    if (fileName.GetLength() > 0)
    {
        return fileName;
    }
    return RString("fn_") + functionClass.GetName() + RString(".sqf");
}

RString BuildFunctionName(RString tag, RString functionName)
{
    return tag + RString("_fnc_") + functionName;
}

RString GetAddonNameFromRoot(const ParamEntry& root)
{
    const ParamEntry* cfg = root.FindEntry("CfgPatches");
    if (!cfg)
    {
        return RString();
    }
    for (int i = 0; i < cfg->GetEntryCount(); ++i)
    {
        const ParamEntry& entry = cfg->GetEntry(i);
        if (entry.IsClass())
        {
            return entry.GetName();
        }
    }
    return RString();
}

void RegisterAddonFunctionConfig(const ParamEntry& root)
{
    const ParamEntry* cfgFunctions = root.FindEntry("CfgFunctions");
    if (!cfgFunctions)
    {
        return;
    }

    const RString addonPrefix = GetConfigAddonPrefix(root);
    if (addonPrefix.GetLength() == 0)
    {
        LOG_WARN(Config, "CfgFunctions in addon '{}' has no addon prefix", (const char*)GetAddonNameFromRoot(root));
    }

    for (int tagIndex = 0; tagIndex < cfgFunctions->GetEntryCount(); ++tagIndex)
    {
        const ParamEntry& tagClass = cfgFunctions->GetEntry(tagIndex);
        if (!tagClass.IsClass())
        {
            continue;
        }

        RString tag = GetTextValue(tagClass, "tag", tagClass.GetName());
        RString tagFile = GetTextValue(tagClass, "file");
        int tagRecompile = 0;
        GetIntValue(tagClass, "recompile", tagRecompile);

        for (int categoryIndex = 0; categoryIndex < tagClass.GetEntryCount(); ++categoryIndex)
        {
            const ParamEntry& categoryClass = tagClass.GetEntry(categoryIndex);
            if (!categoryClass.IsClass())
            {
                continue;
            }

            RString categoryFile = GetTextValue(categoryClass, "file", tagFile);
            int categoryRecompile = tagRecompile;
            GetIntValue(categoryClass, "recompile", categoryRecompile);

            for (int functionIndex = 0; functionIndex < categoryClass.GetEntryCount(); ++functionIndex)
            {
                const ParamEntry& functionClass = categoryClass.GetEntry(functionIndex);
                if (!functionClass.IsClass())
                {
                    continue;
                }

                int functionRecompile = categoryRecompile;
                GetIntValue(functionClass, "recompile", functionRecompile);
                if (functionRecompile)
                {
                    LOG_WARN(Config, "CfgFunctions recompile ignored for {}_fnc_{}", (const char*)tag,
                             (const char*)functionClass.GetName());
                }

                const RString source = JoinFunctionPath(categoryFile, FunctionClassFileName(functionClass));
                const RString name = BuildFunctionName(tag, functionClass.GetName());
                if (!RegisterAddonScriptFunction(GWorld ? GWorld->GetGameState() : nullptr, name, source, addonPrefix))
                {
                    LOG_WARN(Config, "CfgFunctions failed to register {} from '{}'", (const char*)name,
                             (const char*)source);
                }
                else
                {
                    LOG_DEBUG(Config, "CfgFunctions registered {} from '{}'", (const char*)name, (const char*)source);
                }
            }
        }
    }
}

void RegisterAddonLifecycleConfigValue(const ParamEntry& handlerClass, const char* valueName, const char* phase,
                                       RString addonPrefix)
{
    if (!phase)
        return;

    const ParamEntry* init = handlerClass.FindEntry(valueName);
    if (!init || !init->IsTextValue())
        return;

    RString body = init->GetValue();
    if (body.GetLength() == 0)
        return;

    const bool inlineCode = !LooksLikeScriptPath(body);
    RegisterAddonLifecycleHandler(handlerClass.GetName(), phase, body, inlineCode, addonPrefix);
}

void RegisterAddonLifecycleConfigClass(const ParamEntry& root, const AddonLifecycleClass& lifecycleClass,
                                       RString addonPrefix)
{
    const ParamEntry* handlers = root.FindEntry(lifecycleClass.configClass);
    if (!handlers)
        return;

    for (int i = 0; i < handlers->GetEntryCount(); ++i)
    {
        const ParamEntry& handlerClass = handlers->GetEntry(i);
        if (!handlerClass.IsClass())
            continue;

        RegisterAddonLifecycleConfigValue(handlerClass, "init", lifecycleClass.phase, addonPrefix);
        RegisterAddonLifecycleConfigValue(handlerClass, "serverInit", lifecycleClass.serverPhase, addonPrefix);
        RegisterAddonLifecycleConfigValue(handlerClass, "clientInit", lifecycleClass.clientPhase, addonPrefix);
    }
}

void RegisterAddonLifecycleConfig(const ParamEntry& root)
{
    const RString addonPrefix = GetConfigAddonPrefix(root);
    for (const AddonLifecycleClass& lifecycleClass : AddonLifecycleClasses)
    {
        RegisterAddonLifecycleConfigClass(root, lifecycleClass, addonPrefix);
    }
}
} // namespace

void AddonSystem::RegisterAddon(RString name, RString prefix)
{
    s_addonRegistry.Add(AddonInfo(name, prefix));
}

const AddonInfo* AddonSystem::FindAddonInfo(RString name)
{
    const AddonInfo& info = s_addonRegistry[name];
    return s_addonRegistry.IsNull(info) ? nullptr : &info;
}

void AddonSystem::ClearRegistry()
{
    s_addonRegistry.Clear();
    ClearAddonLifecycleHandlers();
}

void AddonSystem::LoadAddon(const char* addon)
{
    const AddonInfo* info = FindAddonInfo(addon);
    if (!info)
    {
        LOG_DEBUG(Config, "Cannot load addon {} - addon does not exist", addon);
        return;
    }
}

void AddonSystem::UnloadAddon(const char* addon)
{
    const AddonInfo* info = FindAddonInfo(addon);
    if (!info)
    {
        LOG_DEBUG(Config, "Cannot unload addon {} - addon does not exist", addon);
        return;
    }
    for (int i = 0; i < GFileBanks.Size(); i++)
    {
        QFBank& bank = GFileBanks[i];
        if (stricmp(bank.GetPrefix(), info->GetPrefix()) == 0)
            GFileServer->FlushBank(&bank);
    }
}

void AddonSystem::UnlockAddonCallback(const AddonInfo& addon, AddonInfoMap* map, void* context)
{
    RString prefix = addon.GetPrefix();
    GFileBanks.Unlock(prefix);
}

void AddonSystem::MarkAddonLockableCallback(const AddonInfo& addon, AddonInfoMap* map, void* context)
{
    RString prefix = addon.GetPrefix();
    GFileBanks.SetLockable(prefix, true);
}

void AddonSystem::UnlockAllAddons()
{
    for (AddonInfoMap::Iterator it(s_addonRegistry); it; ++it)
        UnlockAddonCallback(*it, &s_addonRegistry, nullptr);
}

void AddonSystem::MarkAllAddonsLockable()
{
    for (AddonInfoMap::Iterator it(s_addonRegistry); it; ++it)
        MarkAddonLockableCallback(*it, &s_addonRegistry, nullptr);
}

RString AddonSystem::CheckAddonName(const ParamEntry& addon)
{
    const ParamEntry* patches = addon.FindEntry("CfgPatches");
    if (!patches || patches->GetEntryCount() == 0)
        return RString();
    return patches->GetEntry(0).GetName();
}

bool AddonSystem::CheckVersion(const RString& prefix, const ParamEntry& addon)
{
    const ParamEntry* patches = addon.FindEntry("CfgPatches");
    if (!patches || patches->GetEntryCount() == 0)
        return false;

    RString strVersion = GetAppVersion();
    int version = VersionToInt(strVersion);

    for (int i = 0; i < patches->GetEntryCount(); i++)
    {
        const ParamEntry& patch = patches->GetEntry(i);
        const ParamEntry* entry = patch.FindEntry("requiredVersion");
        if (entry)
        {
            RString required = *entry;
            if (VersionToInt(required) > version)
            {
                WarningMessage("Addon '%s' requires version %s or higher", (const char*)patch.GetName(),
                               (const char*)required);
                return false;
            }
        }
        const AddonInfo& info = s_addonRegistry[patch.GetName()];
        if (s_addonRegistry.NotNull(info))
            RptF("Conflicting addon %s in '%s', previous definition in '%s'", (const char*)patch.GetName(),
                 (const char*)prefix, (const char*)info.GetPrefix());
        else
            s_addonRegistry.Add(AddonInfo(patch.GetName(), prefix));
    }

    return true;
}

bool AddonSystem::ParseAddonConfig(const RString& prefixPath)
{
    RString filename;

    filename = prefixPath + RString("config.cpp");
    if (QIFStreamB::FileExist(filename))
    {
        SRef<ParamFile> addon = new ParamFile;
        if (QIFStream::FileExists(filename))
        {
            RString folderPath;
            int n = prefixPath.GetLength();
            DoAssert(prefixPath[n - 1] == '\\' || prefixPath[n - 1] == '/');
            folderPath = prefixPath.Substring(0, n - 1);
            char buf[1024];
            GetCurrentDirectory(sizeof(buf), buf);
#ifndef _WIN32
            unixPath((char*)(const char*)folderPath);
#endif
            SetCurrentDirectory(folderPath);
            addon->Parse("config.cpp");
            SetCurrentDirectory(buf);
        }
        else
        {
            addon->Parse(filename);
        }

        if (CheckVersion(prefixPath, *addon))
        {
            addon->SetOwner(CheckAddonName(*addon));
            s_addonConfigs.Add(addon);
        }
        else
        {
            return false;
        }
    }
    else
    {
        filename = prefixPath + RString("config.bin");
        if (QIFStreamB::FileExist(filename))
        {
            SRef<ParamFile> addon = new ParamFile;
            addon->ParseBin(filename);
            if (CheckVersion(prefixPath, *addon))
            {
                addon->SetOwner(CheckAddonName(*addon));
                s_addonConfigs.Add(addon);
            }
            else
            {
                return false;
            }
        }
    }

    if (ResolveAddonFile(prefixPath, "stringtable.csv", filename))
        LoadStringtable("Global", filename, 0, false);

    return true;
}

RString AddonSystem::GetAddonName(const ParamFile& config)
{
    const ParamEntry* cfg = config.FindEntry("CfgPatches");
    if (cfg)
    {
        for (int i = 0; i < cfg->GetEntryCount(); i++)
        {
            const ParamEntry& entry = cfg->GetEntry(i);
            if (!entry.IsClass())
                continue;
            return entry.GetName();
        }
    }
    return "";
}

const ParamFile* AddonSystem::FindAddonConfig(RStringB name)
{
    for (int i = 0; i < s_addonConfigs.Size(); i++)
    {
        const ParamFile& config = *s_addonConfigs.Get(i);
        const ParamEntry* cfg = config.FindEntry("CfgPatches");
        if (cfg)
        {
            for (int j = 0; j < cfg->GetEntryCount(); j++)
            {
                const ParamEntry& entry = cfg->GetEntry(j);
                if (!entry.IsClass())
                    continue;
                if (!strcmpi(entry.GetName(), name))
                    return &config;
            }
        }
    }
    return nullptr;
}

bool AddonSystem::IsPreloadedAddon(RStringB name)
{
    const ParamEntry& def = Pars >> "CfgAddons" >> "PreloadAddons";
    for (int c = 0; c < def.GetEntryCount(); c++)
    {
        const ParamEntry& cc = def.GetEntry(c);
        if (!cc.IsClass())
            continue;
        if (!cc.FindEntry("list"))
            continue;
        const ParamEntry& cl = cc >> "list";
        for (int i = 0; i < cl.GetSize(); i++)
        {
            RStringB addon = cl[i];
            if (!strcmpi(name, addon))
                return true;
        }
    }
    return false;
}

void AddonDependency::Resolved(const AddonDependency& dep)
{
    for (int i = 0; i < dependsOn.Size(); i++)
    {
        if (dependsOn[i] != dep.addon)
            continue;
        dependsOn.Delete(i--);
    }
}

void AddonSystem::ParseAllAddonConfigs()
{
    AutoArray<AddonDependency> dependencies;

    for (int i = 0; i < s_addonConfigs.Size(); i++)
    {
        const ParamFile& config = *s_addonConfigs.Get(i);
        AddonDependency& dep = dependencies.Append();
        dep.addon = &config;
        dep.preloaded = false;

        const ParamEntry* cfg = config.FindEntry("CfgPatches");
        if (cfg)
        {
            for (int j = 0; j < cfg->GetEntryCount(); j++)
            {
                const ParamEntry& entry = cfg->GetEntry(j);
                if (!entry.IsClass())
                    continue;
                dep.preloaded = IsPreloadedAddon(entry.GetName());
                const ParamEntry* required = entry.FindEntry("requiredAddons");
                if (!required)
                    continue;
                for (int k = 0; k < required->GetSize(); k++)
                {
                    RStringB requiredName = (*required)[k];
                    const ParamFile* file = FindAddonConfig(requiredName);
                    if (!file)
                    {
                        WarningMessage("Addon '%s' requires addon '%s'", (const char*)GetAddonName(config),
                                       (const char*)requiredName);
                        continue;
                    }
                    dep.dependsOn.Add(file);
                }
            }
        }
    }

    AutoArray<AddonDependency> resolved;
    while (dependencies.Size() > 0)
    {
        bool someResolved = false;
        for (int preloaded = 1; preloaded >= 0; preloaded--)
        {
            for (int i = 0; i < dependencies.Size(); i++)
            {
                const AddonDependency& dep = dependencies[i];
                if ((int)dep.preloaded != preloaded)
                    continue;
                if (dep.dependsOn.Size() > 0)
                    continue;
                resolved.Add(dep);
                for (int j = 0; j < dependencies.Size(); j++)
                    dependencies[j].Resolved(dep);
                dependencies.Delete(i--);
                someResolved = true;
            }
        }
        if (!someResolved)
        {
            RStringB addonName = GetAddonName(*dependencies[0].addon);
            ErrorMessage("Circular addon dependency in '%s'", (const char*)addonName);
            break;
        }
    }

    for (int i = 0; i < resolved.Size(); i++)
    {
        Log("Parsing addon config '%s' %s", (const char*)GetAddonName(*resolved[i].addon),
            resolved[i].preloaded ? "(preloaded)" : "");
        Pars.Update(*resolved[i].addon);
    }

    Pars.SetFile(&Pars);
    ClearAddonScriptFunctions(GWorld ? GWorld->GetGameState() : nullptr);
    ClearAddonLifecycleHandlers();
    for (int i = 0; i < resolved.Size(); i++)
    {
        RegisterAddonFunctionConfig(*resolved[i].addon);
        RegisterAddonLifecycleConfig(*resolved[i].addon);
    }
    RebindScriptFunctions(GWorld ? GWorld->GetGameState() : nullptr);
}

void AddonSystem::ClearAddonConfigs()
{
    s_addonConfigs.Clear();
}

} // namespace Poseidon
