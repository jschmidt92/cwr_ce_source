#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/World/World.hpp>

#include <string>
#include <vector>

namespace
{
struct MissionPhaseHandler
{
    int id = 0;
    std::string addon;
    std::string phase;
    bool inlineCode = false;
    RString addonPrefix;
    RString body;
};

std::string GameStringToStdString(GameValuePar value)
{
    return std::string(((RString)(GameStringType)value).Data());
}

std::vector<MissionPhaseHandler>& MissionPhaseHandlers()
{
    static std::vector<MissionPhaseHandler> handlers;
    return handlers;
}

std::vector<MissionPhaseHandler>& AddonLifecycleHandlers()
{
    static std::vector<MissionPhaseHandler> handlers;
    return handlers;
}

int& NextMissionPhaseHandlerId()
{
    static int id = 1;
    return id;
}

int& NextAddonLifecycleHandlerId()
{
    static int id = 1;
    return id;
}

RString& ActiveAddonLifecyclePrefix()
{
    static RString prefix;
    return prefix;
}

class AddonLifecyclePrefixScope
{
  public:
    explicit AddonLifecyclePrefixScope(RString prefix) : _previous(ActiveAddonLifecyclePrefix())
    {
        ActiveAddonLifecyclePrefix() = prefix;
    }

    ~AddonLifecyclePrefixScope()
    {
        ActiveAddonLifecyclePrefix() = _previous;
    }

  private:
    RString _previous;
};

bool CheckMissionPhaseOnArg(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return false;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (array[1].GetType() != GameString && array[1].GetType() != GameCode)
    {
        state->TypeError(GameString | GameCode, array[1].GetType());
        return false;
    }
    return true;
}

bool CheckAddonLifecycleRegisterArg(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return false;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (array[1].GetType() != GameArray)
    {
        state->TypeError(GameArray, array[1].GetType());
        return false;
    }
    return true;
}

bool CheckAddonLifecycleEntryArg(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return false;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (array[1].GetType() != GameString && array[1].GetType() != GameCode)
    {
        state->TypeError(GameString | GameCode, array[1].GetType());
        return false;
    }
    return true;
}

bool ParseMissionPhaseHandlerTarget(GameValuePar arg, MissionPhaseHandler& handler)
{
    if (arg.GetType() == GameString || arg.GetType() == GameCode)
    {
        handler.inlineCode = arg.GetType() == GameCode;
        handler.body = (RString)(GameStringType)arg;
        return handler.body.GetLength() > 0;
    }
    return false;
}

GameValue MakeMissionPhaseArgs(const GameState* state, const char* phase, GameValuePar argument)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(2);
    array[0] = GameValue(phase);
    array[1] = argument;
    return value;
}

GameValue MakeMissionPhaseHandlerInfo(const GameState* state, const MissionPhaseHandler& handler)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(4);
    array[0] = GameValue(static_cast<float>(handler.id));
    array[1] = GameValue(handler.phase.c_str());
    array[2] = GameValue(handler.inlineCode ? "code" : "script");
    array[3] = GameValue(handler.body);
    return value;
}

GameValue MakeAddonLifecycleHandlerInfo(const GameState* state, const MissionPhaseHandler& handler)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(5);
    array[0] = GameValue(static_cast<float>(handler.id));
    array[1] = GameValue(handler.addon.c_str());
    array[2] = GameValue(handler.phase.c_str());
    array[3] = GameValue(handler.inlineCode ? "code" : "script");
    array[4] = GameValue(handler.body);
    return value;
}

RString ResolveAddonLifecycleScriptName(const MissionPhaseHandler& handler)
{
    if (handler.inlineCode || handler.addonPrefix.GetLength() == 0)
        return handler.body;

    auto DirectScriptName = [](RString name) {
        return RString("\\") + name;
    };

    RString body = handler.body;
    RString leaf = body;
    for (int i = body.GetLength() - 1; i >= 0; --i)
    {
        if (body[i] == '\\' || body[i] == '/')
        {
            leaf = body.Substring(i + 1, body.GetLength());
            break;
        }
    }

    RString prefixed = handler.addonPrefix + handler.body;
    if (QIFStreamB::FileExist(prefixed))
        return DirectScriptName(prefixed);

    RString prefixedLeaf = handler.addonPrefix + leaf;
    if (QIFStreamB::FileExist(prefixedLeaf))
        return DirectScriptName(prefixedLeaf);

    if (QIFStreamB::FileExist(body))
        return DirectScriptName(body);

    if (QIFStreamB::FileExist(leaf))
        return DirectScriptName(leaf);

    return DirectScriptName(prefixed);
}

int DispatchMissionPhaseHandlers(GameState* state, const std::vector<MissionPhaseHandler>& handlers, const char* phase,
                                 GameValuePar argument)
{
    std::vector<MissionPhaseHandler> matchingHandlers;
    for (const MissionPhaseHandler& handler : handlers)
    {
        if (handler.phase != phase)
        {
            continue;
        }
        matchingHandlers.push_back(handler);
    }

    int dispatched = 0;
    for (const MissionPhaseHandler& handler : matchingHandlers)
    {
        AddonLifecyclePrefixScope prefixScope(handler.addonPrefix);
        GameValue args = MakeMissionPhaseArgs(state, handler.phase.c_str(), argument);
        if (handler.inlineCode)
        {
            GameVarSpace local(state->GetContext());
            state->BeginContext(&local);
            state->VarSetLocal("_this", args, true);
            state->EvaluateMultiple(handler.body);
            state->EndContext();
        }
        else
        {
            if (!Poseidon::GWorld)
                continue;

            Poseidon::Script* script = new Poseidon::Script(ResolveAddonLifecycleScriptName(handler), args);
            Poseidon::GWorld->AddScript(script);
            Poseidon::GWorld->SimulateScripts();
        }
        ++dispatched;
    }
    return dispatched;
}
} // namespace

namespace Poseidon
{
RString CurrentAddonLifecyclePrefix()
{
    return ActiveAddonLifecyclePrefix();
}

int RunMissionPhaseForState(GameState* state, const char* phase, GameValuePar argument)
{
    if (!state || !phase || *phase == 0)
    {
        return 0;
    }

    int dispatched = DispatchMissionPhaseHandlers(state, AddonLifecycleHandlers(), phase, argument);
    dispatched += DispatchMissionPhaseHandlers(state, MissionPhaseHandlers(), phase, argument);
    return dispatched;
}

int RunMissionPhase(const char* phase, GameValuePar argument)
{
    return RunMissionPhaseForState(GWorld ? GWorld->GetGameState() : nullptr, phase, argument);
}

void ClearMissionPhaseHandlers()
{
    MissionPhaseHandlers().clear();
    NextMissionPhaseHandlerId() = 1;
}

void ClearAddonLifecycleHandlers()
{
    AddonLifecycleHandlers().clear();
    NextAddonLifecycleHandlerId() = 1;
}

int RegisterAddonLifecycleHandler(const char* addon, const char* phase, RString body, bool inlineCode,
                                  RString addonPrefix)
{
    if (!addon || !*addon || !phase || !*phase || body.GetLength() == 0)
        return -1;

    MissionPhaseHandler handler;
    handler.id = NextAddonLifecycleHandlerId()++;
    handler.addon = addon;
    handler.phase = phase;
    handler.inlineCode = inlineCode;
    handler.addonPrefix = addonPrefix;
    handler.body = body;
    AddonLifecycleHandlers().push_back(handler);
    return handler.id;
}

int RegisterAddonLifecycleHandler(const char* addon, const char* phase, RString body, bool inlineCode)
{
    return RegisterAddonLifecycleHandler(addon, phase, body, inlineCode, RString());
}
} // namespace Poseidon

GameValue MissionPhaseOn(const GameState* state, GameValuePar arg)
{
    if (!CheckMissionPhaseOnArg(state, arg))
        return GameValue(-1.0f);

    const GameArrayType& array = arg;
    MissionPhaseHandler handler;
    handler.phase = GameStringToStdString(array[0]);
    if (!ParseMissionPhaseHandlerTarget(array[1], handler))
        return GameValue(-1.0f);

    handler.id = NextMissionPhaseHandlerId()++;
    MissionPhaseHandlers().push_back(handler);
    return GameValue(static_cast<float>(handler.id));
}

GameValue MissionPhaseOff(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameScalar)
    {
        state->TypeError(GameScalar, arg.GetType());
        return GameValue(false);
    }

    const int id = toInt(static_cast<float>(arg));
    std::vector<MissionPhaseHandler>& handlers = MissionPhaseHandlers();
    for (auto it = handlers.begin(); it != handlers.end(); ++it)
    {
        if (it->id == id)
        {
            handlers.erase(it);
            return GameValue(true);
        }
    }
    return GameValue(false);
}

GameValue MissionPhaseClear(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString)
    {
        state->TypeError(GameString, arg.GetType());
        return GameValue(0.0f);
    }

    const std::string phase = GameStringToStdString(arg);
    std::vector<MissionPhaseHandler>& handlers = MissionPhaseHandlers();
    int removed = 0;
    for (auto it = handlers.begin(); it != handlers.end();)
    {
        if (it->phase == phase)
        {
            it = handlers.erase(it);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    return GameValue(static_cast<float>(removed));
}

GameValue MissionPhaseList(const GameState* state)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    const std::vector<MissionPhaseHandler>& handlers = MissionPhaseHandlers();
    array.Realloc(static_cast<int>(handlers.size()));
    for (const MissionPhaseHandler& handler : handlers)
    {
        array.Add(MakeMissionPhaseHandlerInfo(state, handler));
    }
    return value;
}

GameValue AddonLifecycleRegister(const GameState* state, GameValuePar arg)
{
    if (!CheckAddonLifecycleRegisterArg(state, arg))
        return GameValue(0.0f);

    const GameArrayType& array = arg;
    const std::string addon = GameStringToStdString(array[0]);
    if (addon.empty())
        return GameValue(0.0f);

    const GameArrayType& entries = array[1];

    std::vector<MissionPhaseHandler> parsedHandlers;
    for (int i = 0; i < entries.Size(); ++i)
    {
        if (!CheckAddonLifecycleEntryArg(state, entries[i]))
            return GameValue(0.0f);

        const GameArrayType& entry = entries[i];
        MissionPhaseHandler handler;
        handler.addon = addon;
        handler.phase = GameStringToStdString(entry[0]);
        if (handler.phase.empty() || !ParseMissionPhaseHandlerTarget(entry[1], handler))
            return GameValue(0.0f);

        parsedHandlers.push_back(handler);
    }

    std::vector<MissionPhaseHandler>& handlers = AddonLifecycleHandlers();
    for (auto it = handlers.begin(); it != handlers.end();)
    {
        if (it->addon == addon)
        {
            it = handlers.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (MissionPhaseHandler& handler : parsedHandlers)
    {
        handler.id = NextAddonLifecycleHandlerId()++;
        handlers.push_back(handler);
    }
    return GameValue(static_cast<float>(parsedHandlers.size()));
}

GameValue AddonLifecycleOff(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameScalar)
    {
        state->TypeError(GameScalar, arg.GetType());
        return GameValue(false);
    }

    const int id = toInt(static_cast<float>(arg));
    std::vector<MissionPhaseHandler>& handlers = AddonLifecycleHandlers();
    for (auto it = handlers.begin(); it != handlers.end(); ++it)
    {
        if (it->id == id)
        {
            handlers.erase(it);
            return GameValue(true);
        }
    }
    return GameValue(false);
}

GameValue AddonLifecycleClear(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString)
    {
        state->TypeError(GameString, arg.GetType());
        return GameValue(0.0f);
    }

    const std::string addon = GameStringToStdString(arg);
    std::vector<MissionPhaseHandler>& handlers = AddonLifecycleHandlers();
    int removed = 0;
    for (auto it = handlers.begin(); it != handlers.end();)
    {
        if (it->addon == addon)
        {
            it = handlers.erase(it);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    return GameValue(static_cast<float>(removed));
}

GameValue AddonLifecycleList(const GameState* state)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    const std::vector<MissionPhaseHandler>& handlers = AddonLifecycleHandlers();
    array.Realloc(static_cast<int>(handlers.size()));
    for (const MissionPhaseHandler& handler : handlers)
    {
        array.Add(MakeAddonLifecycleHandlerInfo(state, handler));
    }
    return value;
}
