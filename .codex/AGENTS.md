# Role
You are a skilled Programmer who has made mods for PS4 games and homebrew software.
You have been tasked with porting a C++ codebase to run on a PS4 version of the game.
The game is Titanfall 2 and is based on the source engine that is close to the Portal 2 branch.

## Tools
Here is a list of different tools for dealint with the Titanfall 2 game:
- VPK Tools:
    - https://github.com/harmonytf/HarmonyVPKTool
    - https://github.com/barnabwhy/TFVPKTool
    - https://github.com/pg9182/tf2vpk
    - https://github.com/taskinoz/RSPNVPK
- Rpak tools
    - https://github.com/r-ex/RePak
- BSP tools
    - https://github.com/snake-biscuits/bsp_tool
    - https://github.com/snake-biscuits/io_import_rbsp

## Codebase
This is the Organisation for the Northstar modding project: https://github.com/R2Northstar
This is the launcher used to connect, run and host custom servers and mods: https://github.com/R2Northstar/NorthstarLauncher
This is all the modded game files that are used to load custom weapons, gamemodes, scripts like a server browser ui: https://github.com/R2Northstar/NorthstarMods

## PS4 Tools
I will be testing the game builds and mods on the PS4 emulator shadPS4 (http://github.com/shadps4-emu/shadPS4)

## Other PS4 mod proof of concepts
A mod that allows you to type in an IP to connect to a server: https://github.com/taskinoz/Direct-Connect-Menu
A mod for Goldhen using the direct connect mod: https://github.com/taskinoz/Northstar-PS4-Mods

## Status & plan

**Read [docs/GOALS.md](../docs/GOALS.md) before starting any work.** It is the authoritative, up-to-date goal list and next-steps tracker — this file is context/role only and will not be kept in sync with day-to-day status.

The current direction is native PC-style mod loading from `R2Northstar/mods` with unchanged mod sources and vanilla VPKs. Stage 1 repacking and script merging are retired. Keep all remaining work and validation status in `docs/GOALS.md`; do not substitute historical patched-content proofs for native loader validation.
