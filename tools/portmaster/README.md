# vnreader for PortMaster

FGO story reader. Built with `bash tools/build_portmaster.sh` (aarch64).

Copy `vnreader.sh` and the `vnreader/` folder into your `ports` folder, then put the contents of your
PC's `userdata/` folder (ui/, fonts/, sfx/, wipes/, dict.bin, cache/, progress.json, saves/) into
`ports/vnreader/userdata/`.

- **No Wi-Fi (RG28XX):** the reader only shows what is in `userdata/cache/`. Read the parts on the PC first
  (the Windows app fills the cache as it plays), then copy the cache over. The chapter list comes from the
  cached catalogue too; parts that were never read on the PC fail to load.
- **With Wi-Fi:** downloads work when the firmware has libcurl.
- **Font:** the game font `userdata/fonts/FOT-SkipStd-B.otf` is used for the story; the dictionary popup
  uses `userdata/fonts/system.ttf` (any Japanese font) or falls back to the game font.
- **Controls:** A advance, B back, X log, Y dictionary, Select auto, L1 / R1 save / load, Start back / quit.
  Select + Start (PortMaster hotkey) force-quits.
- `log.txt` in this folder has the run's messages.
