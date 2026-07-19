# Script Event System

Poseidon exposes a named script event bus for mission and mod scripts. It is
separate from object event handlers such as `unit addEventHandler [...]`.

Handlers are script files. When an event fires, each handler script receives:

```sqf
_this select 0
_this select 1
_this select 2
_this select 3
```

Where:

```sqf
scope = _this select 0
name = _this select 1
payload = _this select 2
sender = _this select 3
```

`sender` is the remote sender DPID when an event is delivered through
`remoteExec`. Local events and single-player fallback events use `-1`.

## Commands

```sqf
eventOn ["scope", "name", "script.sqf"]
eventOn ["scope", "name", { statement; statement }]
eventOff handlerId
eventClear ["scope", "name"]

eventEmit ["scope", "name", payload]
eventEmitGlobal ["name", payload]
eventEmitServer ["name", payload]
eventEmitClient [target, "name", payload]
eventReceive ["scope", "name", payload]
```

`eventOn` returns a numeric handler id. Store that id if the handler needs to be
removed later with `eventOff`.

Strings always run script files:

```sqf
eventOn ["local", "actorLoaded", "events\on_actor_loaded.sqf"]
```

Code blocks run inline:

```sqf
eventOn ["local", "actorLoaded", { payload = _this select 2; hint payload }]
```

Inline code block handlers should also stay on one line in SQS-style scripts.

Inline code runs immediately during event dispatch. Script-file handlers are
started as scripts, like `exec`, and may use SQS waits such as `~5`.

Handlers are mission-scoped. The engine clears registered event handlers before
running a mission's `init.sqs` and `init.sqf`, so each mission starts with a fresh
event registry and can register its handlers from init scripts.

`eventReceive` is the network delivery endpoint used by `eventEmitGlobal`,
`eventEmitServer`, and `eventEmitClient`. Scripts can call it directly, but
normal mission code should usually use the emit helpers.

## Scopes

The scope is a string, so systems can define their own scope names. The engine
provides three expected patterns:

- `local`: process-local events only.
- `global`: events intended for every machine in multiplayer.
- `server`: events intended for the server.
- `client`: events intended for targeted clients.

`eventEmit` only dispatches on the current machine. It does not perform network
forwarding.

`eventEmitGlobal` sends through `remoteExec` in multiplayer. In single-player, it
falls back to local dispatch with the `global` scope.

`eventEmitServer` sends through `remoteExec` to the server in multiplayer. In
single-player, it falls back to local dispatch with the `server` scope.

`eventEmitClient` sends through `remoteExec` to a specific target with the
`client` scope. The target uses the normal `remoteExec` selector rules: a
positive DPID targets one client, an object targets its owner, `-2` targets all
clients, and an array combines selectors.

## Local Event Example

Register a local handler:

```sqf
cashHandler = eventOn ["local", "actorCashChanged", "events\on_actor_cash_changed.sqf"]
```

Register a local inline handler:

```sqf
cashHandler = eventOn ["local", "actorCashChanged", { payload = _this select 2; hint format ["Cash: %1", jsonGet [payload, "cash"]] }]
```

Emit the event:

```sqf
payload = jsonObject [
  ["uid", "76561198027566824"],
  ["cash", 500]
]

eventEmit ["local", "actorCashChanged", payload]
```

SQS-safe payload form:

```sqf
payload = jsonObject [["uid", "76561198027566824"], ["cash", 500]]
eventEmit ["local", "actorCashChanged", payload]
```

Handler script:

```sqf
scope = _this select 0
name = _this select 1
payload = _this select 2

uid = jsonGet [payload, "uid"]
cash = jsonGet [payload, "cash"]

hint format ["%1 cash: %2", uid, cash]
```

## Global Event Example

Register on each machine:

```sqf
eventOn ["global", "actorLoaded", "events\on_actor_loaded.sqf"]
```

Emit from any machine:

```sqf
payload = jsonObject [
  ["uid", uid],
  ["name", name player]
]

eventEmitGlobal ["actorLoaded", payload]
```

In multiplayer this is forwarded using `remoteExec ["eventReceive", 0]`.

## Server Event Example

Register on the server:

```sqf
? isServer : eventOn ["server", "actorSaveRequested", "events\server_actor_save.sqf"]
```

Emit from a client:

```sqf
state = jsonObject [
  ["uid", uid],
  ["position", getPosASL player],
  ["direction", getDir player],
  ["loadout", getUnitLoadout player]
]

eventEmitServer ["actorSaveRequested", state]
```

Server handler:

```sqf
scope = _this select 0
name = _this select 1
state = _this select 2
sender = _this select 3

uid = jsonGet [state, "uid"]

cacheSet ["actor", uid, state]
cacheFlush ["actor", uid]
```

## Targeted Client Reply

A server handler can reply only to the client that made a request by using the
sender DPID:

```sqf
eventOn ["server", "actorLoadRequested", {
  payload = _this select 2
  sender = _this select 3

  uid = jsonGet [payload, "uid"]
  state = dbLoad ["actor", uid]
  reply = jsonObject [["uid", uid], ["state", state]]

  eventEmitClient [sender, "actorLoaded", reply]
}]
```

The target can also be a player object or any other supported `remoteExec`
target selector:

```sqf
eventEmitClient [player, "actorLoaded", reply]
eventEmitClient [-2, "roundStarted", payload]
```

## Removing Handlers

```sqf
handler = eventOn ["local", "test", "events\test.sqf"]
eventOff handler
```

Remove every handler for one event:

```sqf
eventClear ["local", "test"]
```
