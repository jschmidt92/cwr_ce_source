# Mission Lifecycle

Poseidon exposes mission lifecycle hooks for scripts that need to run at known
mission load phases. This is separate from the runtime event system: lifecycle
phases are engine-owned and fire in a fixed order during mission startup.

The lifecycle registry is mission-scoped and is cleared at the beginning of
mission init. `preInit` fires immediately after that clear, before mission
scripts run, so handlers registered from `init.sqs` or `init.sqf` can catch
later phases such as `init`, `postInit`, `serverInit`, and `playerLocalInit`,
but not `preInit`.

## Commands

```sqf
missionPhaseOn ["phase", "script.sqf"]
missionPhaseOn ["phase", { statement; statement }]
missionPhaseOff handlerId
missionPhaseClear "phase"
missionPhaseList
```

`missionPhaseOn` returns a numeric handler id. Store the id if the handler needs
to be removed later with `missionPhaseOff`.

`missionPhaseList` returns handler records:

```sqf
[
  [id, phase, type, body],
  [id, phase, type, body]
]
```

`type` is `"script"` for script-file handlers or `"code"` for inline code block
handlers.

When a phase fires, each handler receives:

```sqf
_this select 0
_this select 1
```

Where:

```sqf
phase = _this select 0
argument = _this select 1
```

The argument is currently empty for the built-in phases. It is reserved for
future phase-specific context.

## Built-In Phases

Common mission phases:

- `preInit`: before mission `init.sqs` and `init.sqf`; this is currently an
  engine-owned phase and is not observable by handlers registered from mission
  scripts.
- `init`: after mission `init.sqs` and `init.sqf`.
- `postInit`: immediately after `init`.

Multiplayer role phases:

- `serverInit`: before `initServer.sqs`.
- `serverPostInit`: after `initServer.sqs`.
- `playerLocalInit`: before `initPlayerLocal.sqs`.
- `playerLocalPostInit`: after `initPlayerLocal.sqs`.
- `playerServerInit`: before `initPlayerServer.sqs` for a JIP player.
- `jipInit`: before `initJIP.sqs` on a joining client.

The multiplayer init files support both legacy `.sqs` and SQF `.sqf` variants.
When both exist for the same hook, Poseidon runs the legacy `.sqs` file first
and the `.sqf` file second:

- `initServer.sqs`, then `initServer.sqf`
- `initPlayerLocal.sqs`, then `initPlayerLocal.sqf`
- `initPlayerServer.sqs`, then `initPlayerServer.sqf`
- `initJIP.sqs`, then `initJIP.sqf`

SQF init files are executed unscheduled, like `init.sqf`, and receive the same
`_this` argument as the corresponding SQS script.

## Example

Register lifecycle hooks from `init.sqf`:

```sqf
missionPhaseOn ["postInit", "scripts\post_init.sqf"]
missionPhaseOn ["serverInit", "scripts\server_init.sqf"]
missionPhaseOn ["playerLocalInit", "scripts\player_local_init.sqf"]
missionPhaseOn ["jipInit", { hint "Joined in progress" }]
```

Remove a hook later:

```sqf
id = missionPhaseOn ["postInit", "scripts\post_init.sqf"]
missionPhaseOff id
```

Clear every handler for a phase:

```sqf
missionPhaseClear "postInit"
```
