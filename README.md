# Cross-Base Storage

An Enshrouded mod: the magic chests of all your bases work together.
Stone in one base and wood in another? Build and craft with both, in every base, without carrying anything around.

**Download:** [Nexus Mods](https://www.nexusmods.com/enshrouded/mods/245) or the [Releases](../../releases) of this repository.
How to install, settings, co-op and troubleshooting are explained in the [mod README](mod/README.md).

## What it does

- **Building:** the build menu shows the materials from the magic chests of all your bases, and placing blocks takes them from there.
- **Crafting:** workbenches and the crafting menu use the magic chests of all your bases.
- **Station queues:** the +1 and +10 buttons of production stations fill the queue from the magic chests of all your bases.

## How it works

The game already keeps a list of crafting stock per base, but only looks at the base you are in.
`cross-base-storage.dll` redirects a few game functions so they see the stock of all bases, your current base first.
Every place it changes is found by a byte pattern and checked before patching. If the game code doesn't match (for example after a game update), the mod changes nothing and says so in its log.

`dinput8.dll` is a small loader. The game loads it at start; it passes the game's DirectInput calls on to the real Windows `dinput8.dll` and loads every DLL in `mods\<mod>\native\`.

## Repository

| Path | Content |
|---|---|
| `src/cross_base_storage.c` | the mod |
| `src/dinput8_proxy.c` | the loader |
| `src/*.rc` | version details of the DLL files |
| `mod/` | `mod.json` and the README that ship with the mod |
| `build.bat` | builds both DLLs into `build\` |

## Building

Needs Visual Studio 2022 or newer with "Desktop development with C++". Run `build.bat`; the DLLs end up in `build\`.
For a working install, copy `build\dinput8.dll` next to `enshrouded.exe`, and `mod\mod.json`, `mod\README.md` and `build\cross-base-storage.dll` into `Enshrouded\mods\cross-base-storage\` (the DLL into its `native` subfolder).

## About AI use

This mod was made in collaboration with Claude (Anthropic), an AI assistant. Claude did the reverse engineering of the game, wrote the code and ran the automated tests. I came up with the ideas and the design, decided how each feature should work, and tested everything in game, reporting problems until it worked the way I wanted.

## License

[MIT](LICENSE)
