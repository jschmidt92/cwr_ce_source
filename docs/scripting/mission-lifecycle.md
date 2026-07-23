# Mission Lifecycle

Poseidon exposes internal mission lifecycle phases used by the config-driven XEH
system. This is separate from the runtime event system: lifecycle phases are
engine-owned and fire in a fixed order during mission startup.

The mission-scoped registry is cleared at the beginning of mission init.
`preInit` fires immediately after that clear, before mission scripts run. The
side-scoped pre phases fire immediately after global `preInit`, and the
side-scoped post phases fire immediately after global `postInit`.

Addon and framework layers should use
[`Extended_*_EventHandlers`](addon-lifecycle.md) instead of manually registering
common phase hooks from mission scripts. The old low-level `missionPhase*`
commands are intentionally not exposed to scripts.

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
- `serverPreInit`: server-only pre-init phase immediately after `preInit`.
- `playerLocalPreInit`: client/player-local pre-init phase immediately after
  `preInit`.
- `init`: after mission `init.sqs` and `init.sqf`.
- `postInit`: immediately after `init`.
- `serverPostInit`: server-only post-init phase immediately after `postInit`.
- `playerLocalPostInit`: client/player-local post-init phase immediately after
  `postInit`.

Multiplayer role phases:

- `serverInit`: before `initServer.sqs`.
- `playerLocalInit`: before `initPlayerLocal.sqs`.
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
