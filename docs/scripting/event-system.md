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

`sender` is the remote sender DPID as a string when an event is delivered
through `remoteExec`. Local events and single-player fallback events use `"-1"`.
Keep this value as a string when replying to a specific client; large player
DPIDs are not safe to round-trip through scalar floats.

## Commands

```sqf
eventOn ["scope", "name", "script.sqf"]
eventOn ["scope", "name", { statement; statement }]
eventGet handlerId
eventList
eventOff handlerId
eventClear ["scope", "name"]

eventEmitLocal ["name", payload]
eventEmitGlobal ["name", payload]
eventEmitServer ["name", payload]
eventEmitTarget [target, "name", payload]
eventReceive ["scope", "name", payload]
```

`eventOn` returns a numeric handler id. Store that id if the handler needs to be
removed later with `eventOff`.

`eventGet handlerId` returns one handler record:

```sqf
[id, scope, name, type, body]
```

`type` is `"script"` for script-file handlers or `"code"` for inline code block
handlers. If the id is not registered, `eventGet` returns an empty array.

`eventList` returns an array of all current handler records in the same format.

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

`eventEmit ["scope", "name", payload]` is still available for custom local
scopes. Use the domain-specific emitters for normal local, global, server, and
targeted delivery.

`eventReceive` is the network delivery endpoint used by `eventEmitGlobal`,
`eventEmitServer`, and `eventEmitTarget`. Scripts can call it directly, but
normal mission code should usually use the emit helpers.

Treat server-scoped events as a trust boundary. A client can request any
registered server event name through the event transport, so server handlers must
authorize the sender and validate the payload before changing persistent state.
Use `_this select 3` as the authoritative remote sender DPID and resolve it with
`playerUidById`; do not trust a client-supplied `uid`, player name, or target
selector by itself.

## Scopes

The scope is a string, so systems can define their own scope names. The engine
provides four expected patterns:

- `local`: process-local events only.
- `global`: events intended for every machine in multiplayer.
- `server`: events intended for the server.
- `target`: events intended for a selected target.

`eventEmitLocal` dispatches with the `local` scope on the current machine. It
does not perform network forwarding.

`eventEmit ["scope", "name", payload]` also dispatches only on the current
machine and is intended for custom scopes.

`eventEmitGlobal` sends through `remoteExec` in multiplayer. In single-player, it
falls back to local dispatch with the `global` scope.

`eventEmitServer` sends through `remoteExec` to the server in multiplayer. In
single-player, it falls back to local dispatch with the `server` scope.

`eventEmitTarget` sends through `remoteExec` to a specific target with the
`target` scope. The target uses the normal `remoteExec` selector rules: a
positive DPID string targets one client, an object targets its owner, `-2`
targets all clients, and an array combines selectors. Numeric scalar targets
still work for small constants such as `0`, `2`, and `-2`. Group targets are expanded by
`eventEmitTarget` into the group's unit objects before network dispatch, so a
group sends to the owners of its units rather than only the owner of the group
object itself.

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

eventEmitLocal ["actorCashChanged", payload]
```

SQS-safe payload form:

```sqf
payload = jsonObject [["uid", "76561198027566824"], ["cash", 500]]
eventEmitLocal ["actorCashChanged", payload]
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

uid = playerUidById sender
state = jsonSet [state, "uid", uid]

cacheSet ["actor", uid, state]
cacheFlush ["actor", uid]
```

`sender` is the remote DPID string supplied by the event transport. `playerUidById`
converts it to the stable multiplayer identity stored in `PlayerIdentity.id`.

## Targeted Client Reply

A handler for targeted replies should register with the `target` scope on any
machine that may receive the reply:

```sqf
eventOn ["target", "actorLoaded", "events\on_actor_loaded.sqf"]
```

A server handler can reply only to the client that made a request by using the
sender DPID:

```sqf
eventOn ["server", "actorLoadRequested", {
  payload = _this select 2
  sender = _this select 3

  uid = playerUidById sender
  state = dbLoad ["actor", uid]
  reply = jsonObject [["uid", uid], ["state", state]]

  eventEmitTarget [sender, "actorLoaded", reply]
}]
```

The target can also be a player object, group, or array of supported target
selectors. Groups are expanded to their units and duplicate recipients are
deduplicated by the network dispatch layer:

```sqf
eventEmitTarget [player, "actorLoaded", reply]
eventEmitTarget [group player, "squadStateChanged", payload]
eventEmitTarget [[player, group cursorTarget], "squadStateChanged", payload]
eventEmitTarget [-2, "roundStarted", payload]
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
