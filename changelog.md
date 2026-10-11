# Changelog

## v1.0.0
- Initial release
- Switches to a random StartPos after you die
- Setting for how many deaths before it switches
- On/off toggle

## v1.1.0
- Switch modes: random, next, previous
- Randomized death count (a range between a minimum and a maximum)
- Avoid repeats, and an excluded StartPos list
- Option to include the level start as a spawn
- Only count deaths after a chosen level percentage
- Option to disable in practice mode
- Popup and sound when the StartPos changes
- On-screen label
- Pause menu buttons: settings, switch now, on/off toggle

## v1.2.0
- More reliable auto-switching: the new StartPos is applied right before every respawn, with a backup death counter
- New mode: ping-pong
- Switch on level complete
- Clear practice checkpoints on switch
- Choice of switch sound and popup duration
- Label color, size and an optional total death counter

## v1.3.0
- New pick modes: shuffle (every StartPos once before repeating) and weighted (favors the sections you die in most)
- New triggers: chance (random chance per death) and streak (switch when you stop improving)
- Presets: practice, challenge, chaos, drill
- Label and popup turned off by default
- Performance work: new StartPos applied once, settings read once per event, label redrawn only when its text changes
- Better compatibility with other StartPos mods
- Compact pause panel with an on/off checkbox and settings button ("Switch Now" removed)
- Settings grouped into sections, with dependent options greyed out
- "Clear checkpoints on switch" now off by default
- New logo

## v1.4.0
- More reliable death detection (three independent signals), and the new StartPos is applied at death and again at respawn
- Fallback scan for StartPos if the normal hook finds none
- Default deaths until switch is now 3
- Mod off means no processing, and turning it off from the pause panel puts you back at 0%
- Levels with no StartPos: the mod idles for that level only and wakes up on the next one
- Safe mode: playing from a StartPos never counts as a new best and never shows the new-best popup
- Respawn delay (default 200 ms): the game holds still after a switch and the music starts when it ends
- Subtle switch indicator low on the screen as you die (on by default), replacing the old popup
- Label on by default at size 0.3, with three styles (pill, glass, minimal)
- Random mode avoids the last 2 picks by default
- New practice styles: beat to advance and build-up runs
- Seeded sequences
- StartPos stats (deaths and best run per StartPos) with a button on the level page
- In-game tabbed settings menu with info buttons, 5 themes and automatic saving
- Compatibility warning when another StartPos mod is turned on
- Debug log option
