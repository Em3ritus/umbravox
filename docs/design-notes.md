# UMBRAVOX — design notes

> This is the engineering journal: the measurements, trade-offs and traps
> behind the build, kept in the order they were learned. For a plain
> description of what the project is, start with the [README](../README.md).
> Some status notes below ("Not done yet", capture "is not written anywhere
> yet") are historical and have since been overtaken — treat this as a record
> of *why* things are the way they are, not a current feature list.

A dark-technomancer UI for a dual-band wardriver on the **RockBase NM-CYD-C5**
(ESP32-C5, 2.8" 320x240 ST7789, XPT2046 touch).

Two targets, one copy of the UI:

```
cd sim && make && ./umbravox out.png     # PC: renders a frame to PNG
pio run -e nm-cyd-c5 -t upload            # device: the same screen on glass
```

The simulator came first on purpose. The look is the risky part, and iterating
on it over USB reflashes is how this kind of project dies.

**GPS, WiFi and BLE capture are all live.** Nothing on the screen is invented
any more. What is missing is the other end: none of it is written anywhere
yet, so there is no WiGLE CSV and nothing to upload.

Measured on the board (ESP32-C5 rev 1.0, 1 core, 16MB flash):

```
draw   6 ms    the whole canvas, every frame
blit   7 ms    2 rects, 6689 of 19200 px      (35 ms before dirty rects)
heap   flat over 6060 frames, WiFi + BLE + GPS all running
RAM    34.5% -- 113,108 of 327,680 bytes  (111,052 before the second theme)
Flash  49.1% -- 1,544,642 bytes           (1,535,510 before the second theme)
PSRAM  8 MB, holding the dedup tables
```

The draw and blit figures above predate the BAPH0MAU5 theme and have not been
re-measured. The RAM figure was 28.3% (92,676 bytes) when first recorded; the
tree had already grown to 111,052 bytes before the theme work, which itself
cost 2,056 bytes of RAM and 9,132 of flash.

### PSRAM

The board carries 8 MB that the `esp32-c5-devkitc-1` board definition does
not assume, so it needs `-DBOARD_HAS_PSRAM`. The dedup tables are the natural
tenant: touched once per packet, never in the draw path, so the extra latency
of external RAM costs nothing that matters. **8192 WiFi slots and 4096 BLE
slots**, against 512 each when they lived in internal RAM.

Turning it on paid twice over. Moving the tables out freed internal RAM
(35.8% -> 28.3%, with free heap going 45 KB -> 87 KB), and **the blit dropped
from 12 ms to 7 ms** -- more internal DMA-capable memory left for
Arduino_GFX's transfer buffer. That second one was not planned.

![UMBRAVOX](docs/umbravox.png)

## Why it is built this way

**160x120, blitted 1:2 to the panel.** The framebuffer is 38,400 bytes, which
fits in the C5's internal SRAM instead of PSRAM. This matters more than it
looks: the C5 is **single core**, so packet capture and drawing share one CPU
and every redraw costs captures. A small buffer and a dumb blit is what makes
a frame affordable. The chunky pixel look is a side effect of that decision
rather than a style applied on top of it.

**The seam is a plain struct.** `ui/model.h` is everything a screen may know.
A screen gets a `State` and draws it -- no WiFi handles, no SD handles, no
globals. On the device a collector fills that struct once per frame; here
`sim/main_sim.cpp` fills it with invented numbers. Same code either side, so
the art cannot drift from what the device draws.

**No libraries anywhere.** `g++` and `make`. PNGs are written directly using
deflate's "stored" block type, so the files are larger than a real encoder
would make them and nothing has to be installed. The font is a hand-authored
5x7 all-caps face in `ui/font5x7.cpp` -- all-caps because a technomancer
console has no lowercase and it halves the glyph table. The 16 ring marks in
`ui/runes.cpp` are invented geometry, not transcribed from Enochian, Theban or
any other alphabet with a claimed meaning; they answer to reading as
deliberate at 5 pixels and tiling evenly, and to nothing else.

**Chrome is violet, ceremony is bone, and cyan is data only.** The palette's
organising rule: the cold instrument cyan survives as `ICE`, and `ICE` is only
ever a 5 GHz reading. Nothing structural is cyan. That single move is most of
what separates a summoning circle from a dashboard. `AMBER` and `RED` are
reserved for status and never used for data, so an alert always means
something.

## Screens

**UMBRAVOX** is the live view; **CODEX** is the register of what has been
found. Navigation is by **tapping a tile**, not swiping -- each tile opens the
codex filtered to what that tile counts, which is the obvious meaning of
tapping a number. The mark at the top-left of the header becomes a back arrow
on the codex, in the same place, so the eye does not have to hunt for it.

Swipe navigation was built first and replaced: it is invisible until you
already know it is there, and on a 2.8" panel a diagonal swipe fights the
scroll.

![codex](docs/codex.png)

Codex rows: band stripe, SSID (`<HIDDEN>` is a finding, not a blank), a
four-block signal meter, and channel -- or `BLE` for advertisers, which have
no channel. BLE rows are named by the **last three octets** of the address,
because BLE addresses are frequently randomised and the OUI is often
meaningless.

Two details that cost a round trip each:

- **Scroll was inverted and far too fast.** The speed was a unit bug:
  pixels of finger travel were subtracted straight from a *row* index, so one
  pixel moved the list a whole row. It now accumulates finger pixels and
  advances one row per row-height.
- `ui/layout.h` exists because the code that *draws* a tile and the code that
  decides whether a finger *landed* on it must agree. Two private copies drift
  the first time a row moves.

### Alerts

![alert](docs/alert.png)

The card **materialises** rather than appearing:

![sequence](docs/alert-sequence.png)

1. **The strike** -- the heptagram slams down oversized and contracts onto the
   card, through a wash of the confidence colour (~90 ms).
2. **The ward closes** -- the border traces itself from top-centre, both
   directions at once, and a seal flares where the two ends meet (~300 ms).
3. **The name surfaces out of runes**, left to right, ~55 ms a character. The
   first version scrambled through `#$%&*+/` and read as a terminal with a bad
   connection; runes read as a thing being *named*.
4. **It holds by candlelight** -- the inner border wanders rather than
   breathing on a sine wave, and eight runes walk the perimeter as
   inscription.

The same `{7/3}` heptagram the dial is inscribed with stays faint behind the
card, so the ward is part of the same working rather than separate chrome.
Supporting detail (vendor, MAC, confidence) arrives only after the name
settles, so the eye reads the threat first.

Cost is 26 ms on the worst animating frame against 18 ms steady -- all of it
blit, since the whole card goes out each frame while it moves. The clock
restarts only when the alert is a genuinely *different* device, so parking
next to one camera holds a steady card instead of strobing.

Signature matching lives in `ui/signatures.h` -- pure logic, no hardware, so
`sim/test_signatures.cpp` exercises it on the host. WiFi evidence is the OUI
and the SSID; BLE evidence is the 16-bit service UUID and the SIG company
identifier, read straight out of the advertisement.

**The hard part is not matching, it is not over-matching.** A MAC prefix
identifies whoever registered the block with the IEEE, which for most of this
hardware is the chip vendor rather than the product vendor -- a camera built
on an ESP32 shares its OUI with every hobby board on earth. So every signature
carries its own confidence, and ALERT is a floor on it. The card's colour is
the *confidence*, not the threat: a LOW-confidence hit on something alarming
is still a guess and should not look like a certainty.

Half the test suite is cases that must **not** fire -- an ordinary SSID, a
hidden network, a bare Apple company ID. A matcher that alerts on every
passing phone is worse than none, because it teaches you to dismiss it.

**125 signatures**, ported from SquachWatch-CYD (GPL-3.0, see NOTICE): 97 OUI
prefixes, 14 BLE service UUIDs, 7 SSID prefixes and 7 company identifiers,
covering Flock, Axon, ALPR, cameras and Ring, Meta and Snap glasses, AirTag /
Tile / SmartTag / Find My, skimmers, Remote ID drones, gunshot detectors and
pentest hardware.

The confidence column is why this was worth porting rather than rebuilding.
**Of the 97 OUI rows, 60 are LOW** -- and they are not mistakes. Flock really
does build on ESP32, so an Espressif prefix really is evidence; it is just
evidence shared with every dev board on earth. That research is the part that
takes the time, and it is what the ALERT floor exists to act on.

A test in `sim/test_signatures.cpp` demonstrates this by accident and is kept
on purpose. Its first version used `24:0A:C4` as an "arbitrary" MAC for the
must-not-fire cases -- which is an Espressif block, and therefore sits in the
Flock list. The test reproduced the exact false positive the grading exists
for, so it now asserts the behaviour instead: that prefix reports FLOCK, at
LOW, and a MED floor hides it.

Two notes for anyone extending it:

- **BLE advertisements are parsed by hand**, walking the length-prefixed AD
  structures in the raw payload. The library's `getManufacturerData()` and
  `getServiceUUID()` accessors allocate, and with `shouldParse` on they leaked
  ~17 bytes per advert -- the device ran out of heap and rebooted roughly
  every 45 seconds. The raw payload costs nothing. Watch the heap in the
  status line after touching this.
- The alert **fades rather than latches**, so a card on the glass means the
  thing is still in range.

### ASPECT

What the instrument looks like, as opposed to how it captures. It is a
separate screen, reached from the grimoire, because ATTUNE is full: `ui/layout.h`
documents seven rows as its ceiling and all seven are used.

Three pickers, each a label with a `-` and `+`:

| | choices | effect |
|---|---|---|
| THEME | UMBRAVOX / BAPH0MAU5 / COSMIC MUFFIN | the palette, the dial geometry and the centrepiece |
| SWEEP | RADAR / GAZE / PULSE / STAR / CANDLE / GALAXY / ORBIT / STARFALL / PULSAR / COMET / AURORA | how the dial searches the field |
| ALERT | WARD · SUMMONED · SUPERNOVA / VORTEX / COMET / LOCK-ON | the alert rite (see below) |

Above them is a **live preview**: it calls the real dial code with the radii
scaled down to a 24px bound, rather than drawing a picture of a dial. A
picture would drift from the dial it describes. FACE is a fixed bitmap and does
not scale, so on BAPH0MAU5 the preview crops the seal to its middle -- horns and
eyes -- which is a window onto the dial rather than a defect.

The sweeps a theme offers are part of the theme: GAZE (two beams from the
seal's eyes) exists only on BAPH0MAU5. Changing theme to one that does not
offer the current sweep resets it to RADAR rather than keeping a preference
that would draw nothing meaningful. The six **cosmic sweeps** (GALAXY, ORBIT,
STARFALL, PULSAR, COMET, AURORA) are drawn entirely from `pal::` colours, so
every theme offers them in its own palette. PULSAR finds by distance on the
pulse's ring; GALAXY, ORBIT and COMET find by bearing like the radar; STARFALL
and AURORA are washes that light nothing.

**Alerts are per theme.** UMBRAVOX traces a ward and BAPH0MAU5 summons; those
are fixed, and the ALERT row names them with its buttons dimmed. Only COSMIC
MUFFIN has a choice -- Supernova, Vortex, Comet Strike or Lock-On -- stored as
`Settings.alertStyle`. Confidence still owns the card's border colour in every
rite.

**COSMIC MUFFIN** is the third theme: a neon deep-space palette, and a
centrepiece that is a full-colour bitmap rather than a drawn figure or a
4-level ramp. `tools/gen_muffin.py` crops the committed pixel art in
`ui/art/cosmic-muffin-src.png`, BOX-downsamples it to 72×72, quantises to 16
colours and writes `ui/muffin.h`; `drawMuffin()` scales it to the dial and
masks it to the circle, applying the same lift the seal uses so the candle and
pulse can light it. The muffin has no eyes, so it does not offer the gaze. The
ASPECT row abbreviates it to COSMIC; the scan masthead carries the full name.

**The candle is deliberately subtle.** It has no beam: the circle is lit from
the middle by a guttering flame, and the rings and the seal's brightness
flicker together, a few percent at a time. Contacts are simply there to be seen
rather than found. If it looks almost still next to the others, that is the
design, not a stalled animation.

### ATTUNE

![attune](docs/attune.png)

| | range | effect |
| --- | --- | --- |
| ALERT | LOW / MED / HIGH / OFF | minimum confidence that may interrupt |
| BRIGHT | 1-100% | backlight PWM |
| DWELL | 100-600 ms | time parked per channel |
| BLE | 0-100% | share of the radio BLE may take; 0 gives it all to WiFi |
| BANDS | both / 2.4 / 5 GHz | narrows the hop cycle |
| CLEAR | | wipes both dedup tables and every counter |

Three of these are the trade-offs that were *measured* rather than guessed,
which is why they are knobs at all.

**DWELL shows its cycle time**, not just its own value: `600MS (13S)`. The
dwell is the mechanism, but the full sweep is what you feel -- at 600 ms
across 21 channels a given channel is revisited every 13 seconds, and the
spectrum goes stale. Showing only the dwell hides the consequence, and the
number moves with BANDS too, since 5 GHz-only visits 8 channels rather than
21.

**Brightness steps from a table** (`1, 3, 5, 10, 20 ... 100`) rather than a
fixed step, because 10% turned out to be comfortably readable -- the useful
range keeps going below where a 10-point step can reach.

Settings are **not persisted** yet. NVS is available and the partition table
already reserves it.

## What the scan screen shows

| | |
|---|---|
| **Tiles** | WiFi total, 5 GHz subset, BLE. The stripe colour on each tile is the same colour that band's contacts use on the dial, so the tiles double as the dial's key. |
| **Circle** | Three bounds: the inner field where contacts live, the band the runes walk, and the outer bound. (The radii and the figure belong to the active theme; this table describes UMBRAVOX, and BAPH0MAU5 is covered under ASPECT.) Inscribed inside is a {7/3} heptagram -- seven points, each joined to the one three along. It is drawn deliberately faint: the figure should read as *inscribed*, not lit, because the contacts are the only thing on the dial that is data. |
| **Runes** | On UMBRAVOX, sixteen marks walking the band, slowly and against the sweep. Those near the beam catch the light and those away from it fall back, so the ring reads as consecrated rather than as a flat border. |
| **Field** | Radius is RSSI -- the `-90` labels the outer ring. **Angle is not a bearing.** There is no magnetometer, and RSSI alone cannot give direction. The intent is a stable hash of the BSSID, so a given AP always sits in the same spot and you can watch the field move as you drive. |
| **Sweep** | Cosmetic, but it earns its place: with the radar, a contact's fade is driven by how long since the beam crossed it, which on the device is just `millis()` since last seen. (The candle draws no beam, so nothing is ever just crossed; contacts there are simply present.) Stale contacts decay so the dial does not silt up. |
| **Spectrum** | 13 bars for 2.4 GHz, 8 for the 5 GHz subset we hop, split by a gap. The parked channel is drawn bright with a tick above it. |
| **Footer** | GPS fix and satellite count, card status and log file, uptime. |

There is **no battery gauge**: this board has no battery sensing, and a gauge
that cannot be measured is a number invented on the device's behalf. The
device also never borrows `demoState()` -- it builds a default `State` and
fills only what it can fill from a real source, so anything unimplemented
shows an honest default rather than whatever the simulator uses for a
screenshot.

## Layout

The block below is UMBRAVOX's. The dial geometry is per-theme: the radii
live in the theme table in `ui/palette.cpp`, and BAPH0MAU5 has a larger
circle and no spectrum strip.

```
0        HEADER   sigil - UMBRAVOX - band badge - battery
13 ..100 TILES    x2  w56     CIRCLE  x61 w97 h70
                                  field r22 - runes r26 - bound r31
                                SPECTRUM  x61 w97 h16  (y85)
103      FOOTER   fix - card/log - uptime
```

Screen geometry is named constants in `ui/layout.h`; the dial's radii are the
exception, held in the theme table in `ui/palette.cpp` because they differ per
theme. Magic numbers in a 160px layout are how screens drift a pixel and
nobody notices.

## Device build

```
platform  pioarduino 55.03.36   (stock espressif32 has no ESP32-C5 at all)
board     esp32-c5-devkitc-1
display   Arduino_GFX / Arduino_ST7789
```

### Mesh: Core and Nodes

More radios is the only real fix for the fundamental limit. One radio
sweeping 21 channels at 220 ms looks at any given channel once every 4.6
seconds, and a beacon on a channel you are not parked on is gone.

Transport is **ESP-NOW** -- no AP, no association. The catch that shapes
everything is that it sends and receives on whatever channel the radio is
currently parked on, so a sweeping Core and a sweeping Node would only hear
each other by coincidence. That is not a theory:

```
node sends 1 hello/sec; a SWEEPING core heard 0.04/sec  = 4%
```

One in twenty-one dwells, as predicted. So **the Core parks on the report
channel** whenever a node is present, and resumes sweeping when the last one
drops. Channel 1 is one of the three that actually carry traffic, so the Core
is not idling there -- it becomes its full-time camper while also being
reachable.

Nodes get everything else, split evenly. The 2.4 GHz run is listed first, so
with exactly two nodes the split lands on the band boundary, which is the
division that makes physical sense. A node handed a single channel is told to
**camp** rather than "sweep" a list of one.

Node findings are folded into the Core's own dedup table through the same
path as local sniffing (`cap::ingest`), so counts, the codex, the spectrum
and signature matching all see one merged picture. The spectrum in particular
becomes mesh-wide rather than a view of one radio.

Measured with one node, roughly 80 seconds:

```
core alone on ch1     1 AP known,     3 mgmt frames
core + 1 node        12 APs known, 1033 mgmt frames
node                 374 seen, 374 sent, 0 dropped
```

**Reports carry a sequence number and the Core counts the gaps.** That is
what turned an inferred loss rate into a measured one, and the measurement
disagreed with the inference: the link runs at **0% report loss**, with the
receive path rejecting nothing.

An earlier estimate of 13% came from differencing counters that had not been
reset together -- a node's totals against a Core's, sampled at different
moments and across a reboot. The broadcast-to-unicast change really did help
(59% -> 87%, measured consistently), but the residual gap was an artifact.
`MESHSTAT` on the console prints the receive-path accounting that settled it:

```
meshstat: callbacks=95  rejected: magic=0 ver=0 len=0 noslot=0  accepted=91
```

**The protocol carries a version and the Core complains about mismatches.**
Adding a field to a wire struct and reflashing only one side makes every
frame fail the length check, and both boards look healthy in isolation. That
cost a debugging session before the Core learned to say:

```
mesh: node A7:B4:54 speaks protocol v1, we speak v2 -- reflash it
```

Boot order: **start coordinators before nodes** -- a node already bound to
the CYD as its Core migrates to a coordinator only when it loses that Core.

Known limitation: the ESP-NOW mesh is unauthenticated (magic + version only),
so any radio in range can speak to it. Encryption is out of scope for now.

Two things worth knowing if you extend this:

- A node queues nothing before it is assigned. The first version buffered
  from boot, filled its 64-slot queue before any assignment arrived, and
  discarded 49% of everything it heard.
- The Core folds reports in **outside** its own lock. `cap::ingest` takes a
  different module's spinlock, and nesting two across modules is how a
  deadlock gets written.

### Mesh: network enrolment

Every frame carries a network id, and a board only talks to boards on the same
one, so neighbouring setups never cross-talk. The id is set over the air from
the CYD's MESH screen (or the console):

- **ENROLL** mints a random private id if the CYD is still open, then invites
  for 60 s. Only an *open* board adopts an invite -- a board already on a net
  ignores it, so a neighbour cannot take your nodes.
- **RESET** (`NETRESET`) tells boards on the CYD's net to go open.

Two things about delivery matter, and both were learned the hard way:

- **Boards listen for tens of milliseconds per cycle**, and unassigned boards
  wait on the report channel (1). So during any window the CYD parks on channel
  1 and transmits every 25 ms; a once-a-second broadcast from a sweeping Core
  only landed by luck, and never reached a board behind a coordinator.
- **A reset must not abandon a net while boards are still on it.** The CYD
  stays on the old net until no board has answered for 8 s (minimum 15 s, hard
  cap 120 s), because a board that missed the command -- one behind a
  coordinator falls back to the CYD after ~16 s -- needs to hear it then. Once
  the CYD leaves, an invite cannot reach that board: it only moves *open*
  boards. If the cap is hit with boards still answering, the diag log says so
  (`WARN reset gave up ... NETSET <old>`).

**Recovery:** `NETSET <id>` puts the CYD on a known net (0 = open). The net a
reset left is in `/diag.log` (`enroll reset begins, network N`), so
`NETSET N` followed by `NETRESET` goes back for a stranded board.

### Capture

Promiscuous sniffing of management frames (beacons and probe responses) across
both bands, deduplicated by BSSID. Confirmed on hardware: **13/13 channels on
2.4 GHz and 8/8 on 5 GHz accepted**, no regulatory refusals.

Dual-band on the C5 needs the band selected *before* the channel, and that is
only legal under `WIFI_BAND_MODE_AUTO` -- in a single-band mode the band
cannot be switched at runtime.

```c
esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);   // once, at startup
esp_wifi_set_band(is5 ? WIFI_BAND_5G : WIFI_BAND_2G);
esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
```

**The channel is tracked locally, not read from the packet.** `rx_ctrl` has a
`channel : 4` bitfield, which cannot represent anything above channel 15 and
is therefore useless for 5 GHz. We know which channel we parked on, so we use
that. (`rssi` and `sig_len` are safe -- they carry the same names in both the
HE and non-HE variants of the struct.)

Channel plan is named rather than computed, because 5 GHz numbering is not a
stride: UNII-1 is 36-48, UNII-3 is 149-161, and the 52-64 block between them
is DFS -- radar-detection channels we deliberately stay off.

Dwell is 220 ms: long enough for two beacon intervals (~102 ms) per visit,
which puts a full 21-channel cycle just under 5 seconds. Shorter and beacons
get missed; longer and the cycle crawls. That trade is the main thing to tune
if capture rate ever looks low.

The callback runs on the WiFi task, so it does no printing, no allocation and
no blocking -- just hash, insert, publish, under a short spinlock.

### BLE, and the coexistence trade

Passive scanning, deduplicated by address. Passive on purpose: an active scan
transmits scan requests to pull more out of advertisers, and a wardriver
should listen rather than announce itself.

**There is one radio.** WiFi sniffing and BLE scanning time-share it, and the
split is set by the BLE scan duty cycle. This is not a small effect -- it was
measured, after the first attempt (`window 99 / interval 100`, a 99% duty)
quietly strangled the sniffer:

| at ~22s in | BLE 99% duty | BLE 30% duty |
| --- | --- | --- |
| WiFi APs found | 2 | **6** |
| WiFi frames | 3 | **16** |
| BLE devices | 9 | 8 |
| BLE adverts | 256 | 89 |

BLE gave up two thirds of its advert count and lost **one device**. The reason
is that advertisers repeat every few hundred milliseconds, so finding them is
not duty-cycle bound -- while a beacon on a channel you are not parked on is
simply gone. 30% is the current setting; it is one constant in `blescan.cpp`
if the balance ever needs shifting.

Watch the frame counters after changing anything here. A WiFi capture rate
that quietly collapses looks identical to a quiet neighbourhood.

### GPS

On header **P5**, GPIO 4 (we receive), 9600 baud.

The pin is the trap: **GPIO 4 is broken out on P1 as well**, so a module in P1
looks plugged in and is electrically silent, because only P5 carries the
receiver's TX. If nothing arrives, check the header before anything else.

`pio run -e gps-probe -t upload` flashes a diagnostic that measures the lines
rather than guessing at them. A UART idles high and its shortest pulse is one
bit time, so 1/width gives the baud -- that answers "is anything connected",
"is anything powered" and "what rate" in one pass, with a pull-up as the
control. A pin reading sparse bytes with the pull-up *off* and nothing with it
*on* was never a signal; it was a floating input. That is what P1 looked like.

The parser in `ui/gnss.h` has no hardware in it, so `sim/test_gnss.cpp` can
feed it canned sentences on the host:

```
g++ -std=c++17 -o /tmp/t sim/test_gnss.cpp ui/gnss.cpp && /tmp/t
```

Two things it pins down, both of which are easy to get wrong in ways that
still look plausible:

- **NMEA angles are `ddmm.mmmm` -- degrees and *minutes*.** Parsing one with a
  plain `atof` gives a position that is wrong everywhere but looks like a real
  coordinate.
- **GSV is per-constellation.** Each talker (`GP`, `GL`, `GA`, ...) reports
  only its own total, so reading "the last GSV" undercounts and can claim
  fewer satellites in view than GGA reports in use.

### Touch

XPT2046 on CS 1, **polled** -- there is no IRQ line wired on this board
(RockBase sets `CYD28_TouchR_IRQ = -1`).

It shares SCK/MOSI/MISO with the panel but cannot be clocked near 40 MHz, so
it runs its own 2 MHz transactions on the same bus. That works because
Arduino_GFX is constructed with `is_shared_interface = true`: it takes the HAL
lock and re-applies its own divider on every `beginWrite()`, so an
`SPI.beginTransaction()` at a different speed in between is safe. No display
corruption, confirmed on hardware.

**The touch layer does not share the display's rotation.** Measured by tapping
the four corners:

| corner | raw X | raw Y |
| --- | --- | --- |
| top-left | 3545 | 3474 |
| top-right | 3340 | 409 |
| bottom-right | 496 | 320 |
| bottom-left | 500 | 3524 |
| centre | 2096 | 1910 |

Raw X is high at the top and low at the bottom, so it carries **screen Y**.
Raw Y is high on the left and low on the right, so it carries **screen X**.
Both are inverted. The board's published calibration constants were about
right on *range* but say nothing about assignment, and assignment is the part
that was wrong -- mapping raw X to screen X looks plausible and is useless.

If a panel ever reads wrong, tap the corners and look at the raw pairs before
touching the constants. Which raw axis moves tells you the assignment; which
end is larger tells you the inversion.

### The blit is the budget

Drawing the whole 160x120 canvas costs **6 ms**. Pushing it over SPI cost
**35 ms**. On a single-core chip that second number is CPU the radio does not
get, so it is the only number worth optimising.

It cannot be optimised by going faster. Measured: 35 ms at both 40 MHz *and*
80 MHz, and 112 ms at 10 MHz -- so the speed parameter does work, and 80 is
being clamped. SPI above 40 MHz needs the dedicated IOMUX pins, and this
board's `SCK 6` / `MOSI 7` route through the GPIO matrix. 40 MHz is a hardware
ceiling here, not a setting.

So it is optimised by sending less. `drawScan()` reports which regions
actually changed (`ui::Dirty`), and the device blits only those. The circle is
always moving so it is always dirty, but it goes out as its *bounding box*
rather than its panel -- the chrome around it does not move. Steady state is
2 rects and 35% of the pixels, for **12 ms**. Frame total: 41 ms -> 18 ms.

**The first frame must push the whole canvas**, and does. Most of this screen
is static chrome -- panel fills, borders, corner marks, the `-90` ring label --
that no per-frame region covers precisely *because* it never changes. Marking
only the moving parts leaves that chrome drawn in the canvas but never sent,
and the glass keeps whatever `fillScreen()` left behind. That was a real bug:
the circle and the channel readout rendered, and everything else in the dial
panel stayed black.

The same trap applies to anything added later: if a new element animates, it
needs its own region or a region that contains it. If it is static, the first
frame has it covered.

Next lever, if it is ever needed: the circle's bounding box is still mostly
corners. A per-row span would cut another third.

### Three things that cost time if you get them wrong:

- **Backlight is GPIO 25.** GPIO 27 is the WS2812. Other C5 projects wire the
  backlight to 27, so copying their config pulses the LED and leaves the panel
  dark.
- **`ARDUINO_USB_CDC_ON_BOOT=0`.** The board has a native USB-C *and* a CH340
  UART port. We flash over the CH340 (`/dev/ttyUSB0`), so Serial has to stay
  on UART0 or the console looks dead.
- **Arduino core 3.x LEDC.** `ledcSetup()` / `ledcAttachPin()` are gone and
  `ledcWrite()` takes a pin, not a channel. This is the single API break that
  makes porting core-2 firmware (SquachWatch among them) to the C5 expensive.

And one Arduino_GFX detail: `writeAddrWindow()` / `writePixels()` are declared
on `Arduino_TFT`, not on the `Arduino_GFX` base, so the pointer has to be the
subclass to reach the fast path. The colour constant is `RGB565_BLACK`.

Pins are confirmed against RockBase's own schematic, not inferred:
`SCK 6 / MOSI 7 / MISO 2 / CS 23 / DC 24 / RST -1 / BL 25`, touch `CS 1`,
SD `CS 10`, LED 27, GPS on LP-UART `RX 4 / TX 5`.

The system `esptool` here is 4.7.0, which **cannot identify a C5** -- it
reports `Unexpected chip ID value 23`. Use 5.x for any manual flashing;
PlatformIO ships its own and is fine.

## Files

```
ui/      compiles for BOTH the PC and the device
  palette.h      the whole colour ramp
  font5x7.h/cpp  5x7 all-caps face, ASCII 32..95
  runes.h/cpp    16 invented 5x5 marks for the ring
  canvas.h/cpp   framebuffer + primitives + vignette and scanline passes
  model.h        State: everything a screen may know
  demo.h/cpp     bring-up scaffolding: an invented State. Delete when the
                 real collector lands -- nothing else in ui/ depends on it.
  screens.h      screen entry points
  screen_scan.cpp UMBRAVOX
  screen_aspect.cpp ASPECT: theme and sweep pickers and the live dial preview
  palette.h/cpp  the theme table: colours, dial radii, offered sweeps
  seal.h/cpp     the BAPH0MAU5 centrepiece: face bitmap, drawn figure, mark band
  sweep.h/cpp    the five searches, and the detection that matches each
  sigil.h        generated face bitmap; do not edit by hand
sim/     PC only
  png_writer.h   zlib-free PNG
  main_sim.cpp   writes frames
  Makefile
device/  board only
  main.cpp       owns the panel, the backlight and the 1:2 blit. That is all.
tools/
  gen_sigil.py   generates ui/sigil.h from the source art
platformio.ini
```

## Usage

```
./umbravox out.png                 one frame, 4x (640x480)
./umbravox out.png --scale 2       320x240 -- exactly what the panel shows
./umbravox out.png --frame 40      pick a point in the sweep
./umbravox out.png --sheet 6       out-000.png .. out-005.png
./umbravox out.png --theme 1       0 UMBRAVOX, 1 BAPH0MAU5
./umbravox out.png --sweep 3       0 radar, 1 gaze, 2 pulse, 3 star, 4 candle
./umbravox out.png --screen aspect the appearance screen
make sheet                          six frames, for checking motion
```

`--theme` and `--sweep` are the only way to see the second theme in the
simulator. Settings are clamped as the device clamps them, so `--theme 0
--sweep 1` renders the radar: the gaze is not offered on UMBRAVOX.

Always check `--scale 2` before believing a layout. Everything is legible at
4x; the panel is not 4x.

## Not done yet

- Touch (XPT2046 on CS 1, no IRQ line) and therefore any navigation.
- Every other screen: detail list, per-contact view, settings, the export run.
- A real data source. The intended one is
  [`justcallmekoko/ESP32DualBandWardriver`](https://github.com/justcallmekoko/ESP32DualBandWardriver),
  whose capture, dedup, WiGLE and GPS layers (~6,000 lines) are worth keeping;
  this replaces its ~1,350-line UI, which is built for a 160x80 ST7735.
- Logging. The fix is parsed but goes nowhere yet: no SD card writes, no
  WiGLE CSV.
