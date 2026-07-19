#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/World/World.hpp>

#include <map>
#include <string>
#include <vector>

GameValue RemoteExec(const GameState* state, GameValuePar oper1, GameValuePar oper2);

namespace
{
struct EventHandler
{
    int id = 0;
    std::string scope;
    std::string name;
    bool inlineCode = false;
    RString body;
};

std::string GameStringToStdString(GameValuePar value)
{
    return std::string(((RString)(GameStringType)value).Data());
}

std::vector<EventHandler>& EventHandlers()
{
    static std::vector<EventHandler> handlers;
    return handlers;
}

int& NextEventHandlerId()
{
    static int id = 1;
    return id;
}

bool CheckStringArrayArg(const GameState* state, GameValuePar arg, int size)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != size)
    {
        state->SetError(EvalDim, array.Size(), size);
        return false;
    }

    for (int i = 0; i < size; ++i)
    {
        if (array[i].GetType() != GameString)
        {
            state->TypeError(GameString, array[i].GetType());
            return false;
        }
    }
    return true;
}

bool CheckEventOnArg(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != 3)
    {
        state->SetError(EvalDim, array.Size(), 3);
        return false;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return false;
    }
    if (array[2].GetType() != GameString && array[2].GetType() != GameCode)
    {
        state->TypeError(GameString | GameCode, array[2].GetType());
        return false;
    }
    return true;
}

bool ParseEventHandlerTarget(GameValuePar arg, EventHandler& handler)
{
    if (arg.GetType() == GameString || arg.GetType() == GameCode)
    {
        handler.inlineCode = arg.GetType() == GameCode;
        handler.body = (RString)(GameStringType)arg;
        return handler.body.GetLength() > 0;
    }
    return false;
}

bool CheckEventEmitArg(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != 3)
    {
        state->SetError(EvalDim, array.Size(), 3);
        return false;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return false;
    }
    return true;
}

bool CheckEventNetworkEmitArg(const GameState* state, GameValuePar arg)
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
    return true;
}

GameValue MakeEventArgs(const GameState* state, const char* scope, const char* name, GameValuePar payload)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(3);
    array[0] = GameValue(scope);
    array[1] = GameValue(name);
    array[2] = payload;
    return value;
}

int DispatchEvent(const GameState* state, const std::string& scope, const std::string& name, GameValuePar payload)
{
    if (!Poseidon::GWorld)
        return 0;

    int dispatched = 0;
    for (const EventHandler& handler : EventHandlers())
    {
        if (handler.scope != scope || handler.name != name)
            continue;

        GameValue args = MakeEventArgs(state, handler.scope.c_str(), handler.name.c_str(), payload);
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
            Poseidon::Script* script = new Poseidon::Script(handler.body, args);
            Poseidon::GWorld->AddScript(script);
        }
        ++dispatched;
    }
    return dispatched;
}

GameValue EmitRemoteEvent(const GameState* state, const char* scope, GameValuePar arg, int target)
{
    if (!CheckEventNetworkEmitArg(state, arg))
        return GameValue(false);

    const GameArrayType& array = arg;
    GameValue params = MakeEventArgs(state, scope, GameStringToStdString(array[0]).c_str(), array[1]);

    if (!Poseidon::GWorld || Poseidon::GWorld->GetMode() != Poseidon::GModeNetware)
    {
        DispatchEvent(state, scope, GameStringToStdString(array[0]), array[1]);
        return GameValue(true);
    }

    GameValue descriptor = state->CreateGameValue(GameArray);
    GameArrayType& desc = descriptor;
    desc.Resize(2);
    desc[0] = GameValue("eventReceive");
    desc[1] = GameValue(static_cast<float>(target));

    RemoteExec(state, params, descriptor);
    return GameValue(true);
}
} // namespace

namespace Poseidon
{
void ClearScriptEventHandlers()
{
    EventHandlers().clear();
    NextEventHandlerId() = 1;
}
} // namespace Poseidon

GameValue EventOn(const GameState* state, GameValuePar arg)
{
    if (!CheckEventOnArg(state, arg))
        return GameValue(-1.0f);

    const GameArrayType& array = arg;
    EventHandler handler;
    handler.id = NextEventHandlerId()++;
    handler.scope = GameStringToStdString(array[0]);
    handler.name = GameStringToStdString(array[1]);
    if (!ParseEventHandlerTarget(array[2], handler))
        return GameValue(-1.0f);

    EventHandlers().push_back(handler);
    return GameValue(static_cast<float>(handler.id));
}

GameValue EventOff(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameScalar)
    {
        state->TypeError(GameScalar, arg.GetType());
        return GameValue(false);
    }

    const int id = toInt(static_cast<float>(arg));
    std::vector<EventHandler>& handlers = EventHandlers();
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

GameValue EventClear(const GameState* state, GameValuePar arg)
{
    if (!CheckStringArrayArg(state, arg, 2))
        return GameValue(false);

    const GameArrayType& array = arg;
    const std::string scope = GameStringToStdString(array[0]);
    const std::string name = GameStringToStdString(array[1]);

    std::vector<EventHandler>& handlers = EventHandlers();
    int removed = 0;
    for (auto it = handlers.begin(); it != handlers.end();)
    {
        if (it->scope == scope && it->name == name)
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

GameValue EventEmit(const GameState* state, GameValuePar arg)
{
    if (!CheckEventEmitArg(state, arg))
        return GameValue(0.0f);

    const GameArrayType& array = arg;
    const std::string scope = GameStringToStdString(array[0]);
    const std::string name = GameStringToStdString(array[1]);
    return GameValue(static_cast<float>(DispatchEvent(state, scope, name, array[2])));
}

GameValue EventEmitGlobal(const GameState* state, GameValuePar arg)
{
    return EmitRemoteEvent(state, "global", arg, 0);
}

GameValue EventEmitServer(const GameState* state, GameValuePar arg)
{
    return EmitRemoteEvent(state, "server", arg, 2);
}

GameValue EventReceive(const GameState* state, GameValuePar arg)
{
    if (!CheckEventEmitArg(state, arg))
        return GameValue(0.0f);

    const GameArrayType& array = arg;
    const std::string scope = GameStringToStdString(array[0]);
    const std::string name = GameStringToStdString(array[1]);
    return GameValue(static_cast<float>(DispatchEvent(state, scope, name, array[2])));
}
