#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/World/World.hpp>

#include <string>
#include <vector>

namespace
{
struct MissionPhaseHandler
{
    int id = 0;
    std::string phase;
    bool inlineCode = false;
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

int& NextMissionPhaseHandlerId()
{
    static int id = 1;
    return id;
}

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
} // namespace

namespace Poseidon
{
int RunMissionPhaseForState(GameState* state, const char* phase, GameValuePar argument)
{
    if (!state || !phase || *phase == 0)
    {
        return 0;
    }

    std::vector<MissionPhaseHandler> matchingHandlers;
    for (const MissionPhaseHandler& handler : MissionPhaseHandlers())
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
            if (!GWorld)
                continue;

            Script* script = new Script(handler.body, args);
            GWorld->AddScript(script);
        }
        ++dispatched;
    }
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
