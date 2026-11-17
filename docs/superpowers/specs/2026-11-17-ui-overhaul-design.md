# Full UI overhaul (theme layouts) — Design

Status: draft for the owner's review. Follows `2026-10-21-theme-packs-design.md` (project 1) and
`2026-11-17-theme-sharing-design.md` (project 2).

## Intent and why

The owner wants themes that **look nothing like UMBRAVOX**: a theme author can change where every piece of the
screen sits, how big it is and how it looks, design the alert, and use their own art and fonts, on either board.
Today a pack only picks colours, a centrepiece, a sweep and an alert from fixed menus, and every theme has
UMBRAVOX's layout.

This is the original roadmap's project 4 ("the whole UI"), done **as data, not scripting**. The owner chose a
data-only format first, with room for a scripted piece later (project 3's idea) as one more kind of piece.

## Goals

- In the designer, drag pieces on the live device preview to move and resize them; a side panel sets each piece's
  art, font, colours and alert effect. A tray beside the preview holds hidden pieces.
- **Scan** and the **alert** get full layout control. Every other engine screen (Detail, Codex, Aspect, Omens,
  Files, Domain, Mesh, Menu, Echoes, Attune) gets full **styling** (fonts, colours, background art, frames, list
  highlight, icons). More screens can get layouts later, one at a time, in the same format.
- **The header and footer pieces move on every screen** (owner's answer). A theme lays them out once, on Scan, and
  every screen draws them there. Each other screen keeps its own internal arrangement but fits it into the content
  area the theme's header and footer leave free (see "Content area").
- **Alerts on both boards** (owner's answer): the CYD engine build gets the alert takeover, clips and effects
  that today exist only on the Waveshare, with a tighter animation budget (see "Speed budget").
- Both boards: the author lays out one board; the designer drafts the other (keeping round things round, anchoring
  each piece to its nearest edge or the centre); the author adjusts it on that board's tab.
- Alerts: an alert may play a clip (GIF or video, optionally one per confidence level), and any piece may switch on
  one simple effect during an alert: **flash, pulse or shake**, with a speed. No timeline.
- Fonts: a built-in set of about a dozen pixel fonts, **plus** uploading a TTF/OTF that the designer turns into
  pixel text at the sizes the layout uses.
- Art: the author drops in PNG or GIF; the designer converts it to the device format.
- Starting layouts on the Start page: four or five that look very different (radar huge, radar to the side,
  numbers only, list view, ...), so nobody starts from a blank screen.
- A speed meter in the designer, measured with the device's own drawing code; a layout over budget cannot be
  installed or shared.
- Old packs (`format: 1`) keep working unchanged. Layout themes are a new format version. Sharing goes through the
  existing gallery and submission flow.

## Non-goals

- No scripting and no code in a pack (a later project; the format leaves a slot for it, below).
- No new kinds of piece invented by the author: a theme moves, sizes, restyles, hides and decorates the pieces the
  firmware provides.
- No layouts for the inside of screens other than Scan and the alert in this project (they only fit into the
  content area the shared header and footer leave).
- No timeline animation, keyframes or per-piece motion paths.
- No portrait orientation (the legacy 120×160 portrait mode is not part of the engine UI).
- The legacy 160×120 canvas (the default CYD release) is not themed by layouts; a layout theme on a device running
  the legacy UI falls back to its format-1 parts (colours etc.), see "Device behaviour".

## Anti-goals (a failure even if it works)

- A theme that can hide an alert or make its words unreadable.
- A theme that can strand the user: MENU and STOP must always be on screen and tappable.
- A theme that makes the radar stutter or starves the radio of CPU.
- A theme that can boot-loop, hang or crash the device, or run code.
- Built-in themes changing by a single pixel (render baselines stay byte-identical), except the CYD engine
  build's new alert takeover, which is deliberate.

## Constraints (verified in the code)

- Engine UI sizes: 480×320 (Waveshare) and 320×240 (opt-in CYD engine build). Both are landscape.
- All engine geometry is single-sourced in `ui/layout_engine.h` (`chrome()`, `scan()`, `codex()`, ...); touch hit
  regions are computed from the same rects, so drawing and touch cannot drift. The host oracle test asserts the
  complete rect set at both sizes.
- The header and footer (MENU, wordmark, status cells, bridge, SD, nav cells, STOP) come from `chrome(W,H)`,
  which **every** engine screen draws (`scanr::drawChrome`) and takes touches from (`addChromeHits`). Only the
  count tiles and the hero come from `scan()` (`ScanLayout`: status, nav, body, statusCells, navCells, counts,
  hero, tiles[3], bridge, wordmark, stop). So moving a header or footer piece is a `chrome()` override, used by
  drawing and hits alike, not a `scan()` one.
- The nav cells are three screens plus MARK POI (`Action::MarkPoi`); MENU is in the header.
- The hero holds more than the radar: the channel readout (`chRect`), the emblem badge, the spectrum with its
  labels (`heroSub`, `specRect`) and, on the Waveshare, a 66 px right column. The wide radar is fixed at r=102,
  centre hero.x+168 (`radarBounds`); `radar.vid` must be exactly 102×102; the dirty-region helpers
  (`radarBounds`, `chRect`, `specRect`, `sweepRects`, `scan_present.h`) recompute these fixed shapes. **Making the
  radar resizable is a rework of radar drawing and invalidation, not a move.**
- The engine alert takeover exists only at `W >= 400` (Waveshare). A HIGH alert covers the whole screen, MENU and
  STOP included, and a tap anywhere dismisses it; LOW and MED cover the hero. The rite (ward, supernova, vortex,
  comet, lockon) and the alert words are drawn together in one rect (`scan_alert.h`, `alertText`).
- Every tappable chrome target is 44 px on **both** boards today.
- Media loading today: emblems are read at boot; `radar.vid` and `alert.snd` are read when a theme stays selected
  for 0.4 s, one set in memory at a time.
- The released loader skips any pack whose `format` is not 1 (`themepack.cpp` `FORMAT`).
- Text is drawn with the single 5×7 font (`ui/font5x7.h`), all caps ASCII 32..95, at integer scales
  (`txt`, `fitScale` in `ui/scan_render.h`). Custom fonts mean a second text routine, not a change to this one.
- Drawing is done in horizontal strips (`scan_render.h` `S{px,W,y0,h,lo,hi}`); every piece must draw correctly
  into any strip window.
- The C5 has **no FPU**; median scan draw is ~12 ms and a rise past ~14 ms is a regression. On the CYD the SPI push
  is the real cost (35 ms for a full frame at the 40 MHz ceiling), so only changed regions are sent
  (`design-notes.md` "The blit is the budget"). **Anything that animates needs its own dirty region**, and a large
  animated area (a full-screen clip, a shaking radar) costs push time every frame.
- Boot heap is scarce (about 64 KB free, must stay above 55 KB); large buffers go in PSRAM (8 MB).
- Packs are read at boot only. The loader is `ui/themepack.cpp`; `format` above the supported number is skipped.
  Format-1 limits: `theme.json` 4 KB, emblem 9.5 KB, `radar.vid` and `alert.snd` as documented in
  `docs/theme-packs.md`.
- The submission pipeline (themes repo) reads packs under fixed limits and file names (`scripts/zipread.mjs`
  `PACK_FILES`, `limits.json`), runs the device loader in the sim and renders previews with
  `tools/pack-preview.mjs`. New files and bigger limits must be added there too, behind a matching firmware pin.

## Approaches

1. **Layout is data, read by the layout engine** (chosen). `theme.json` format 2 gives each piece a rect per board
   and a style; `layout_engine.h` uses those rects in place of its built-in numbers; the existing drawing code
   draws each piece in its new rect. Touch follows automatically. Trade-off: the set of pieces is fixed; a scripted
   piece later is one more kind of piece.
2. A compiled display list the device runs each frame. Most flexible and the road to scripting, but a new
   interpreter on a no-FPU chip, harder to bound for speed, and many more ways for a hostile shared theme to
   misbehave. Rejected.
3. Picture-backed: full-screen background art with fixed live pieces on top. Cheapest, but static decoration and
   themes that read as UMBRAVOX with new wallpaper. Rejected: works against the goal.

The owner picked 1.

## What a layout theme is

A pack folder as today: `theme.json` **stays `"format": 1`** and keeps every format-1 key, and the layout goes in a
new file, `layout.json` (with its own `"format": 1` and its own size cap), plus new optional font and art files.
Released firmware ignores files it does not know, so on a device without the new loader the theme still shows its
colours, centrepiece, sweep and alert; the gallery says which firmware shows the layout. (My call, from the
review: bumping `theme.json` to 2 would make every released device skip the whole pack.)

### Pieces

A **piece** is a named thing the firmware draws. Scan's pieces (names are the format's, fixed):

| piece | what it draws | can hide? |
|---|---|---|
| `radar` | the dial, sweep, contacts, centrepiece; resizable within limits (square) | yes |
| `spectrum` | the spectrum strip and its labels | yes |
| `channel` | the channel readout | yes |
| `badge` | the theme emblem badge | yes |
| `count.wifi`, `count.5g`, `count.ble` | the three count tiles (tapping opens Codex for that band) | yes (Codex stays reachable from nav) |
| `wordmark` | the theme's header text | yes |
| `status.gps`, `status.nodes`, `status.power`, ... | status cells; some open a detail screen | only if that detail is reachable another way; the checks list says which |
| `bridge` | the BLE bridge toggle | **no** (no other route) |
| `sd` | SD state | yes |
| `nav.*` | the three screen buttons and MARK POI | **no** |
| `menu`, `stop` | MENU and STOP | **no** |
| `decor.N` | author's art: a picture or an animated GIF, no function | n/a (author adds/removes) |
| `alert.card.low`, `alert.card.med`, `alert.card.high` | the alert words (what was seen, confidence, address), one rect per level | **no** |
| `alert.clip` | the alert clip, if any (one per level allowed) | n/a |

Radar size: the radar is square and may be **any size** between a minimum and the current size (r=102 on the
Waveshare, r=55 on the CYD) — the owner chose any size over a few fixed sizes. The rings are generated on the device
for radii that have no built-in table, with integer arithmetic that gives exactly the built-in tables' pixels for
the radii that have one. The designer re-converts the theme's `radar.vid` to the placed size when it saves, so the
device never scales a clip. (My call: capping at today's size keeps the drawing cost at or under today's.)

Alert rite: a theme with no clip keeps its format-1 rite, drawn in the card's rect behind the words, as today. A
theme with a clip plays the clip instead of the rite. Tap-to-dismiss stays "tap anywhere", so a full-screen HIGH
card never strands the user even though it covers MENU and STOP while it shows. (My calls.)

Touch: every tappable piece is at least 44 px each way on both boards, as today. (My call: the reviewer found the
CYD already uses 44.)

Each placed piece has, per board: `x y w h` in that board's pixels, a z-order, and a style: font and size,
colours (from the theme's palette roles or its own), optional frame art (a nine-slice picture), optional
background art, and an alert effect (`none | flash | pulse | shake`, speed `slow | medium | fast`).

### Content area

Today every screen's content sits in `chrome().body` (or a fixed rect between the 44 px bands). With a layout
theme, the **content area** is the largest rectangle on screen not covered by any header or footer piece (MENU,
wordmark, status cells, bridge, SD, nav cells, STOP), computed once per board when the layout loads. Every non-Scan
screen lays out inside it: its layout function takes the content rect instead of assuming `{8,52,W-16,H-104}`.
- The designer and device both refuse a layout whose content area is smaller than a minimum per board (the
  builder sets it from the smallest size at which every screen still fits its rows and 44 px controls; the
  designer shows each screen's preview so the author sees the effect).
- Built-in themes produce exactly today's content area, so their screens are byte-identical.
- This touches every screen's layout function and the host oracle test, which gains cases at other content
  sizes. It is the largest single part of the project.

The other screens get a **style block** only: fonts per text role (title, body, numbers), the palette, background
art, a frame picture for panels, the list highlight, and an icon set. Their layouts stay the engine's.

### Two boards

`layout.waveshare` and `layout.cyd` are each optional. A theme that gives neither still works (it is a
format-1 theme with styling). A theme that gives one: the designer drafts the other before saving, so a saved
pack always carries both, and the device never guesses. Drafting rule (the designer's, not the device's):
each piece is anchored to its nearest edge(s) or the centre, scaled by the shorter axis ratio so round things stay
round, then clamped on screen and nudged apart if it now overlaps a piece it did not overlap before. The author
then fixes anything on that board's tab.

### Room for later

A piece kind `script` is reserved (refused by this version's loader with a clear reason), so a scripting project
can add it without a format 3. More screens get layouts by adding their piece names to the same structure.

## Device behaviour

- **Load.** At boot the loader parses each `layout.json` into a fixed-size layout table in PSRAM (no allocation
  sized by the file; piece count capped). Fonts, frames, decor art and per-size clips load **when the theme is
  selected**, through today's 0.4 s per-selection loader, one theme's set in memory at a time. (My call: loading
  every pack's media at boot multiplies boot SD time and PSRAM use.) The boot-loop guard covers both loads.
- **Use.** `layout_engine.h` gains an override for `scan()` and for `chrome()`. Drawing and hits both read the
  overridden rects, so they cannot drift; the radar, spectrum, channel and dirty-region helpers take their
  geometry from the overridden rects instead of fixed numbers. Built-in themes have no override, so their output
  is byte-identical.
- **Check on the device too.** The designer checks everything below, but the device re-checks (packs can be made
  by hand): every rect on screen; MENU, STOP, nav, bridge and the alert cards present, not hidden, tappable ones at
  least 44 px each way and not covered by a higher piece;
  piece count and art sizes in limits. A failed check skips the **layout** (the theme still loads with format-1
  styling) and logs the reason; ASPECT shows it as today's skipped count does.
- **Alerts stay readable.** `alert.card` is always drawn last (top of the z-order) with the existing contrast
  rule applied to its text and edge; its font must be at least a minimum pixel height; a clip or effect never
  draws over it.
- **Legacy UI.** On the legacy 160×120 canvas `layout.json` is ignored and the theme is its format-1 parts.

## Speed budget

- The designer's meter runs the device drawing code (wasm, the same code as the sim) on the layout for a set of
  frames (idle scan, busy scan, each alert level) and reports **draw cost** and **pushed area** per frame.
- The pass rule is calibrated against real hardware once: measured device time = f(sim operation count, pushed
  pixels) fitted from the boot probe on both boards. Until that calibration exists, the meter's limits are set
  conservatively (so it may refuse a layout that would have been fine, never the reverse).
- Pushed area matters most on the CYD: the meter flags an animated area (clip, effect, animated decor) that makes
  steady-state pushes larger than a budget the builder sets from the calibration. The CYD has its own, tighter
  budget for alerts too: a full-screen CYD push is 35 ms against ~12 ms for a normal frame, so a full-screen
  flash or clip on the CYD will usually be refused or limited to a lower frame rate.
- The CYD alert takeover is new firmware work (the `W >= 400` gate in `scan_render.h` goes); built-in themes on
  the CYD engine build gain it too. This changes the CYD engine build's alert look, so its render baselines change
  deliberately, listed image by image; the Waveshare's and the legacy CYD's stay byte-identical.
- The device does not trust the meter: it caps per-frame work (effects on at most N pieces, one clip, one
  animated decor area) by limits in the loader.

## Fonts

- Built-in: about a dozen pixel fonts shipped with the designer (licences allowing redistribution, recorded in
  the repo). Each is converted to the device font format at build time.
- Uploaded TTF/OTF: the designer rasterises it in the browser at each pixel size the layout uses, for the glyphs
  the device can show (ASCII 32..126; lowercase now kept), into the device font format. The pack carries only
  the rasterised sizes, never the font file.
- The same "I have the right to share this" agreement as pictures and sounds covers uploaded fonts.

## Art

- PNG and GIF in; the designer converts to the device format (palette-indexed, like `emblem.bin` and
  `radar.vid`) at the exact pixel size placed. Animated GIF becomes a frame strip with its timing.
- Frames (nine-slice) for panels and buttons.
- Pack size cap rises from today's; the exact numbers are the builder's, set from SD read time at boot and PSRAM
  use with the maximum number of packs, and shown in `limits.json` like every other limit.

## The designer

- New **Layout** step. The live preview is the device drawing code. Click to select, drag to move, corner handles
  to resize, arrow keys to nudge by one pixel. Side panel: the selected piece's art, font, colours and alert
  effect, and its exact numbers. Tray beside the preview: hidden pieces, drag back to show.
- Board tabs: Waveshare 3.5" / CYD 2.8". The first board laid out is the master; the other shows the draft with
  an "adjusted" mark on pieces the author has moved.
- Alert preview: buttons for low / medium / high play the alert on the preview.
- Speed meter always visible; checks list (as today) includes every rule in "Check on the device too".
- Start page: the starting layouts, alongside today's templates.
- Every new limitation goes into `limits.json` and the docs, as the theme-packs spec requires.

## Sharing

- The submission pipeline accepts layout themes: new file names and limits in `zipread.mjs`, the sim runs the
  layout loader, previews render with the layout. Gallery pictures show both boards' layouts, and a card says
  which firmware release first shows the layout.
- `FIRMWARE_PIN` moves to the first release with the layout loader; until then submissions with `layout.json` are
  refused with a clear reason.
- "Open in designer" loads layout themes into the Layout step.

## Testing

- Host tests (sim): format-2 parser hostile-input fuzz (as project 1's), rect checks, the override producing the
  same rects as the built-in when given the built-in's numbers, built-in render baselines byte-identical, alert
  card always on top and readable, MENU/STOP always hittable.
- Designer tests: drafting rule (round stays round, anchors, clamping), font rasteriser output, art conversion,
  speed meter limits, checks list.
- Hardware: boot probe on both boards with a heavy layout theme; meter calibration run.

## Left to the builder

- The exact JSON shape of format 2 and the device file formats for fonts, frames and animated decor.
- Pack size caps, piece-count caps, effect caps and the speed-meter thresholds (from measurement).
- The built-in font list (licences checked) and the four or five starting layouts.
- How the project is split into releases (suggested: device loader, `chrome()`/`scan()` override and content
  area first; then the radar rework; then the designer layout step; then fonts and art; then alerts including the
  CYD takeover; then sharing).
- The minimum content area per board.

## Decisions made here (the author's calls; flag any that are wrong)

- Hidden pieces live in a tray beside the preview.
- Starting layouts on the Start page.
- Speed meter that blocks install and share when over budget.
- A pack always carries both boards' layouts (drafted in the designer, never on the device).
- A failed layout check skips only the layout, not the theme.
- Lowercase becomes available for custom fonts (the built-in 5×7 stays caps only).
- The layout lives in a new `layout.json`; `theme.json` stays format 1, so released devices still show the colours.
- The spectrum, channel readout and emblem badge are their own pieces; the radar is square and resizable up to
  today's size, and the designer re-converts `radar.vid` to the placed size.
- One alert card rect per level; no clip means the format-1 rite plays behind the words; tap anywhere dismisses.
- 44 px touch minimum on both boards.
- Pieces whose function has no other route (MENU, STOP, nav, bridge) cannot be hidden.
- Fonts and art load on theme selection, like clips and sounds today, not at boot.
