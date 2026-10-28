# No libraries. `make` and a C++17 compiler is the whole requirement, which is
# what keeps the iteration loop fast enough to actually do art in.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

BIN = umbravox
SRC = main_sim.cpp ../ui/palette.cpp ../ui/canvas.cpp ../ui/gnss.cpp ../ui/nav.cpp ../ui/screen_codex.cpp ../ui/screen_attune.cpp ../ui/screen_detail.cpp ../ui/screen_mesh.cpp ../ui/screen_domain.cpp ../ui/screen_omens.cpp ../ui/screen_echoes.cpp ../ui/screen_grimoire.cpp ../ui/signatures.cpp ../ui/wifi_auth.cpp ../ui/demo.cpp ../ui/font5x7.cpp ../ui/runes.cpp ../ui/screen_scan.cpp ../ui/seal.cpp ../ui/muffinseal.cpp ../ui/motif.cpp ../ui/themepack.cpp ../ui/sweep.cpp ../ui/screen_aspect.cpp ../ui/screen_files.cpp ../ui/files.cpp ../ui/prevstate.cpp ../ui/framediff.cpp ../ui/draw.cpp ../ui/portrait_chrome.cpp ../ui/portrait_scan.cpp ../ui/portrait_screens.cpp
# nav.cpp reaches every screen, and the screens share chrome and name tables,
# so this links the same sources as the simulator (minus its main) rather than
# a hand-picked subset that breaks each time a screen grows a dependency.
SRC_NOMAIN = $(filter-out main_sim.cpp,$(SRC))

# Every ui header, found rather than listed. The hand-written list silently
# omitted nav.h, layout.h, signatures.h and wifi_auth.h, so editing any of
# them left umbravox stale -- and a stale binary makes `regress` pass while
# testing code that no longer exists. Later tasks add headers here; a wildcard
# cannot forget them.
HDR = $(wildcard ../ui/*.h) png_writer.h

$(BIN): $(SRC) $(HDR) packload.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lm

run: $(BIN)
	./$(BIN) umbravox.png

sheet: $(BIN)
	./$(BIN) sheet.png --sheet 6

clean:
	rm -f $(BIN) $(TESTS) sdcmd_console bench_draw *.png

# The host tests each carry their own main(), so each links as its own
# binary. They are listed explicitly rather than globbed: a test that stops
# being built is a test that stops failing, and nobody notices.
# palette.cpp reaches themepack.cpp (sanitizeStatus) and muffinseal.cpp
# (muffinEmblem), which reaches motif.cpp (drawEmblem), so any rule linking it links those too.
PAL_DEPS = ../ui/palette.cpp ../ui/themepack.cpp ../ui/muffinseal.cpp ../ui/motif.cpp ../ui/canvas.cpp ../ui/font5x7.cpp
UI_CORE = ../ui/runes.cpp $(PAL_DEPS)

test_chanplan: test_chanplan.cpp ../device/chanplan.h ../device/mesh.h ../ui/model.h
	$(CXX) $(CXXFLAGS) -I../ui -o $@ test_chanplan.cpp -lm

test_phase: test_phase.cpp ../device/phase.h
	$(CXX) $(CXXFLAGS) -o $@ test_phase.cpp -lm

test_wigle: test_wigle.cpp ../device/wigle.h
	$(CXX) $(CXXFLAGS) -o $@ test_wigle.cpp -lm

test_logging: test_logging.cpp ../device/logging.cpp ../device/logging.h ../device/sdlog.h ../device/csv.h ../device/applog.h ../device/wigle.h
	$(CXX) $(CXXFLAGS) -o $@ test_logging.cpp ../device/logging.cpp -lm

test_csv: test_csv.cpp ../device/csv.h ../ui/gnss.cpp ../ui/gnss.h
	$(CXX) $(CXXFLAGS) -o $@ test_csv.cpp ../ui/gnss.cpp -lm

test_gnss: test_gnss.cpp ../ui/gnss.cpp ../ui/gnss.h
	$(CXX) $(CXXFLAGS) -o $@ test_gnss.cpp ../ui/gnss.cpp -lm

test_signatures: test_signatures.cpp ../ui/signatures.cpp ../ui/signatures.h
	$(CXX) $(CXXFLAGS) -o $@ test_signatures.cpp ../ui/signatures.cpp -lm

test_wifi_auth: test_wifi_auth.cpp ../ui/wifi_auth.cpp ../ui/wifi_auth.h
	$(CXX) $(CXXFLAGS) -o $@ test_wifi_auth.cpp ../ui/wifi_auth.cpp -lm

test_mesh: test_mesh.cpp ../device/mesh.h
	$(CXX) $(CXXFLAGS) -o $@ test_mesh.cpp -lm

test_cluster: test_cluster.cpp ../device/balance.h ../device/chanplan.h ../device/phase.h ../device/mesh.h ../ui/model.h
	$(CXX) $(CXXFLAGS) -I../ui -o $@ test_cluster.cpp -lm

test_rehome: test_rehome.cpp ../device/rehome.h
	$(CXX) $(CXXFLAGS) -o $@ test_rehome.cpp -lm

# The BLE advertisement walk parses attacker-controlled lengths, so it runs
# under the address and UB sanitizers: a read past the payload fails the test.
test_ble_ad: test_ble_ad.cpp ../device/ble_ad.h
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ test_ble_ad.cpp -lm

# theme.json is read off an SD card anyone can write to: sanitizers, as above.
test_themepack: test_themepack.cpp ../ui/themepack.cpp ../ui/themepack.h ../ui/model.h ../ui/palette.h
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ test_themepack.cpp ../ui/themepack.cpp -lm

test_elog: test_elog.cpp ../device/elog.h
	$(CXX) $(CXXFLAGS) -o $@ test_elog.cpp -lm

test_perflog: test_perflog.cpp ../device/perflog.h
	$(CXX) $(CXXFLAGS) -o $@ test_perflog.cpp -lm

test_palette: test_palette.cpp $(PAL_DEPS) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_palette.cpp $(PAL_DEPS) -lm

test_muffin: test_muffin.cpp $(PAL_DEPS) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_muffin.cpp $(PAL_DEPS) -lm

test_sweep: test_sweep.cpp ../ui/sweep.cpp $(PAL_DEPS) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_sweep.cpp ../ui/sweep.cpp $(PAL_DEPS) -lm

# The append-only WiGLE logger core: cursors, row cap, retry, exact-once
# delivery across failed writes, and the rollover rule. Pure; faked card.
test_applog: test_applog.cpp ../device/applog.h
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ test_applog.cpp -lm

test_wiglerow: test_wiglerow.cpp ../device/wiglerow.h ../device/csv.h
	$(CXX) $(CXXFLAGS) -o $@ test_wiglerow.cpp -lm

# The DOMAIN map projection: bounding-box cull, agreement with the old
# per-node projection, the refresh cache, and a host timing of both loops.
test_plotproj: test_plotproj.cpp ../device/plotproj.h
	$(CXX) $(CXXFLAGS) -o $@ test_plotproj.cpp -lm

# The dedup probe cap::insert and the BLE callback run with interrupts off:
# bounded, never overwrites a slot when full, gates the evil-twin pass.
test_dedup: test_dedup.cpp ../device/dedup.h
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ test_dedup.cpp -lm

# When blescan stops and restarts the scan to empty the library's result map.
test_blerecycle: test_blerecycle.cpp ../device/blerecycle.h
	$(CXX) $(CXXFLAGS) -o $@ test_blerecycle.cpp -lm

# Node expiry (wrap-safe, 75 s) and the Core's camp hold after the last node.
test_meshlive: test_meshlive.cpp ../device/meshlive.h
	$(CXX) $(CXXFLAGS) -o $@ test_meshlive.cpp -lm

# The evil-twin pass budget: a token bucket of entry visits, so a beacon flood
# cannot keep interrupts off for the whole table on every fake SSID.
test_twinbudget: test_twinbudget.cpp ../device/twinbudget.h
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ test_twinbudget.cpp -lm

# AUTO LOG: when the unit may start a session itself on a GPS fix.
test_autolog: test_autolog.cpp ../device/autolog.h
	$(CXX) $(CXXFLAGS) -o $@ test_autolog.cpp -lm

test_enrollwin: test_enrollwin.cpp ../device/enrollwin.h
	$(CXX) $(CXXFLAGS) -o $@ test_enrollwin.cpp -lm

test_idlemode: test_idlemode.cpp ../device/idlemode.h ../ui/model.h ../ui/nav.cpp $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_idlemode.cpp $(SRC_NOMAIN) -lm

test_settings: test_settings.cpp $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_settings.cpp $(SRC_NOMAIN) -lm

test_motif: test_motif.cpp $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ test_motif.cpp $(SRC_NOMAIN) -lm

# The canvas's runtime geometry (portrait): stride, clip, whole-frame passes.
test_canvas: test_canvas.cpp ../ui/canvas.cpp ../ui/font5x7.cpp $(HDR)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ test_canvas.cpp ../ui/canvas.cpp ../ui/font5x7.cpp -lm

# Portrait mode: layout, hit zones, placeholder BACK, bounds, the pixel diff,
# landscape parity. Loads the sim packs, so it runs from sim/ like the rest.
test_portrait: test_portrait.cpp packload.h $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ test_portrait.cpp $(SRC_NOMAIN) -lm

# The XPT2046's landscape calibration carried to any display rotation.
test_touchmap: test_touchmap.cpp ../device/touchmap.h
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ test_touchmap.cpp -lm

test_nav: test_nav.cpp $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_nav.cpp $(SRC_NOMAIN) -lm

# Every trig call is routed through an auditing shim (trig_audit.h) so the test
# can assert none is ever handed an argument beyond the FPU-less fast path.
test_contrast: test_contrast.cpp $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_contrast.cpp $(SRC_NOMAIN) -lm

test_trig: test_trig.cpp trig_audit.h packload.h $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -include trig_audit.h -o $@ test_trig.cpp $(SRC_NOMAIN) -lm

# Links the host pack loader; PACKS_DIR is relative to sim/, where make runs it.
test_pack_parity: test_pack_parity.cpp packload.h $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_pack_parity.cpp $(SRC_NOMAIN) -lm

# FILES: the screen's state machine, its renders, and the list/delete rules
# (device/filescan.h) against the serial SDDEL's policy.
test_files: test_files.cpp ../device/filescan.h ../device/sdcmd.h ../device/sdlog.h $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -I../ui -o $@ test_files.cpp $(SRC_NOMAIN) -lm

test_footer: test_footer.cpp $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_footer.cpp $(SRC_NOMAIN) -lm

# The serial SD commands (SDPUT/SDDEL/SDRMDIR/REBOOT): the path policy, the
# strict decoders and the upload state machine against a fake card. The
# commands come off a wire anyone can type into: sanitizers, as above.
test_sdcmd: test_sdcmd.cpp fakesd.h ../device/sdcmd.h ../ui/themepack.cpp ../ui/themepack.h sdput_golden.txt
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -I../ui -o $@ test_sdcmd.cpp ../ui/themepack.cpp -lm

# The same Console on stdin/stdout, over the same fake card: the designer's
# Web Serial client (web/designer/test/usbinstall.test.mjs) is run against it.
sdcmd_console: sdcmd_console.cpp fakesd.h ../device/sdcmd.h ../ui/themepack.cpp ../ui/themepack.h
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -I../ui -o $@ sdcmd_console.cpp ../ui/themepack.cpp -lm

TESTS = test_canvas test_portrait test_touchmap test_files test_sdcmd test_dedup test_twinbudget test_meshlive test_blerecycle test_applog test_wiglerow test_plotproj test_footer test_chanplan test_gnss test_signatures test_wifi_auth test_settings test_nav test_csv test_wigle test_logging test_mesh test_phase test_cluster test_rehome test_ble_ad test_themepack test_elog test_palette test_muffin test_motif test_sweep test_enrollwin test_autolog test_idlemode test_perflog test_trig test_contrast test_pack_parity

# The committed cosmic-clone emblem must be what the converter makes of ui/muffin.h.
emblem-check:
	@python3 -I ../tools/muffin_to_emblem.py ../ui/muffin.h $(CURDIR)/.emblem.tmp >/dev/null && \
	 cmp $(CURDIR)/.emblem.tmp packs/cosmic-clone/emblem.bin && echo "emblem-check: emblem.bin matches ui/muffin.h"; \
	 rc=$$?; rm -f $(CURDIR)/.emblem.tmp; exit $$rc

# tools/sdput.py's own checks (CRC-32, hex, folder names), and the golden
# transcript test_sdcmd replays must be exactly what the tool sends today:
# the host encoder and the device decoder are compared byte for byte.
sdput-check:
	@python3 -I ../tools/sdput.py selftest >/dev/null && \
	 python3 -I ../tools/sdput.py transcript packs/cosmic-clone | cmp - sdput_golden.txt && \
	 echo "sdput-check: selftest passed, sdput_golden.txt matches tools/sdput.py"

test: $(TESTS) sdcmd_console emblem-check sdput-check
	@rc=0; for t in $(TESTS); do \
	   echo "== $$t"; ./$$t || rc=1; done; \
	 exit $$rc

# UMBRAVOX must render exactly as it did before this work started. With no
# version control there is nothing to diff against after the fact, so the
# baseline was captured up front and this target is the contract.
SCREENS = scan codex detail domain mesh omens echoes grimoire attune files
# Portrait (--orient portrait): the core screens, and CODEX for the placeholder.
# Added to the baseline when portrait landed; the landscape lines are untouched.
PORTRAIT_SCREENS = scan grimoire attune files codex

MANIFEST = $(CURDIR)/../private/baseline/MANIFEST.md5

regress: $(BIN)
	@[ -f $(MANIFEST) ] || { \
	   echo "regress: no baseline at $(MANIFEST)" >&2; \
	   echo "  see private/baseline/README.txt to re-capture it" >&2; exit 1; }
	@set -e; \
	 d=$$(mktemp -d); \
	 trap 'rm -rf "$$d"' EXIT; \
	 for s in $(SCREENS); do \
	   ./$(BIN) "$$d/$$s.png" --screen $$s --frame 0 --scale 4 >/dev/null; \
	 done; \
	 for s in $(PORTRAIT_SCREENS); do \
	   ./$(BIN) "$$d/portrait-$$s.png" --orient portrait --screen $$s --frame 0 --scale 4 >/dev/null; \
	 done; \
	 ./$(BIN) "$$d/portrait-attune-p2.png" --orient portrait --screen attune --setpage 2 --frame 0 --scale 4 >/dev/null; \
	 ./$(BIN) "$$d/portrait-alert.png" --orient portrait --alert 0 --frame 30 --scale 4 >/dev/null; \
	 cd "$$d" && md5sum -c --quiet $(MANIFEST); \
	 echo "regress: UMBRAVOX unchanged (landscape and portrait)"

# Draw-time benchmark (not part of `test`): host us/frame for the landscape
# scan screen under each theme. Run before and after touching ui/canvas.*.
bench_draw: bench.cpp packload.h $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ bench.cpp $(SRC_NOMAIN) -lm

bench: bench_draw
	@./bench_draw 2000

.PHONY: bench run sheet clean test regress emblem-check sdput-check
