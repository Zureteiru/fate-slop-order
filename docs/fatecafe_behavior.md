# Fate Café FGO reader: behaviour spec

This document describes how the Fate Café reader (`https://fatecafe.com/leitor`) plays an FGO script. It is the reference for the vnreader Switch player.

- **Recon date:** 2026-11-05.
- **Method:** a live session in a browser, plus reading the production JS/CSS bundles. The bundles were downloaded to a scratch folder outside the repo and are not committed.
- **Copying:** everything below is restated in our own words. Constants and formulas are facts about the behaviour, not copied code.

All coordinates are in **stage units**, a 1344×626 logical canvas (see §4), unless marked "screen px".

## vnreader decisions (2026-11-06, updated 2026-11-07)

This spec describes Fate Café, and **Fate Café is the default for vnreader** (user, 2026-11-07): where nothing below
says otherwise, do what this spec says and take Fate Café's assets 1:1 (e.g. the choice click `fgochoice.mp3`).
Ripped assets are never committed; each user installs them with a `tools/install_*.sh` script.

The user's exceptions, which override the rest of the spec:

1. **Follow the script, not Fate Café.**
   - Honour the durations the script gives for `charaFadein`, `charaFadeout`, `charaMove`, `charaScale` and the `bgm` fade-in.
   - Actually execute `[branch]`/`[label]` jumps at run time.
   - After a choice, keep the stage state of the option the reader picked.

   Where this spec says "ignored" or "fixed 0.3 s", treat it as a Fate Café limitation, not as a target.
2. **Wipe transitions:** use the real game wipe textures if a source can be found, the same way other assets come from Atlas. Otherwise, generate them in code.
3. **Named particle effects:** rebuild them all. The name→program table in §6 is the starting list.
4. **Visual reference for effects:** use frames from YouTube chapter playthroughs.
   Under the default above, Fate Café's 33 wipe textures (base64 PNGs in its reader chunk) are a valid source for item 2.
5. **Voices (2026-11-07):**
   - `[voice X]` plays where it appears, as Atlas DB does (Fate Café holds it for the next beat). It counts as the
     next line's voice in the backlog only when no wait, choice or movie comes in between.
   - A voice is **not** cut when the reader advances: it plays to its end, so a `[wait voice]` after `[k]` really waits
     (up to 12 s). It stops on `[voiceStop]`, Back, skip, or a new voice.
   - `NP_<svt>_<n>` lives at `Audio/NoblePhantasm_<svt>/NP_<svt>_<n>.mp3`. Other ids try `Servants_` first when the last
     part starts with `B` (battle lines), otherwise `ChrVoice_` first.
6. **Look:** the story screen matches the real game (UI art from the game's sheets, FOT-Skip Std B, layout measured
   from 1080p gameplay video; see apps/README.md), not Fate Café's layout.
7. **No SKIP button** on the story screen.
8. **No spinning "next" marker** (advance hint) on the story screen (user, 2026-11-07).

## Reference chapters

| Role | Reader URL | Atlas script ID(s) |
|---|---|---|
| Baseline (sprites, faces, BGM, text). **Also used for choices, flashes, wipes and shake.** | `/leitor/fuyuki?parte=1` (Prologue, Singularity F) | `0100000010` (72 lines), `0100000011` (1411 lines, 66 `？` choice lines) |
| Effect-heavy | `/leitor/solomon?parte=19` ("Throne of the Light Band") | `0100081810` (plus `0100081820` and `0100081821` in the same part) |
| Choices and branch | Same as baseline: `0100000011` has 13 choice blocks plus `[label]` and `[input]` | `0100000011` |

**Script line numbers:** line numbers in this document refer to the NA `.txt` files, numbered from 1. The JP file for `0100000011` has the same line count.

**Deep links:** the reader can open a specific line through query parameters: `?parte=<n>&modo=en|jp|ptbr&cena=<scriptId>&fala=<normalised line text>&n=<occurrence>`. The `fala` text is NFKC-normalised, lowercased, has quote characters removed and has whitespace collapsed. If no line matches, the reader opens at the first beat of `cena`.

---

## 1. Data and asset sources

### 1.1 Request hosts

| Host | What it serves |
|---|---|
| `fatecafe.com/assets/*` | The Vite bundle (lazy chunks per page), CSS, two `FGO Main` woff2 subsets, `img_talk_textbg-*.png` (text box), `img_talk_selectbg-frame-*.png` (choice frame) and `fgochoice-*.mp3` (choice click sound). The 33 wipe textures are 512×512 greyscale PNGs that the reader view chunk (`ReaderChapterView.*.js`) references, either as `/assets/<name>-<hash>.png` files or inline base64; `tools/install_wipes.sh` fetches them from there. |
| `assets.fatecafe.com/data/atlas/*` | Fate Café's own JSON: `reader.json` (catalogue, about 915 KB), `reader-translations/<chapter>.json` (PT-BR plus aligned EN/JP lines), `reader-event-translations.json` and banner data. |
| `static.atlasacademy.io` | **Raw scripts** and all game assets. |
| `api.atlasacademy.io` | `GET /raw/<region>/svtScript?charaId=…&charaId=…`: one batched request per part, covering every `charaSet` figure ID in that part. |
| Others | Google Fonts (Cormorant Garamond, Exo 2, Open Sans) for the site UI, and Cloudflare analytics. There is no Fate Café backend in the playback path. |

**Key answer: Fate Café parses raw Atlas `.txt` scripts in the browser.** It has no pre-parsed script format. For each script ID in the part, it fetches `static.atlasacademy.io/<region>/Script/<folder>/<id>.txt` and parses the file client-side into typed events and then "beats" (see §2). JP mode loads the `JP/` copy of the script. EN uses `NA/`, or a per-script `englishScriptUrl` override from the catalogue.

Fate Café's own data is limited to the following:

- **`reader.json`.**
  - Top-level fields: `generatedAt`, `source`, `sourceRegion`, `dataVersion`, `arcs[]` and `chapters[]`.
  - Each chapter has: `id`, `warId`/`warIds`, `order`, `arcId`, `slug`, `name`, `chapterLabel`, `longName`, `bannerUrl`, `translationUrl`, `languages`, `sectionCount`, `scriptCount`, `durationSeconds` and `sections[]`.
  - Each section has: `questId`, `name`, `type`, `spotName`, `durationSeconds` and `scripts[]`.
  - Each entry in `scripts[]` has: `scriptId`, `scriptUrl`, `scriptRegion`, and optionally `englishScriptUrl`, `englishScriptRegion` and `skipMovieIds`.
  - A reader "part" (`parte=N`) is one section. Its scripts are played back to back as one beat list.
- **The translation file.** It holds per-script arrays of `{english, japanese, portuguese, speakerEnglish, speakerJapanese}`. The reader aligns these to parsed dialogue by matching normalised EN/JP text with a longest-common-subsequence pass, with a speaker-name fallback. **The vnreader does not need this file. It is only used for PT-BR.**

### 1.2 Asset URL patterns

`R` is the region (`NA` or `JP`) and `S` is `https://static.atlasacademy.io`.

| Type | Pattern | Notes |
|---|---|---|
| Script | `S/R/Script/<folder>/<id>.txt` | `<folder>`: IDs starting with `0` or `9` use the first two digits. IDs starting with `94` use `94/<first4>/<id>`. Everything else uses `Common/<id>`. Special case: `WarEpilogue108` → `01/WarEpilogue108`. |
| Background (`[scene N]`) | `S/R/Back/back<N>.png`, or `back<N>_1344_626.png` when N ≥ 101900 | Uses a numeric threshold, not a lookup. |
| Sprite (`charaSet`) | `S/R/CharaFigure/<id>/<id>_merged.png` | One sheet holds the body and every face. |
| Image (`imageSet`, `pictureFrame`, mask textures) | `S/R/Image/<name>/<name>.png` | |
| Marks | `S/R/Marks/<name>.png` | |
| BGM | `S/R/Audio/Bgm/<name>/<name>.mp3`; on error, retried once as `S/R/Audio/<name>/<name>.mp3` | |
| SE (`se`) | `S/R/Audio/<folder>/<name>.mp3` | `<folder>` is `Battle` for names starting `ba`, `ResidentSE` for names starting `ar`, `SE_<n>` for names starting `<n>_`, and `SE` otherwise. |
| SE (`cueSe <folder> <name>`) | `S/R/Audio/<folder>/<name>.mp3` | |
| Voice (`[voice A_B]`) | `S/R/Audio/Servants_<A>/<B>.mp3`; fallback `S/R/Audio/ChrVoice_<A>/<B>.mp3` | |
| Voice (`[tVoice F N]`) | `S/R/Audio/<F>/<N>.mp3` | |
| Movie | `S/R/Movie/<id>.mp4`, with fallback to the other region | |

### 1.3 Per-servant metadata

Fate Café fetches `svtScript` rows for each figure ID and uses only these `extendData` fields: `faceX`, `faceY`, `offsetX`, `offsetY`, `faceSize` and `faceSizeRect`. Example request from Fuyuki part 1: `…/raw/NA/svtScript?charaId=98001000&charaId=98003000&…&charaId=8001000`.

---

## 2. Player model and command table

### 2.1 Pipeline

1. **Pre-pass.** The reader applies `[masterBranch fem masc]`: it drops the label block for the other gender.
2. **Reachability pass.** If the file contains `branch`, `label`, `else` or `revivalBranch`, the reader builds a line graph:
   - Single-target `[branch X]`, `branchQuestClear`, `branchMaterial` and `revivalBranch` jump to label X.
   - `[branchQuestNotClear]` falls through.
   - `[else]` jumps to `[endIf]`.
   - Everything inside a `？…？！` choice region counts as reachable.
   - Unreachable lines are **not played**. They are only kept as hidden text for translation alignment.
   - **Jumps are never executed at run time.** The script is played as one linear list with dead code removed.
3. **Line parse.**
   - Lines starting `＄` or `//`, and empty lines, are skipped.
   - `＠[code：]name` starts a dialogue block. `=spot…` is stripped from the name, and `code` is 1–4 alphanumeric characters.
   - Text lines accumulate until a line ends in `[k]` or `[q]`, or a `[k]`, `[q]` or `[page]` line appears. Each such block becomes one **dialogue** event.
   - A bracket line outside dialogue is parsed as a command.
   - A bracket command *inside* a dialogue block is extracted, runs at the start of that dialogue beat, and is removed from the text.
4. **Command filter.** Only commands on a whitelist (the table below) are parsed. **Every other command is silently dropped**: no error, no placeholder.
5. **Beat build.** A reducer walks the events and produces **beats**. A beat is one stop of the player, and each beat holds a full snapshot of stage state (background, characters, camera, overlays, effects, audio events). Three kinds of event create a beat:
   - **dialogue**: one per `[k]` block;
   - **choice**: one per `？…？！` block;
   - **wait**: `[wt]`, `[twt]` or `[wait X]`, but only if something visual or audio changed since the last beat (see §3).

   All other commands only change the pending state.

### 2.2 Command table

**How to read the table:**

- "Wait beat" means the command adds to the duration of the next wait beat.
- Durations are in **seconds**.
- **Blocks?** says whether the command holds the line: "auto" means it is part of an auto-advancing wait beat; "no" means it applies instantly to the next beat.
- `e[n]` is the nth whitespace-separated argument. Quoted arguments keep their spaces. A decimal comma (`0,5`) is accepted.

#### Stage and characters

| Command | Params read | Effect | Default duration | Easing | Layer or slot | Blocks? |
|---|---|---|---|---|---|---|
| `charaSet C id face name` | code, figure ID, base face, display name | Registers a character (hidden). | n/a | n/a | Character layer | no |
| `masterSet C idM idF face name` | Picks the figure by reader gender | Same as `charaSet`. | | | | no |
| `charaChange` / `charaCrossFade C id face` | code, figure, face | Swaps the figure and keeps visibility and position. **The script's duration argument is ignored.** | Instant swap; the 0.3 s CSS opacity applies | | | no |
| `charaFace` / `charaFaceFade C n` | code, face | Sets the face index. **`charaFaceFade` does not cross-fade.** | Instant | | | no |
| `charaFadein C t slot` | code, **slot** (`e[3]`) | Shows the character at the slot (see §5). **`t` is ignored.** | **0.3 s** opacity (fixed) | `cubic-bezier(.4,0,.2,1)` | Slot | no |
| `charaFadeout` / `charaClear C t` | code | Hides the character. `t` is ignored. | 0.3 s | same | | no |
| `charaPut C slot\|x,y` | Position | Instant reposition. | 0 | | | no |
| `charaMove` / `charaMoveEase C pos t ease` | Position, duration, easing name | Moves the character. | 0.3 s CSS transform | `ease`, or the named ease | | no |
| `charaMoveReturn` | Position | Instant move. | | | | no |
| `charaScale` / `charaMoveScale*` | code, scale | Sets the scale. Duration is ignored. | 0.3 s transform | ease | | no |
| `charaDepth C d` | Depth | z-order inside the character layer. | | | | no |
| `charaLayer C sub K` | Sub-render layer key | Puts the character in a sub-camera layer. | | | | no |
| `charaTalk C` / `on` / `off` / `depthOn` / `depthOff` | | Sets the focus character, or toggles focus dimming. | | | | no |
| `charaFadeTime C t op` | Duration, opacity | Animated opacity change. | `t` | | | wait beat |
| `charaFilter C mode [color]` | mode, colour | See §6. `normal` clears the filter. | | | | no |
| `charaRoll`, `charaRollMove`, `charaRollAxis` | Angle, duration, origin | Rotates the character (z axis, or y for `charaRollAxis … y`). | `t`, or 0 | | | wait beat |
| `charaRelativeLoopMove C n p1..pn t1..tn` | | Loops a float: the amplitude is the max of the points, the period is the sum of the times (default 1 s). | | | | no |
| `charaShake C int ax ay dur` / `charaShakeStop` | interval, amplitude X/Y, duration | Character shake (CSS keyframes). | Period = max(0.04, 4×interval); loops `dur/period` times, or forever | | | wait beat if `dur > 0` |
| `charaAttack C mode x,y t` | mode, offset, duration | Lunge animation. | 0.2 s | `quick` = `cubic-bezier(.2,0,.2,1)`, else `ease-out` | | wait beat |
| `charaShadow C on\|true\|1` | | Adds a drop shadow (0 18 px 13 px, black at 42%). | | | | no |
| `charaCutin` / `charaCutinPause C pattern t softness` | | Reveals the character with a clip-path wipe. | 0.4 s | | | wait beat |
| `charaCutout C t` | | Hides the character with the reverse of its cut-in pattern. | 0.4 s | | | wait beat |
| `charaSpecialEffect C mode slot t` | Mode table (§6) | Erasure or appearance transitions. | 2 s | | | wait beat |
| `overlayFadein` / `overlayFadeout C … slot` | | Like `charaFadein`/`charaFadeout`, on top and without taking talk focus. | | | | no |
| `imageSet` / `verticalImageSet` / `horizontalImageSet` / `masterImageSet C name` | | An image element handled like a character. | | | | no |
| `imageChange C name` | | Swaps the image. | | | | no |
| `sceneSet C id` | | Uses a background image as a positionable element. | | | | no |
| `scene N [t]` / `masterScene` | Background ID (`t` ignored) | Sets the background. | **Cut**; the `reader-backdrop-in` CSS fade applies | | Back | no |
| `communicationChara id a b c face` / `…Loop` / `…Face` / `…Clear` | figure, transmission params, face | Codec-style hologram figure: blue tint and scanlines, at the left. | | | Above characters | no |
| `subCameraOn` / `Off`, `subRenderDepth`, `subRenderScale`, `subCameraMove`, `subCameraRoll`, `subRenderFadein`, `subRenderFadeout`, `subRenderMove*`, `subBlur`, `subBlurOff`, `subCameraFilter` | | Split-screen and sub-camera layers. | `t` | | Sub layer | wait beat |

#### Screen

| Command | Params read | Effect | Default duration | Easing | Layer | Blocks? |
|---|---|---|---|---|---|---|
| `fadein` / `maskin color t` | colour (default black), `t` | Overlay fades **out** (1→0), revealing the scene. | **0.4 s** | CSS keyframe | Screen overlay | wait beat |
| `fadeout` / `maskout color t` | | Overlay fades **in** (0→1) and covers the scene. The cover persists into later beats. | 0.4 s | | | wait beat |
| `fadeMove color t opacity` | | Overlay goes to a partial opacity. | 0.4 s | | | wait beat |
| `wipein` / `wipeout pattern t gradation` | pattern (default `leftToRight`), `t`, softness (default 1) | Texture-threshold wipe (see §6). `wipeout` covers the screen. Strip patterns (`rectangleStrip*`, `verblind`, `sideBlind`, `cutVer`, `cutSide`) not found in the texture table run for **t×1.6**. | 0.4 s | Linear | Screen overlay | wait beat |
| `wipeFilter pattern t intensity` / `wipeOff` | | A static partial wipe used as a vignette (e.g. `circleIn` at fraction 0.6). | | | Screen filter | no |
| `flashin once\|loop rise dur from to` | mode, rise (`once` with four params only), duration, `RRGGBBAA` colours | **Rise layer:** opacity 0→1 over `rise` seconds (ease). **Flash layer:** colour `from`→`to` and opacity 1→0 over `dur` (ease). | dur 0.2; colours `FFFFFF80`→`FFFFFF00` | ease | Screen overlay | wait beat (rise + dur) |
| `flashout` / `flashOff t` | | Stops the flash. | | | | wait beat |
| `shake int ax ay dur` / `shakeStop` | | Whole-stage shake: CSS keyframes at 0/18/36/54…% alternating ±x/±y. | Period = max(0.04, 4×interval). `dur = 0` means it loops until `shakeStop`. | | Stage | wait beat if `dur > 0` |
| `cameraMove t x,y[,scale] [scale]` / `cameraMoveEase x,y t ease scale` | | Pan and zoom the whole stage (CSS transform transition). | `t` (default 0) | ease, or the named ease | Stage | wait beat |
| `cameraHome t` | | Reset to offset 0, scale 1, roll 0. | | | | wait beat |
| `cameraRoll a` / `cameraRollMove t a` | | Rotate the stage. | 0 / `t` | | | wait beat |
| `cameraFilter name [intensity]` | | CSS filter on the stage (see §6). `normal` clears it. | | | | no |
| `blur mode t … amount` / `blurOff t` | mode `lens` or `motion` | Stage blur. Amount = clamp(raw ÷ 3 for lens, or ÷ 100 for motion, to 0.8–14 px). | 0.2 s | | | wait beat |
| `pictureFrame` / `pictureFrameTop name t` / `…Off` / `…Clear` | | An `Image/` overlay frame. | | | Above stage | wait beat if `t` |
| `messageOff` / `messageOn` | | Hides or shows the text box for the following beats. | | | UI | no |

#### Effects

| Command | Params read | Effect | Default duration | Blocks? |
|---|---|---|---|---|
| `charaEffect` / `charaBackEffect C name [x,y] [H\|V\|HV]` | | Particle effect attached to the character (front, or behind for `charaBackEffect`). See §6 for the catalogue. | Per effect | no (persists until stopped) |
| `charaEffectStop`, `…Destroy`, `charaBackEffectStop`, `charaBackEffectDestroy` | | Stops the effect. | | no |
| `charaEffectEdgeBlur C color … opacity` / `…Stop` | | Rim glow on the character (opacity default 0.72). | | no |
| `effect` / `forwardEffect` / `fowardEffect` / `backEffect name [x,y] [flip]` | | Full-stage effect (front or back). `bit_queststart*` shows the "Section N" title card and `bit_chapterstart*` the chapter title card, for **2.6 s**. | Per effect | wait beat for title cards |
| `effectStop` / `…Destroy` (all spellings) | | Stops the effect. | | no |

#### Audio

| Command | Params read | Effect | Default duration | Blocks? |
|---|---|---|---|---|
| `bgm name [x] [vol]` | name, volume = **`e[3]`**. **`e[2]` is ignored.** | Starts the track, looped, with a **hard cut** from the previous track (no fade-in, no crossfade). | | no |
| `bgmStop` / `bgmStopEnd [name] t` | Last numeric argument = fade time | Linear volume fade, then stop. No fade time means an instant stop. | | no |
| `se` / `seLoop name [fade] [vol]` | | One-shot or looped SE. Several can overlap. Starting a looped SE stops older instances with the same key. | | no |
| `cueSe` / `cueSeLoop folder name [fade] [vol]` | | Same as `se`, from the named folder. | | no |
| `seVolume` / `cueSeVolume key [t] vol` | | Volume fade for that key. | | no |
| `seStop` / `cueSeStop key [t]` | | Fade out and stop. | | no |
| `soundStopAll`, `soundStopAllEnd`, `soundStopAllFade`, `seStopAll [t]` | | Stops all SEs. **BGM is not stopped.** | | no |
| `voice A_B` / `voiceStop` | | Sets the voice for the next beat. | | no |
| `tVoice F N` (own line inside a dialogue block, or inline) | | Voice for that dialogue beat. | | no |
| `criMovie` / `movie` / `moviePlay` / `playMovie name` | | Creates a **movie beat** (see §7). IDs in `skipMovieIds` are dropped. | | yes (own beat) |

#### Timing and structure

| Command | Params read | Effect | Default duration | Blocks? |
|---|---|---|---|---|
| `wt` / `twt n` | seconds | Creates a wait beat (§3). Inline inside text, it also sets a partial-text preview. | | auto |
| `wait target` | target (`fade`, `wipe`, `flash`, `voice`, `scene`, `camera`…) | Wait beat of 0 s plus pending effect durations. `wait voice` or `wait tvoice` waits for the voice to end, capped at **12 s**. | | auto |
| `？n：text` … `？！` | Option ID and text | Choice beat (§7). | | yes |
| `k`, `q`, `page` | | End of dialogue block. | | yes |

#### Inline text tags

These apply inside dialogue text:

- **Line breaks:** `[r]`, `[sr]` and `[csr]` all become a newline.
- **Master name:** `%1` becomes the master name (default "Fujimaru"; can be set in the reader).
- **Ruby:** `[#base:ruby]` (§4.4). `[#text]` without ruby gives plain text.
- **Gender variants:** `[&male:female]` picks by reader gender.
- **Colour:** `[RRGGBB]` sets the colour (`FFFFFF` resets). `[-]` closes the most recent colour or size tag.
- **Size:** `[f size]`, `[font size]`, `[fontSize size]` and **`[s n]` are all font size.**
  - Named sizes: x-small 19, small 24, medium 29, large 48, x-large 64. The scale is the size ÷ 29.
  - Numeric sizes: n ÷ 29, clamped to 0.45–2.4.
- **Alignment:** `[align center|right]`.
- **Speed:** `[speed n]`.
- **Rules:** `[line n]` renders as n × `━`.
- **Servant name:** `[servantName id:jp:en]` uses the last non-empty part.
- **Images:** `[image name]` / `[i name]` inserts an inline image.
- **Literal unicode:** `uXXXXuXXXX…` runs are decoded.

**Commands with no effect:**

- **Dropped by the whitelist** (nothing happens). Examples seen in the reference scripts: `[input name]`, `[skip]`, `[end]`, `[endIf]`, `[messageShake]` on its own line (it only works inline in dialogue), `tVoiceUser` and any unknown command.
- **Structural only:** `[label]`, `[branch…]`, `[else]` and `[masterBranch]` are used by the pre-passes and never run.

---

## 3. Timing constants

| Item | Value |
|---|---|
| Typewriter speed (setting) | Slow **22**, normal **45** (default), fast **90** chars/s, or instant. One "char" is one code point; an inline image counts as 1. |
| `[speed n]` | Changes the rate for the following characters: per-char rate = base × n ÷ 20. |
| `[s n]` / `[s -]` | **Font size, not speed** (see inline tags). |
| Click or key mid-line | **Completes the line** (shows all text). The next click advances. |
| Advance keys | Space, Enter, →. ← goes back to the previous dialogue or choice beat. |
| `wt` and fade units | **Seconds** (floats). The game's frames are not used. |
| Wait-beat duration | max(`wt` seconds, longest pending screen-effect duration, camera durations, character-transition durations, special-effect durations, **0.18**), capped at **2.6 s**. A `wt` with nothing pending creates no beat. |
| Wait-beat advance | Automatic. While the text box is hidden (`messageOff`), the beat waits its duration, or 0.22 s if none. |
| Voice wait (`[wait voice]`) | Advances when the voice ends, or after at most **12 s**. |
| Inline `[wt]` inside dialogue | Splits the line: a preview beat shows the text up to the tag. The preview fraction is the characters before the tag ÷ the total. |
| Auto-advance | Off by default; toggled with the play button. Delay = **2.6 s per line** (setting "Time per line"), fixed. **It does not depend on line length or voice length** (except for `[wait voice]` beats). |
| Line scroll | Text longer than the visible lines scrolls by line, 160 ms per line (setting 0–1000 ms). |
| Skip | **"Skip scene"** jumps to the first beat of the next script ID in the part. There is no fast-forward or read-skip mode. |
| Character fade and move | Fixed 0.3 s for opacity and filter (`cubic-bezier(.4,0,.2,1)`) and 0.3 s `ease` for transform, whatever the script says (see Unknowns). |

---

## 4. Layout

### 4.1 Stage

- **Logical stage:** **1344 × 626**. The game's 1024-wide "safe" region is centred in it (x 160–1184).
- **Scaling:** scale = **container height ÷ 626**, always fit-to-height, centred horizontally. A container narrower than 1344×scale **crops the sides**. There is no letterbox bar on the sides; with a wider window the stage simply doesn't fill it.
  - Measured: window 977×854 screen px gives scale 1.364. Visible stage x ≈ 314–1030, i.e. narrower than the 1024 safe area at this aspect ratio.
- **Backgrounds:** IDs at or above 101900 use the `_1344_626` asset, drawn at 1344×626. Lower IDs load the older art. How that art is fitted to the stage wasn't traced (see Unknowns).

### 4.2 Text box

All values are measured in stage units at the default settings.

- **Panel:** **700 × 139**, at x 322 (centred), y 479. Its bottom edge sits about 8 px above the stage bottom.
- **Background:** the `img_talk_textbg` image stretched to 100%, radius 9, at 82% opacity. Box opacity can be set from 0 to 1.
- **Padding:** 34 / 73 / 17.
- **Text area:** **554 × 88**, at x 395, y 513.
- **Font:** `FGO Main` bold, about **19.9 px**, line height **32.4** (1.625×). The text scale setting runs 0.5–1.5.
- **Visible lines:** **2** by default. Options are 2, 3, 4 or 6, or auto height. Longer text scrolls (see capture 057).
- **Glyph fill:** a vertical gradient, ink colour at 24% down to ink mixed 70% with black at 88%, clipped to the text. Default ink is white. Colour tags change the ink, e.g. Announcement lines are cyan `#51d4ff`.
- **Advance hint:** a diamond icon, 25×34, at the bottom right (x 979, y 578).
- **Sequences:** while a beat has `messageVisible = false` (`messageOff`, wait beats, choice beats), the box is hidden.

### 4.3 Name plate

- **Position:** absolute, left +3, top −36 relative to the panel.
- **Size:** height **52**, padding 0 48 0 28.
- **Background:** a 9-slice arrow-tab image (borders 40/35) at the same 82% opacity.
- **Text:** `FGO Main` bold, measured about 18.7 stage px. The CSS declares 30 px × name scale 0.85, so there may be an inner scale. Colour `#eee` with a 1 px black text shadow.
- **JP names:** shown as written in the script, e.g. full-width `？？？`.

### 4.4 Ruby

- **Placement:** absolutely positioned above the base text, centred over it. Bottom edge at 100% − 0.74 em; font size **0.38 em** of the base text; colour `#f2f2f2`; no wrapping.
- **Reveal:** the ruby shows once its base characters have been typed.
- **Character count:** ruby characters do not count towards the typewriter count.
- **Backlog:** ruby is kept in the backlog (see captures `…106_jp_*`).

### 4.5 Choices

- **Position:** centred overlay at stage centre, or centre − 78 when the text box is shown.
- **Size:** width min(1007, 0.76 × visible width); max height 590.
- **Buttons:** at least 89 tall, padding 12/60, 20 apart. Background is the `img_talk_selectbg` 9-slice frame (8/40). Text is `FGO Main` bold `#ededed`, centred. Brightness goes ×1.25 on hover.
- **What stays visible:** the previous dialogue line stays visible under the choices.
- **Click sound:** clicking plays `fgochoice.mp3` at master × 0.3 × SFX volume.

---

## 5. Sprites and faces

- **Body placement:** body left = (1344 − figure image width) ÷ 2 + `offsetX`; top = −`offsetY`.
- **Face crop: identical to Atlas DB `Scene.tsx`.** The face sheet page is 1024×1024.
  - Face tile = `faceSizeRect`, or `faceSize` × `faceSize`, with 256 as the default.
  - Faces per row = ⌊1024 ÷ tile width⌋.
  - For face index `f` ≥ 1, let `k = f − 1`. Then `col = k % perRow`, `row = ⌊k ÷ perRow⌋`, `page = ⌊row ÷ perRow⌋`, `rowInPage = row % perRow`.
  - Sheet x = col × tile width.
  - Sheet y = 768 + 256 × row **if** the tile is 256 tall and the image is 1024 tall. Otherwise sheet y = image height + 1024 × page + rowInPage × tile height.
  - The face is drawn at (`faceX` + body left, `faceY`) with the tile size.
  - Face 0 means no face overlay.
- **One difference from Atlas DB:** Atlas DB also applies a per-figure `offsetsFigure.y` fix. Fate Café does not.
- **Slots** (`charaFadein C t <slot>`, `charaPut`): the slot index gives an x offset from stage centre.

  | Slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
  |---|---|---|---|---|---|---|---|
  | x | −256 | 0 | +256 | −438 | −512 | +438 | +512 |

  An `x,y` value is used directly, with y positive upwards. A missing or unknown slot gives (0, 0). Off-stage parking uses explicit coordinates, e.g. `charaPut A -2000,-2000`.
- **Depth:** characters render at z-index ≈ 5000 plus `charaDepth`. `#A`-style layer codes map to letter index × … (see Unknowns). Effects in the `back` layer sit at z 2; `front` effects sit above everything.
- **Talk focus:**
  - When `charaTalk` focus is on, characters on the same layer who are not speaking get `brightness(0.55)`.
  - The speaker is matched by the `＠code` from the dialogue block, falling back to the display name.
  - In capture `523`, Mash was not visibly dimmed while Lev spoke. The exact trigger conditions are under Unknowns.
- **`charaScale`:** a CSS scale with a 0.3 s transition.

---

## 6. Effects catalogue

**Rendering technology:**

- Screen fades, flashes, shakes, camera moves and character transitions are **CSS** (keyframes and transitions).
- Wipes are **canvas 2D** threshold masks.
- Named particle effects are **canvas 2D procedural "programs"**.
- No WebGL is used.

**Where vnreader differs on purpose:**

- `pictureFrame` / `pictureFrameTop`: the frame image's top 626 rows are drawn at stage scale (Fate Café squeezes them into the 576 visible rows) and under the text box, so the cinema bars never cover the text.
- The hologram (`communicationChara`) is pale blue with scanlines, as in gameplay video; Fate Café draws the plain figure with a blue glow.
- Title-card text comes from the catalogue (no catalogue, no card); chapter labels keep their own case.

### Screen

- **Fade:** a full-screen colour layer whose opacity animates. `fadein` goes 1→0 and `fadeout` goes 0→1. The last opacity persists as a "screen overlay" into later beats.
- **Flash:** two layers (§2). In Fuyuki line 488 (`flashin once 1 0.4 FFFFFFFF 00000000`), the measured opacity rose 0→1 over about 1 s, then a white layer faded out over 0.4 s.
- **Wipe:**
  - Each of 33 patterns has a bundled **512×512 greyscale texture** (drawn into a 256×256 canvas, red channel = v), stretched over the stage: `cinema`, `circleIn`, `circleOut`, `clash`, `cutAcross`, `damage`, `downToUp`, `fire`, `heartOutBig`, `leftDownToRightUp`, `leftToRight`, `leftUpToRightDown`, `magic`, `moya`, `openEye`, `polka02`, `polka04`, `rectangleStripDownToUp`, `rectangleStripLeftToRight`, `rectangleStripRightToLeft`, `rectangleStripUpToDown`, `rightDownToLeftUp`, `rightToLeft`, `rightUpToLeftDown`, `rollFlashRight`, `rollLeft`, `rollRight`, `sazanami`, `sideBlind`, `upToDown`, `uzumaki`, `uzumakiBig`, `windmill`.
  - **Aliases:** `rectangleLeftToRight` and `rectangleRightToLeft` map to the strip versions. Names are matched case-insensitively.
  - **Per-texel alpha:** let p = progress (0→1 for `out`, 1→0 for `in`), g = gradation and v = texel value. Then threshold = p × (1 + g); alpha = clamp((threshold − v) ÷ g) when g > 0, else a hard step.
  - **Fill colour:** black.
  - **Unknown pattern:** no texture is drawn, and the screen jumps to a solid fill at the end.
  - **Texture source:** shipped in Fate Café's bundle, almost certainly taken from the game. vnreader never commits them: `tools/install_wipes.sh` installs them (SHA-256 pinned) into `<user folder>/wipes/<name>.png`. Without them vnreader uses built-in fields of the same shapes (measured from the textures), and an even fade for the rest.
- **Strips:** a CSS `reader-fgo-strips` animation exists for strip-style patterns.
- **Shake:** see §2. Amplitude in stage px; the default if not given is 3 px.
- **`cameraFilter`:**
  - `gray`: grayscale(1).
  - `darkred`: sepia 1, saturate 1.7, hue −35°, brightness 0.72.
  - `aberration`: saturate 1.45, contrast 1.08, plus red and blue 1 px drop-shadows.
  - Any other name gives no filter. `normal` clears it.
- **Character filter (`charaFilter`):**
  - Any mode containing `silhouette`: brightness 0, plus a solid drop-shadow in the given colour.
  - `gray` / `grayscale`: grayscale(1).
  - `sepia`: sepia(1).
  - `dark`: brightness 0.62.
  - Any mode containing `blur`: a 2 px blur.
- **`charaSpecialEffect` modes:**
  - Outgoing: `erasure`, `enemyerasure` and `darkenemyerasure` (dark red), `flasherasure` (white), `erasurereverse`, and `wipe`, `darkwipe` and `wipetime` (circleOut).
  - Incoming: `appearance`, `appearancereverse` and `wipetimere`.
  - Default duration 2 s.

### Named particle effects (`effect`, `charaEffect`)

- **Lookup:** a lookup table maps about 160 effect names to about 50 procedural programs. Each program has a size, duration, loop flag, colour and blend mode (additive by default). Some names are aliases of others with overrides.
- **Programs:** impact, slash, flame, flare, arrow, charge, vortex, sparks, sand, mist, backdrop (a full-screen sepia CSS filter), rim, blood, flurry, crescents, volley, ripple, bubbles, clash, dust, bands, fireball, dissolve, sweep, starburst, splash, rain, blaze, lightning, pon, tunnel, streaks, petals, talisman, confetti, twinkle, gems, surge, spray, speedlines, rays, pixels, whirl, imaginary, noise, hearts, sigil, and others.
- **Examples:**
  - `bit_talk_41`: flare.
  - `bit_talk_10_LowLevel`: flare, drawn on a canvas 2600×120 stage units centred on the character (capture `152`).
  - `bit_talk_explosion`: fireball.
  - `bit_sepia01`: sepia backdrop.
  - `bit_talk_rain01`: rain.
  - `bit_talk_42`: speed lines.
- **Placement:** stage centre + the character's slot position × its scale + the effect's offset. The optional `x,y` overrides this. `H`/`V`/`HV` flips the effect.
- **Fallback:** **an unrecognised effect name renders nothing.** The command is accepted, so `charaEffectStop` still works.
- **For vnreader:** we only need the name→program table as a design reference. The look of each program has to be re-created.
  - Done in `runtime/src/effects.cpp`: our own table for the most used names (timing, size and colour follow Fate Café's entries; the top 40 names are 58% of the 789 effect uses in 191 main-story scripts), keyword fallbacks, 17 programs drawn from a procedural sprite atlas.
  - The renderer has no shaders, so `cameraFilter gray` and the sepia backdrop use greyscale / sepia copies of each texture, made at decode time for chapters that use them.
  - Camera blur redraws each stage quad at 8 offsets (an even average), not a true Gaussian.

---

## 7. Audio and flow

### Audio

- **Volume chain:**
  - Master defaults to **0.5**; BGM and SFX sliders default to 1; mute is available. Settings persist in browser storage under `fgo-reader:audio:v1`.
  - BGM = master × **0.6** × BGM slider × the script's volume.
  - SE = master × **0.3** × SFX slider × the SE's own volume.
  - Voice = master.
- **BGM:**
  - One element, looped. Changing track **cuts** to the new one.
  - Returning to a beat with the same BGM URL doesn't restart it.
  - `bgmStop t` fades out linearly over t, then stops.
  - Muting pauses playback.
- **SE:**
  - Unlimited overlap, keyed by name.
  - SEs are **not** stopped when the scene or beat changes. They stop only on `seStop`, `soundStopAll` or a looped SE restarting, or when the player unmounts.
  - Audio events fire once, when their beat is entered.
- **Voice:**
  - One voice at a time.
  - **Moving to any other beat stops the current voice** (on advance or back).
  - If the `Servants_` URL fails, the reader tries `ChrVoice_`.
  - `tVoiceUser` (player-gender voice) is **not handled**.
  - The master-gender variants that *are* handled are `masterSet`, `masterImageSet`, `masterScene`, `masterBranch` and `[&m:f]`.

### Flow

- **Choices:**
  - Each `？n：` line starts an option. The commands and dialogue up to the next `？` or `？！` belong to that option and are laid out one after another as beats.
  - Picking option *n* jumps to its first beat. At its last beat, advancing jumps to the beat after `？！`, where the routes join again.
  - An empty option jumps straight to the join point.
  - **Quirk:** the stage state carried past the join point is the state at the end of the *last* option, whichever option was picked.
- **Branches and labels:** handled statically (§2.1). There is no run-time flag evaluation.
- **Backlog ("LOG"):**
  - Opens over a dimmed stage.
  - Entries hold the speaker plus the text, with ruby and colour, and JP lines wrapped in 「」.
  - **Text only: no voice replay.**
  - After a deep link it held only the current line, so entries appear to build up from visited beats.
- **Movies (`criMovie`):** each movie gets its own beat.
  - The `<video>` uses the Atlas `Movie/<id>.mp4` from the script region, falling back to the other region, with autoplay, controls and inline playback.
  - It is muted when the volume is 0. When the video ends, the reader advances.
  - IDs listed in `skipMovieIds` are dropped entirely.
  - The player also contains a YouTube-nocookie embed path, presumably for catalogue overrides.
- **Part boundaries:**
  - All scripts in a section are concatenated.
  - A Section N or chapter title card shows when an effect name matches `bit_queststart*` or `bit_chapterstart*`.

### Reader home and chapter page (recon 2026-11-07)

- **Home (`/leitor`):** title, search, a "Start · <chapter>" button (the last chapter read, else Singularity F),
  tabs Main Story / Events / Interludes / Story Mode, arc and order filters. Each arc has a header
  ("Arc 1 ——— 10 chapters") and a 3-column grid of cards: the Atlas `Banner/questboard_cap<war>.png` banner
  (450×125), the chapter label in gold small caps, the name, and "11 parts · PT-BR · EN · JP · 1h 44min".
- **Chapter (`/leitor/<id>`):** "← All chapters", label, name, long name, languages and JP release, "11 parts ·
  31 scenes · 1h 44min", **Start: <part>**, the banner, then "Chapter parts 目次" with List / Map / Summary tabs and
  "Mark chapter as read". Rows: `sectionNumber` (none for prologues; numbers can skip, Fuyuki has no 7), name,
  "spot · N scenes · time", and a "Mark read" toggle.
- **End of a part:** the section is marked read; a notice shows "End of part", the section label,
  **"Continue to <next section>"** with its spot name (only when there is a next section) and **"Back to index"**.
  The top bar's "Index" also goes back to the chapter page.
- **Storage:** `fate-cafe:progresso:v1` = `{fgo: {<chapterId>: {read: [section indices], last, updatedAt}}}`;
  `fgo-reader:last-scene:v1` keeps the chapter, section and line for the home button; the language
  (PT-BR / EN / JP) is chosen in the reader and kept in `fgo-reader:text-mode:v1`.
- **Catalogue:** `reader.json` is fetched with a cache-buster and a 5-minute TTL. Display names are PT-BR; the
  English ones are in `sourceName`, `sourceChapterLabel`, `sourceLongName` (and arcs' `sourceName`). Sections can be
  `locked`, video-only (`video`, no scripts) or of type `free`. It holds the Main Story only (41 chapters at data
  version 895); events and interludes come from other files.
- **vnreader:** follows this for the home, chapter page and end card (Main Story only; no map, summary, search,
  filters or "Mark read"; progress per part, not per line). See apps/README.md "Chapter picker".

---

## 8. Reference captures (`docs/ref/`)

All captures were taken at a **977 × 854** screen-px window (stage scale 1.364, devicePixelRatio 1) and stored as **800 × 699** PNGs, because screenshots are downscaled.

The built-in browser had no way to record video. Screenshots take about 1 s, which is too slow for transient effects (flash, shake, wipe). Those were measured through the DOM instead (§6). They need a proper recording later.

| File | Chapter / script / line | What it shows |
|---|---|---|
| `0100000011_020_wipefilter_circlein_fou.png` | Fuyuki p1 / 0100000011 / L14–20 | `wipeFilter circleIn`, Fou sprite, `???` plate, 2-line text |
| `0100000011_029_choice_overlay.png` | same / L29–33 | Two-option choice over the previous line |
| `0100000011_057_sprite_slot1_text_scroll.png` | same / L45–57 | Mash face 12 at slot 1; a 3-line text scrolled to show the last 2 lines |
| `0100000011_106_jp_ruby.png` | same JP / L106 | `[#教科書:テキスト]` ruby; JP plate `？？？` |
| `0100000011_106_jp_backlog.png` | same JP / L106 | LOG overlay |
| `0100000011_489_wait_beat_message_hidden.png` | same / L488–492 | Wait beat after the flash: box hidden, Command Room background |
| `0100000011_523_two_slots_lev_mash.png` | same / L519–523 | Lev at slot 0 (−256), Mash at slot 2 (+256) |
| `0100081810_050_communication_chara.png` | Solomon p19 / 0100081810 / L47–50 | `communicationChara` hologram (Roman) |
| `0100081810_069_charaeffect_bit_talk_41_camera.png` | same / L62–69 | Scene 36900, `cameraMove` 1.5×, `shake`, `charaEffect A bit_talk_41` |
| `0100081810_072_fadeout_black_midway.png` | same / L72 | `fadeout black 1.5` midway |
| `0100081810_152_charaeffect_bit_talk_10_lowlevel.png` | same / L151–159 | `charaEffect H bit_talk_10_LowLevel` with Mash speaking |

---

## 9. Unknowns

**Timing**

- **Inner name-plate scale:** the measured name-plate font (about 18.7 stage px) doesn't match the declared 30 × 0.85 px. There may be an inner scale on the panel that I didn't pin down.
- **Auto-advance start:** does the 2.6 s timer start when the text finishes typing, or when the beat is entered? I didn't trace this.
- **Measured vs computed wait-beat length:** wait beat 96 in Fuyuki (L488–492) advanced after about 2.55 s in a measurement. That is close to the 2.6 s cap, but the formula predicts about 1.4 s. This needs a re-check with timestamps per beat.

**Layering and slots**

- **Depth mapping:** I didn't confirm how `charaDepth` and `#X` layer codes map to exact z values beyond the base of about 5000.
- **Talk-focus dimming:** I didn't confirm exactly when dimming applies. It wasn't visible in capture 523.
- **Slots beyond 6:** slot indices above 6 fall back to (0, 0). I didn't check whether the game has more slots.

**Script parameters Fate Café ignores**

- **Fade, move and BGM fade-in times:** whether the game's own `charaFadein`, `charaFadeout`, `charaMove` and `bgm` fade-in parameters should be honoured. Fate Café ignores them (fixed 0.3 s, and BGM cuts). This is a design decision for vnreader, not a fact about the game.

**Assets and effects**

- **Older backgrounds:** how backgrounds below ID 101900 are sized and positioned on the 1344-wide stage. A 1.5 × 626 constant sits near the backdrop code, but I didn't trace it.

- **Wipe textures:** the origin and licence of the 33 bundled wipe textures. We must not copy them, so we need our own textures or procedural equivalents.
- **Effect programs:** what each procedural program looks like frame by frame. Only the name→program table and parameters were recorded; reproducing them needs screen recordings.

**Flow**

- **Movie playback:** I didn't observe any movie (none in the reference scripts), so playback, skip and placeholder behaviour has not been seen in practice.
- **YouTube movie path:** the conditions that select the YouTube embed are unknown.
- **Backlog behaviour:** how the backlog behaves across going back and across choice branches.
