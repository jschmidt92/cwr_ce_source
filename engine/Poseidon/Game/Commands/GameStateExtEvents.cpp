#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Network/NetworkScriptValueCodec.hpp>
#include <Poseidon/World/World.hpp>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include <map>
#include <string>
#include <vector>

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

bool ParseEventSender(GameValuePar value, int& sender)
{
    if (value.GetType() == GameScalar)
    {
        sender = toInt(static_cast<float>(value));
        return true;
    }
    if (value.GetType() != GameString)
    {
        return false;
    }

    RString textValue = (RString)(GameStringType)value;
    const char* text = textValue;
    if (!text || text[0] == '\0')
    {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    long parsed = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX)
    {
        return false;
    }

    sender = static_cast<int>(parsed);
    return true;
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

bool CheckEventReceiveArg(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != 3 && array.Size() != 4)
    {
        state->SetError(EvalDim, array.Size(), array.Size() < 3 ? 3 : 4);
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

bool CheckEventClientEmitArg(const GameState* state, GameValuePar arg)
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
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return false;
    }
    return true;
}

GameValue MakeEventArgs(const GameState* state, const char* scope, const char* name, GameValuePar payload, int sender)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(4);
    array[0] = GameValue(scope);
    array[1] = GameValue(name);
    array[2] = payload;
    array[3] = GameValue(std::to_string(sender).c_str());
    return value;
}

GameValue MakeEventHandlerInfo(const GameState* state, const EventHandler& handler)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(5);
    array[0] = GameValue(static_cast<float>(handler.id));
    array[1] = GameValue(handler.scope.c_str());
    array[2] = GameValue(handler.name.c_str());
    array[3] = GameValue(handler.inlineCode ? "code" : "script");
    array[4] = GameValue(handler.body);
    return value;
}

void AppendExpandedEventTarget(GameArrayType& out, GameValuePar target)
{
    if (target.GetType() == GameGroup)
    {
        AIGroup* group = GetGroup(target);
        if (!group)
        {
            return;
        }
        for (int i = 0; i < MAX_UNITS_PER_GROUP; ++i)
        {
            AIUnit* unit = group->UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }
            Person* person = unit->GetPerson();
            if (!person)
            {
                continue;
            }
            out.Add(GameValueExt(person));
        }
        return;
    }

    if (target.GetType() == GameArray)
    {
        const GameArrayType& array = target;
        for (int i = 0; i < array.Size(); ++i)
        {
            AppendExpandedEventTarget(out, array[i]);
        }
        return;
    }

    out.Add(target);
}

GameValue ExpandEventTargetGroups(const GameState* state, GameValuePar target)
{
    if (target.GetType() != GameGroup && target.GetType() != GameArray)
    {
        return target;
    }

    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    AppendExpandedEventTarget(array, target);
    return value;
}

int DispatchEvent(const GameState* state, const std::string& scope, const std::string& name, GameValuePar payload,
                  int sender)
{
    if (!state)
        return 0;

    std::vector<EventHandler> matchingHandlers;
    for (const EventHandler& handler : EventHandlers())
    {
        if (handler.scope == scope && handler.name == name)
            matchingHandlers.push_back(handler);
    }

    int dispatched = 0;
    for (const EventHandler& handler : matchingHandlers)
    {
        GameValue args = MakeEventArgs(state, handler.scope.c_str(), handler.name.c_str(), payload, sender);
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

            Poseidon::Script* script = new Poseidon::Script(handler.body, args);
            Poseidon::GWorld->AddScript(script);
        }
        ++dispatched;
    }
    return dispatched;
}

bool EncodeRemoteEvent(const GameState* state, GameValuePar params, GameValuePar target, AutoArray<char>& encodedParams,
                       int& scalarTarget, AutoArray<char>& encodedTarget)
{
    if (!Poseidon::EncodeScriptValue(encodedParams, params))
        return false;

    GameValue expandedTarget = ExpandEventTargetGroups(state, target);
    Poseidon::RemoteExecTargetSelector selector;
    if (!Poseidon::BuildRemoteExecTargetSelector(selector, expandedTarget) ||
        !Poseidon::EncodeRemoteExecTargetSelector(encodedTarget, selector))
    {
        return false;
    }

    scalarTarget = selector.kind == Poseidon::RemoteExecTargetKind::Scalar ? selector.scalar : 0;
    return true;
}

GameValue EmitRemoteEventToTarget(const GameState* state, const char* scope, const std::string& name,
                                  GameValuePar payload, GameValuePar target)
{
    GameValue params = MakeEventArgs(state, scope, name.c_str(), payload, -1);

    if (!Poseidon::GWorld || Poseidon::GWorld->GetMode() != Poseidon::GModeNetware)
    {
        DispatchEvent(state, scope, name, payload, -1);
        return GameValue(true);
    }

    AutoArray<char> encodedParams;
    AutoArray<char> encodedTarget;
    int scalarTarget = 0;
    if (!EncodeRemoteEvent(state, params, target, encodedParams, scalarTarget, encodedTarget))
        return GameValue(false);

    return GameValue(GetNetworkManager().RemoteExec(RString("eventReceive"), encodedParams, scalarTarget, encodedTarget,
                                                    false, RString(), false));
}

GameValue EmitRemoteEvent(const GameState* state, const char* scope, GameValuePar arg, int target)
{
    if (!CheckEventNetworkEmitArg(state, arg))
        return GameValue(false);

    const GameArrayType& array = arg;
    return EmitRemoteEventToTarget(state, scope, GameStringToStdString(array[0]), array[1],
                                   GameValue(static_cast<float>(target)));
}

thread_local int GScriptEventSender = -1;
} // namespace

namespace Poseidon
{
int CurrentScriptEventSender()
{
    return GScriptEventSender;
}

ScriptEventSenderScope::ScriptEventSenderScope(int sender) : _previous(GScriptEventSender)
{
    GScriptEventSender = sender;
}

ScriptEventSenderScope::~ScriptEventSenderScope()
{
    GScriptEventSender = _previous;
}

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
    handler.scope = GameStringToStdString(array[0]);
    handler.name = GameStringToStdString(array[1]);
    if (!ParseEventHandlerTarget(array[2], handler))
        return GameValue(-1.0f);

    handler.id = NextEventHandlerId()++;
    EventHandlers().push_back(handler);
    return GameValue(static_cast<float>(handler.id));
}

GameValue EventGet(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameScalar)
    {
        state->TypeError(GameScalar, arg.GetType());
        return state->CreateGameValue(GameArray);
    }

    const int id = toInt(static_cast<float>(arg));
    for (const EventHandler& handler : EventHandlers())
    {
        if (handler.id == id)
        {
            return MakeEventHandlerInfo(state, handler);
        }
    }
    return state->CreateGameValue(GameArray);
}

GameValue EventList(const GameState* state)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    const std::vector<EventHandler>& handlers = EventHandlers();
    array.Realloc(static_cast<int>(handlers.size()));
    for (const EventHandler& handler : handlers)
    {
        array.Add(MakeEventHandlerInfo(state, handler));
    }
    return value;
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
    return GameValue(static_cast<float>(DispatchEvent(state, scope, name, array[2], -1)));
}

GameValue EventEmitLocal(const GameState* state, GameValuePar arg)
{
    if (!CheckEventNetworkEmitArg(state, arg))
        return GameValue(0.0f);

    const GameArrayType& array = arg;
    return GameValue(static_cast<float>(DispatchEvent(state, "local", GameStringToStdString(array[0]), array[1], -1)));
}

GameValue EventEmitGlobal(const GameState* state, GameValuePar arg)
{
    return EmitRemoteEvent(state, "global", arg, 0);
}

GameValue EventEmitServer(const GameState* state, GameValuePar arg)
{
    return EmitRemoteEvent(state, "server", arg, 2);
}

GameValue EventEmitTarget(const GameState* state, GameValuePar arg)
{
    if (!CheckEventClientEmitArg(state, arg))
        return GameValue(false);

    const GameArrayType& array = arg;
    return EmitRemoteEventToTarget(state, "target", GameStringToStdString(array[1]), array[2], array[0]);
}

GameValue EventReceive(const GameState* state, GameValuePar arg)
{
    if (!CheckEventReceiveArg(state, arg))
        return GameValue(0.0f);

    const GameArrayType& array = arg;
    const std::string scope = GameStringToStdString(array[0]);
    const std::string name = GameStringToStdString(array[1]);
    int sender = Poseidon::CurrentScriptEventSender();
    if (sender < 0 && array.Size() > 3)
        ParseEventSender(array[3], sender);
    return GameValue(static_cast<float>(DispatchEvent(state, scope, name, array[2], sender)));
}
