#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/World/World.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
enum class FunctionLifetime
{
    Addon,
    Mission,
};

struct ScriptFunction
{
    std::string key;
    std::string name;
    FunctionLifetime lifetime = FunctionLifetime::Mission;
    std::string type;
    RString source;
    RString body;
};

struct SpawnedScript
{
    int id = 0;
    LLink<Poseidon::Script> script;
};

std::vector<ScriptFunction>& ScriptFunctions()
{
    static std::vector<ScriptFunction> functions;
    return functions;
}

std::vector<SpawnedScript>& SpawnedScripts()
{
    static std::vector<SpawnedScript> scripts;
    return scripts;
}

int& NextSpawnId()
{
    static int id = 1;
    return id;
}

void PruneSpawnedScripts()
{
    std::vector<SpawnedScript>& scripts = SpawnedScripts();
    for (auto it = scripts.begin(); it != scripts.end();)
    {
        Poseidon::Script* script = it->script;
        if (!script)
        {
            it = scripts.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

SpawnedScript* FindSpawnedScript(int id)
{
    PruneSpawnedScripts();
    for (SpawnedScript& spawned : SpawnedScripts())
    {
        if (spawned.id == id)
        {
            return &spawned;
        }
    }
    return nullptr;
}

std::string GameStringToStdString(GameValuePar value)
{
    return std::string(((RString)(GameStringType)value).Data());
}

std::string NormalizeFunctionName(const std::string& name)
{
    RString normalized(name.c_str());
    normalized.Lower();
    return std::string(normalized.Data());
}

bool CheckFunctionName(const GameState* state, GameValuePar value)
{
    if (value.GetType() != GameString)
    {
        state->TypeError(GameString, value.GetType());
        return false;
    }
    RString name = value;
    return name.GetLength() > 0;
}

const char* FunctionLifetimeName(FunctionLifetime lifetime)
{
    return lifetime == FunctionLifetime::Addon ? "addon" : "mission";
}

RString StripLeadingSlash(RString value)
{
    if (value.GetLength() > 0 && (value[0] == '\\' || value[0] == '/'))
    {
        return value.Substring(1, value.GetLength());
    }
    return value;
}

RString StripTrailingSlash(RString value)
{
    if (value.GetLength() > 0 && (value[value.GetLength() - 1] == '\\' || value[value.GetLength() - 1] == '/'))
    {
        return value.Substring(0, value.GetLength() - 1);
    }
    return value;
}

RString LeafName(RString value)
{
    value = StripTrailingSlash(value);
    for (int i = value.GetLength() - 1; i >= 0; --i)
    {
        if (value[i] == '\\' || value[i] == '/')
        {
            return value.Substring(i + 1, value.GetLength());
        }
    }
    return value;
}

RString JoinPath(RString prefix, RString relative)
{
    relative = StripLeadingSlash(relative);
    if (prefix.GetLength() == 0)
    {
        return relative;
    }
    if (prefix[prefix.GetLength() - 1] == '\\' || prefix[prefix.GetLength() - 1] == '/')
    {
        return prefix + relative;
    }
    return prefix + RString("\\") + relative;
}

bool EqualsNoCase(RString left, RString right)
{
    left.Lower();
    right.Lower();
    return strcmp(left.Data(), right.Data()) == 0;
}

RString StripMatchingAddonRoot(RString source, RString addonPrefix)
{
    source = StripLeadingSlash(source);
    const RString addonName = LeafName(addonPrefix);
    if (addonName.GetLength() == 0)
    {
        return source;
    }

    for (int i = 0; i < source.GetLength(); ++i)
    {
        if (source[i] == '\\' || source[i] == '/')
        {
            const RString firstComponent = source.Substring(0, i);
            if (EqualsNoCase(firstComponent, addonName))
            {
                return source.Substring(i + 1, source.GetLength());
            }
            return source;
        }
    }

    return EqualsNoCase(source, addonName) ? RString() : source;
}

bool ReadExistingFunctionFile(RString source, RString& body)
{
    source = StripLeadingSlash(source);
    if (source.GetLength() == 0)
    {
        return false;
    }

    if (QIFStreamB::FileExist(source))
    {
        QIFStreamB in;
        in.AutoOpen(source);
        if (in.rest() <= 0)
        {
            body = RString();
            return true;
        }
        body = RString(in.act(), in.rest());
        return true;
    }

    std::ifstream in(source.Data(), std::ios::binary);
    if (!in.is_open())
    {
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.empty())
    {
        body = RString();
    }
    else
    {
        body = RString(text.c_str(), static_cast<int>(text.size()));
    }
    return true;
}

bool ReadFunctionFile(RString source, RString& body)
{
    QIFStreamB in;
    if (!OpenScript(in, source))
    {
        return false;
    }
    if (in.rest() <= 0)
    {
        body = RString();
        return true;
    }
    body = RString(in.act(), in.rest());
    return true;
}

bool ReadFunctionFile(RString source, FunctionLifetime lifetime, RString& body)
{
    if (lifetime == FunctionLifetime::Addon &&
        Poseidon::ReadAddonFunctionFile(source, Poseidon::CurrentAddonLifecyclePrefix(), body))
    {
        return true;
    }
    return ReadFunctionFile(source, body);
}

GameValue MakeCodeValue(RString body)
{
    return GameValue(new GameDataCode(body));
}

GameValue MakeFunctionInfo(const GameState* state, const ScriptFunction& function)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(5);
    array[0] = GameValue(function.name.c_str());
    array[1] = GameValue(FunctionLifetimeName(function.lifetime));
    array[2] = GameValue(function.type.c_str());
    array[3] = GameValue(function.source);
    array[4] = GameValue(function.body);
    return value;
}

ScriptFunction* FindScriptFunction(const std::string& key, FunctionLifetime lifetime)
{
    for (ScriptFunction& function : ScriptFunctions())
    {
        if (function.key == key && function.lifetime == lifetime)
        {
            return &function;
        }
    }
    return nullptr;
}

ScriptFunction* FindActiveScriptFunction(const std::string& key)
{
    if (ScriptFunction* function = FindScriptFunction(key, FunctionLifetime::Mission))
    {
        return function;
    }
    return FindScriptFunction(key, FunctionLifetime::Addon);
}

void DeleteFunctionVariable(const GameState* state, const std::string& key)
{
    const_cast<GameState*>(state)->VarDelete(key.c_str());
}

void BindActiveFunctionVariable(const GameState* state, const std::string& key)
{
    ScriptFunction* active = FindActiveScriptFunction(key);
    if (active)
    {
        const_cast<GameState*>(state)->VarSet(active->name.c_str(), MakeCodeValue(active->body), false);
    }
    else
    {
        DeleteFunctionVariable(state, key);
    }
}

bool KeyAlreadyQueued(const std::vector<std::string>& keys, const std::string& key)
{
    for (const std::string& existing : keys)
    {
        if (existing == key)
        {
            return true;
        }
    }
    return false;
}

void ClearFunctionsByLifetime(const GameState* state, FunctionLifetime lifetime)
{
    std::vector<std::string> changedKeys;
    std::vector<ScriptFunction>& functions = ScriptFunctions();
    for (auto it = functions.begin(); it != functions.end();)
    {
        if (it->lifetime == lifetime)
        {
            if (!KeyAlreadyQueued(changedKeys, it->key))
            {
                changedKeys.push_back(it->key);
            }
            it = functions.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (state)
    {
        for (const std::string& key : changedKeys)
        {
            BindActiveFunctionVariable(state, key);
        }
    }
}

bool StoreFunction(const GameState* state, const std::string& name, FunctionLifetime lifetime, const char* type,
                   RString source, RString body)
{
    if (body.GetLength() == 0)
    {
        return false;
    }

    const std::string key = NormalizeFunctionName(name);
    ScriptFunction* existing = FindScriptFunction(key, lifetime);
    if (existing)
    {
        existing->key = key;
        existing->name = name;
        existing->lifetime = lifetime;
        existing->type = type;
        existing->source = source;
        existing->body = body;
    }
    else
    {
        ScriptFunction function;
        function.key = key;
        function.name = name;
        function.lifetime = lifetime;
        function.type = type;
        function.source = source;
        function.body = body;
        ScriptFunctions().push_back(function);
    }

    if (state)
    {
        BindActiveFunctionVariable(state, key);
    }
    return true;
}

GameValue RegisterFunctionWithLifetime(const GameState* state, GameValuePar arg, FunctionLifetime lifetime)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return GameValue(false);
    }

    const GameArrayType& array = arg;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return GameValue(false);
    }
    if (!CheckFunctionName(state, array[0]))
    {
        return GameValue(false);
    }
    if (array[1].GetType() != GameString && array[1].GetType() != GameCode)
    {
        state->TypeError(GameString | GameCode, array[1].GetType());
        return GameValue(false);
    }

    const std::string name = GameStringToStdString(array[0]);
    if (array[1].GetType() == GameCode)
    {
        return GameValue(StoreFunction(state, name, lifetime, "code", RString(), (RString)(GameStringType)array[1]));
    }

    RString source = array[1];
    RString body;
    if (!ReadFunctionFile(source, lifetime, body))
    {
        return GameValue(false);
    }
    return GameValue(StoreFunction(state, name, lifetime, "file", source, body));
}
} // namespace

namespace Poseidon
{
bool ReadAddonFunctionFile(RString source, RString addonPrefix, RString& body)
{
    if (addonPrefix.GetLength() == 0)
    {
        return false;
    }

    source = StripLeadingSlash(source);
    const RString strippedSource = StripMatchingAddonRoot(source, addonPrefix);
    const RString leaf = LeafName(source);
    RString candidates[] = {
        JoinPath(addonPrefix, source),
        JoinPath(addonPrefix, strippedSource),
        JoinPath(addonPrefix, leaf),
        source,
        strippedSource,
        leaf,
    };
    for (const RString& candidate : candidates)
    {
        if (ReadExistingFunctionFile(candidate, body))
        {
            return true;
        }
    }
    return false;
}

void ClearScriptFunctions()
{
    ScriptFunctions().clear();
    SpawnedScripts().clear();
    NextSpawnId() = 1;
}

void ClearScriptFunctions(const GameState* state)
{
    ClearFunctionsByLifetime(state, FunctionLifetime::Mission);
    SpawnedScripts().clear();
    NextSpawnId() = 1;
}

void ClearAddonScriptFunctions(const GameState* state)
{
    ClearFunctionsByLifetime(state, FunctionLifetime::Addon);
}

bool RegisterAddonScriptFunction(const GameState* state, RString name, RString source, RString addonPrefix)
{
    if (name.GetLength() == 0 || source.GetLength() == 0)
    {
        return false;
    }

    RString body;
    if (!ReadAddonFunctionFile(source, addonPrefix, body))
    {
        return false;
    }
    return StoreFunction(state, name.Data(), FunctionLifetime::Addon, "file", source, body);
}

void RebindScriptFunctions(const GameState* state)
{
    if (!state)
    {
        return;
    }

    std::vector<std::string> changedKeys;
    for (const ScriptFunction& function : ScriptFunctions())
    {
        if (!KeyAlreadyQueued(changedKeys, function.key))
        {
            changedKeys.push_back(function.key);
        }
    }
    for (const std::string& key : changedKeys)
    {
        BindActiveFunctionVariable(state, key);
    }
}
} // namespace Poseidon

GameValue FunctionRegister(const GameState* state, GameValuePar arg)
{
    return RegisterFunctionWithLifetime(state, arg, FunctionLifetime::Mission);
}

GameValue FunctionRegisterAddon(const GameState* state, GameValuePar arg)
{
    return RegisterFunctionWithLifetime(state, arg, FunctionLifetime::Addon);
}

GameValue FunctionExists(const GameState* state, GameValuePar arg)
{
    if (!CheckFunctionName(state, arg))
    {
        return GameValue(false);
    }
    return GameValue(FindActiveScriptFunction(NormalizeFunctionName(GameStringToStdString(arg))) != nullptr);
}

GameValue FunctionGet(const GameState* state, GameValuePar arg)
{
    if (!CheckFunctionName(state, arg))
    {
        return state->CreateGameValue(GameArray);
    }

    ScriptFunction* function = FindActiveScriptFunction(NormalizeFunctionName(GameStringToStdString(arg)));
    if (!function)
    {
        return state->CreateGameValue(GameArray);
    }
    return MakeFunctionInfo(state, *function);
}

GameValue FunctionList(const GameState* state)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    const std::vector<ScriptFunction>& functions = ScriptFunctions();
    array.Realloc(static_cast<int>(functions.size()));
    for (const ScriptFunction& function : functions)
    {
        array.Add(MakeFunctionInfo(state, function));
    }
    return value;
}

GameValue FunctionUnregister(const GameState* state, GameValuePar arg)
{
    if (!CheckFunctionName(state, arg))
    {
        return GameValue(false);
    }

    const std::string key = NormalizeFunctionName(GameStringToStdString(arg));
    std::vector<ScriptFunction>& functions = ScriptFunctions();
    for (auto it = functions.begin(); it != functions.end(); ++it)
    {
        if (it->key == key && it->lifetime == FunctionLifetime::Mission)
        {
            functions.erase(it);
            BindActiveFunctionVariable(state, key);
            return GameValue(true);
        }
    }
    return GameValue(false);
}

GameValue FunctionUnregisterAddon(const GameState* state, GameValuePar arg)
{
    if (!CheckFunctionName(state, arg))
    {
        return GameValue(false);
    }

    const std::string key = NormalizeFunctionName(GameStringToStdString(arg));
    std::vector<ScriptFunction>& functions = ScriptFunctions();
    for (auto it = functions.begin(); it != functions.end(); ++it)
    {
        if (it->key == key && it->lifetime == FunctionLifetime::Addon)
        {
            functions.erase(it);
            BindActiveFunctionVariable(state, key);
            return GameValue(true);
        }
    }
    return GameValue(false);
}

GameValue FunctionClear(const GameState* state)
{
    Poseidon::ClearScriptFunctions(state);
    return GameValue(true);
}

GameValue FunctionClearAddon(const GameState* state)
{
    Poseidon::ClearAddonScriptFunctions(state);
    return GameValue(true);
}

GameValue FunctionSpawn(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    if (oper2.GetType() != GameString && oper2.GetType() != GameCode)
    {
        state->TypeError(GameString | GameCode, oper2.GetType());
        return GameValue(-1.0f);
    }
    if (!Poseidon::GWorld)
    {
        return GameValue(-1.0f);
    }

    RString body = oper2;
    AutoArray<RString> lines;
    lines.Add(body);
    Poseidon::Script* script = new Poseidon::Script(lines, oper1);
    Poseidon::GWorld->AddScript(script);

    SpawnedScript spawned;
    spawned.id = NextSpawnId()++;
    spawned.script = script;
    SpawnedScripts().push_back(spawned);
    return GameValue(static_cast<float>(spawned.id));
}

GameValue FunctionScriptDone(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameScalar)
    {
        state->TypeError(GameScalar, arg.GetType());
        return GameValue(true);
    }

    const int id = toInt(static_cast<float>(arg));
    SpawnedScript* spawned = FindSpawnedScript(id);
    if (!spawned)
    {
        return GameValue(true);
    }

    Poseidon::Script* script = spawned->script;
    return GameValue(!script || script->IsTerminated());
}

GameValue FunctionTerminate(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameScalar)
    {
        state->TypeError(GameScalar, arg.GetType());
        return GameValue(false);
    }

    const int id = toInt(static_cast<float>(arg));
    SpawnedScript* spawned = FindSpawnedScript(id);
    if (!spawned)
    {
        return GameValue(false);
    }

    Poseidon::Script* script = spawned->script;
    if (!script)
    {
        PruneSpawnedScripts();
        return GameValue(false);
    }

    script->Exit();
    if (Poseidon::GWorld)
    {
        Poseidon::GWorld->TerminateScript(script);
    }
    PruneSpawnedScripts();
    return GameValue(true);
}
