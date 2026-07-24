# Addon Lifecycle

Addon lifecycle handlers are persistent startup hooks for mods and framework
layers. They run from the same engine-owned phases as mission lifecycle handlers,
but they are addon-scoped and survive mission resets.

Use this layer for XEH-style bootstrapping: an addon registers its init scripts
once through config, and missions do not need manual lifecycle registration.

## Config Classes

The preferred addon-facing API is config-driven:

```cpp
class Extended_PreInit_EventHandlers {
  class ids_core {
    init = "ids_core\XEH_preInit.sqf";
    serverInit = "ids_core\XEH_serverPreInit.sqf";
    clientInit = "ids_core\XEH_clientPreInit.sqf";
  };
};

class Extended_PostInit_EventHandlers {
  class ids_core {
    init = "ids_core\XEH_postInit.sqf";
    serverInit = "ids_core\XEH_serverPostInit.sqf";
    clientInit = "ids_core\XEH_clientPostInit.sqf";
  };
};
```

Each nested class name is the addon/framework registration key. Each value can
be a direct `.sqf` or `.sqs` script path. Non-path values are treated as inline
SQF code:

```cpp
class Extended_PostInit_EventHandlers {
  class ids_core {
    init = "logInfo 'ids_core postInit'";
  };
};
```

Supported classes:

- `Extended_PreInit_EventHandlers`
- `Extended_Init_EventHandlers`
- `Extended_PostInit_EventHandlers`
- `Extended_ServerInit_EventHandlers`
- `Extended_ServerPostInit_EventHandlers`
- `Extended_PlayerLocalInit_EventHandlers`
- `Extended_PlayerLocalPostInit_EventHandlers`
- `Extended_Respawn_EventHandlers`
- `Extended_PlayerServerInit_EventHandlers`
- `Extended_JIPInit_EventHandlers`

Supported values in each nested class:

- `init`: runs on the class's base phase.
- `serverInit`: runs on the server-only variant of the class phase, when that
  class has one.
- `clientInit`: runs on the client/player-local variant of the class phase,
  when that class has one.

For example, inside `Extended_PreInit_EventHandlers`, `serverInit` maps to
`serverPreInit` and `clientInit` maps to `playerLocalPreInit`. Inside
`Extended_PostInit_EventHandlers`, `serverInit` maps to `serverPostInit` and
`clientInit` maps to `playerLocalPostInit`.

The config-facing pre/post order is:

```text
preInit
serverPreInit / playerLocalPreInit
init.sqf
postInit
serverPostInit / playerLocalPostInit
```

`Extended_Respawn_EventHandlers` maps `init` and `clientInit` to
`playerLocalRespawn`. It fires on the client after the engine selects the
respawned player object, which lets addons reattach player-object actions or
other local state.

## Dispatch

When a phase fires, addon lifecycle handlers run from the internal engine-owned
phase registry.

Each handler receives the normal mission phase `_this` payload:

```sqf
phase = _this select 0
argument = _this select 1
```

For `playerLocalRespawn`, `argument` is an array containing the selected
respawned player object:

```sqf
newPlayer = (argument select 0)
```

Addon lifecycle handlers are not cleared by mission init. They are populated
from addon config when addons are loaded.

Addon `CfgFunctions` entries are registered before lifecycle handlers dispatch,
so functions declared in config are available from addon `preInit` scripts.

## XEH Bootstrap Pattern

The mission can stay focused on mission-specific setup. It does not need to
register addon phase handlers itself; the addon config does that when the addon
is loaded.

For reusable mod or framework startup, use `Extended_*_EventHandlers`.
