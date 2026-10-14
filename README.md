# Void Warzone

Cross-platform RTS / hero defense. Windows, Linux, Android.

## Build

Windows (Visual Studio 2026):

```
cmake --preset windows-vs
cmake --build --preset windows-vs-debug
ctest --preset windows-vs
```

Linux:

```
cmake --preset linux
cmake --build --preset linux
```

Linux headless (server + master):

```
cmake --preset linux-server
cmake --build --preset linux-server
```

Dependencies are fetched by CPM into `.deps/` on first configure.

## Targets

| Target | |
|---|---|
| `vw_client` | game client |
| `vw_server` | dedicated server |
| `vw_master` | master server: lobby, STUN/TURN |
| `vw_tests` | tests |
