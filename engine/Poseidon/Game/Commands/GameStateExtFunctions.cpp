#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/World/World.hpp>

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

std::vector<ScriptFunction>& ScriptFunctions()
{
    static std::vector<ScriptFunction> functions;
    return functions;
}

int& NextSpawnId()
{
    static int id = 1;
    return id;
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

    BindActiveFunctionVariable(state, key);
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
    if (!ReadFunctionFile(source, body))
    {
        return GameValue(false);
    }
    return GameValue(StoreFunction(state, name, lifetime, "file", source, body));
}
} // namespace

namespace Poseidon
{
void ClearScriptFunctions()
{
    ScriptFunctions().clear();
    NextSpawnId() = 1;
}

void ClearScriptFunctions(const GameState* state)
{
    ClearFunctionsByLifetime(state, FunctionLifetime::Mission);
    NextSpawnId() = 1;
}

void ClearAddonScriptFunctions(const GameState* state)
{
    ClearFunctionsByLifetime(state, FunctionLifetime::Addon);
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
    return GameValue(static_cast<float>(NextSpawnId()++));
}
