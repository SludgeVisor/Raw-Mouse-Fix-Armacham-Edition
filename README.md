# Raw Mouse Fix: Armacham Edition

Raw mouse input fix for **F.E.A.R. 2: Project Origin**.

A small drop-in DLL that gives F.E.A.R. 2 true raw mouse input and removes several mouse problems of the stock game.

**What it fixes**
- Mouse input in the mech segments of the game too: the mechs use the same raw input path as on-foot play, so they no longer feel slippery or accelerated.
- Windows pointer speed / "Enhance pointer precision" acceleration no longer affects mouse look.
- The dead zone for small movements (the stock game rounds movement to whole cursor pixels).
- Frame-rate problems above 60 fps (per-frame cursor re-centering is no longer used).
- Optional: removes the game's own mouse smoothing, and optionally ignores the in-game "Aim Sensitivity" slider.

## Install
1. Download `Raw-Mouse-Fix-Armacham-Edition.zip` from the [Releases](../../releases) page.
2. Copy `msacm32.dll` and `FEAR2RawMouse.ini` into the folder that contains `FEAR2.exe`.
3. Start the game. `FEAR2RawMouse.log` is written next to the DLL and lists which patches were applied.

Uninstall: delete `msacm32.dll`, or set `Enabled=0` in the INI.

`FEAR2.exe` already imports `msacm32.dll` (audio codec manager), so a copy in the game folder is loaded automatically. All calls are forwarded unchanged to the real `msacm32.dll` in System32. The name was chosen so it does not clash with `d3d9.dll` or `X3DAudio1_5.dll` fixes.

## Settings (`FEAR2RawMouse.ini`)
| Key | Default | Meaning |
|---|---|---|
| `Enabled` | 1 | 0 = stock behaviour |
| `Sensitivity` | 1.0 | Overall speed multiplier |
| `MatchWindowsSpeed` | 1 | 1 = multiply by your Windows pointer-speed slider (no acceleration curve); 0 = pure 1:1 counts |
| `ScaleX` / `ScaleY` | 1.0 | Extra per-axis multipliers |
| `ForceNoSmoothing` | 1 | Removes the game's `InputRate` mouse smoothing |
| `IgnoreGameSensitivity` | 0 | 1 = the Options-menu sensitivity slider has no effect |
| `Log` | 1 | Write `FEAR2RawMouse.log` |

**DPI note:** speed scales directly with mouse DPI. Moving from 1000 to 1600 DPI? Multiply `Sensitivity` by 1000/1600 (= 0.625).

## Compatibility
Everything is patched **in memory only**, and only if the exact expected code bytes are found. Nothing on disk is modified. If the bytes don't match, the DLL logs a message and does nothing.

Verified against:
- `FEAR2.exe` – 3,320,320 bytes, SHA-256 `fa6c18d55114add0cda67725dc3d49fe2101806ef1bee67e40e9f5bdab24a268`
- `GameClient.dll` – 2,220,032 bytes, SHA-256 `ccbc5f62f15da4bdd67464f82141557e6e30be0048aa83a7e604d987968b667d`

Other builds (different store versions or patches) are not supported yet; please open an issue with the log.

## How it works
- Registers for Raw Input (`WM_INPUT`) on a message-only window thread and accumulates counts atomically.
- Detours the engine's mouse axis read (axis IDs 1003/1004) and returns the accumulated raw counts for the frame instead of the cursor-pixel delta, and no longer re-centers the cursor each frame.
- Optional patches to `GameClient.dll`: the smoothing blend weight, and the multiply by the profile's `MouseSensitivity`.
- Built freestanding (no CRT, no Windows SDK) with clang + lld-link; see `build.sh`.

## Build
```
./build.sh        # needs clang, lld-link, llvm-dlltool
```

## Multiplayer
F.E.A.R. 2 has multiplayer and does not use an anti-cheat system. This fix only changes how your own mouse input is read and scaled on your machine; it does not touch networking, game rules or other players' data. As with any mod, use it at your own risk and follow any rules of the servers or communities you play on.

## AI disclosure
This project was developed with the assistance of **Claude**, an AI model by [Anthropic](https://www.anthropic.com) (via Claude Code). Claude did the reverse-engineering analysis of the game's mouse code and wrote most of the source code and documentation, working from requirements and in-game testing feedback provided by the repository owner. Please review the source before running it.

## Disclaimer
Unofficial fan-made tool, not affiliated with or endorsed by the game's publisher or developers. Provided as is, with no warranty. No game files are included in this repository.

## License
MIT – see `LICENSE`.
