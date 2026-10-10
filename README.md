# Random StartPos (Geode mod)

Switches to a random StartPos every N deaths. Configurable in Geode's settings menu (and via a pause-menu button).

## Project layout
```
mod.json            Mod metadata + settings (toggle, deaths-until-switch, etc.)
CMakeLists.txt      Build config
src/main.cpp        All mod code
about.md            Shown in the Geode mod page
changelog.md
logo.png            Mod icon
.github/workflows/  GitHub Actions build (builds .geode for every platform)
```

## BEFORE YOU BUILD
Edit `mod.json`: change `"id"` (`yourname.random-startpos`) and `"developer"` to your own name.
Also check `"geode"` / `"gd"` versions match what you have installed (set to Geode 5.0.0 / GD 2.2081).

## Option A: build with GitHub (no setup needed on your PC)
1. Create a free GitHub repo and upload everything in this folder.
2. Go to the **Actions** tab. The "Build Binary" workflow runs automatically.
3. When finished, download the **Build Output** artifact. It contains the `.geode` file.
4. Put the `.geode` file in your Geometry Dash `geode/mods` folder (or Geode menu > Mods > "Install from file"). Restart GD.

## Option B: build locally
1. Install the Geode CLI and SDK: https://docs.geode-sdk.org/getting-started/
2. Run:
   ```
   geode build
   ```
3. The `.geode` file appears in `build/`. Install it as above.

## Using it
- Place at least one StartPos in your level (editor).
- Settings: Geode > Mods > Random StartPos > Settings, or the **StartPos** button in the pause menu.
- Toggle on/off, set "Deaths Until Switch", and optional extras.

## Notes
- Practice-mode checkpoints take priority over StartPos in GD itself. If you use checkpoints, the switch won't be visible until they're cleared (or enable "Disable In Practice Mode").
- If Geode 5 changed a function (e.g. `openSettingsPopup`), the compiler will tell you which line; the fix is usually a one-line signature change.
