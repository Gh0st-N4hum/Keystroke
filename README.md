# Type Duel

A 2D keyboard-typing battle game built with C++ and SFML 3.

You and an opponent race to type the same random string each round.
Whoever finishes first lands the attack — the faster you type
relative to the opponent, the harder you hit. The opponent's speed
scales from 50 WPM (Level 1) to 150 WPM (Level 10).

## Project structure
```
TypeDuel/
  CMakeLists.txt
  src/
    main.cpp
  assets/
    font.ttf   <-- you need to add this (see below)
```

## 1. Get a font
SFML 3 no longer ships a built-in font. Grab any free .ttf font
(for example "Roboto-Regular.ttf" or "OpenSans-Regular.ttf" from
Google Fonts) and place it at:
```
TypeDuel/assets/font.ttf
```
If you skip this, the game will try to fall back to a Windows system
font (Consolas or Arial), but it's best to bundle your own.

## 2. Install SFML 3 on Windows (via vcpkg)
This is the easiest path if you're using Visual Studio 2022.

1. Install **Visual Studio 2022 Community**, with the
   "Desktop development with C++" workload.
2. Install vcpkg:
   ```powershell
   git clone https://github.com/microsoft/vcpkg
   cd vcpkg
   .\bootstrap-vcpkg.bat
   .\vcpkg integrate install
   ```
3. Install SFML 3:
   ```powershell
   .\vcpkg install sfml
   ```

## 3. Build with CMake
From the `TypeDuel` folder:
```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```
The executable will be in `build/Release/TypeDuel.exe`, and the
`assets` folder is copied next to it automatically.

Alternatively, you can open the `TypeDuel` folder directly in
Visual Studio 2022 ("Open a local folder") — it has native CMake
support and will pick up `CMakeLists.txt` automatically. Just make
sure the vcpkg toolchain file is configured under
CMake Settings, or set the `CMAKE_TOOLCHAIN_FILE` environment
variable globally after running `vcpkg integrate install`.

## 4. Run it
- Press **ENTER** at the menu to start Level 1.
- Type the displayed string exactly as shown.
- Press **ESC** to quit anytime.

## Current MVP scope
- Menu, Playing, RoundResult, GameOver, Victory states
- 10 levels, opponent WPM scaling 50 -> 150
- Word difficulty scales with level (short phrases -> long sentences)
- Placeholder rectangle health bars (no sprites yet)

## Natural next steps (let's do these together once this compiles)
- Swap in actual character sprites/animations for attacks
- Add sound effects for hits/misses
- Add a typing accuracy penalty (mistakes should cost you)
- Persist high scores / best WPM per level
- Polish the UI (fonts, colors, level-select screen)
