# No libraries. `make` and a C++17 compiler is the whole requirement, which is
# what keeps the iteration loop fast enough to actually do art in.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

BIN = umbravox
SRC = main_sim.cpp ../ui/palette.cpp ../ui/canvas.cpp ../ui/gnss.cpp ../ui/nav.cpp ../ui/screen_codex.cpp ../ui/screen_attune.cpp ../ui/screen_detail.cpp ../ui/screen_mesh.cpp ../ui/screen_domain.cpp ../ui/screen_omens.cpp ../ui/screen_echoes.cpp ../ui/screen_grimoire.cpp ../ui/signatures.cpp ../ui/wifi_auth.cpp ../ui/demo.cpp ../ui/font5x7.cpp ../ui/runes.cpp ../ui/screen_scan.cpp ../ui/seal.cpp ../ui/muffinseal.cpp ../ui/themepack.cpp ../ui/sweep.cpp ../ui/screen_aspect.cpp
# Every ui header, found rather than listed. The hand-written list silently
# omitted nav.h, layout.h, signatures.h and wifi_auth.h, so editing any of
# them left umbravox stale -- and a stale binary makes `regress` pass while
# testing code that no longer exists. Later tasks add headers here; a wildcard
# cannot forget them.
HDR = $(wildcard ../ui/*.h) png_writer.h

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lm

run: $(BIN)
	./$(BIN) umbravox.png

sheet: $(BIN)
	./$(BIN) sheet.png --sheet 6

clean:
	rm -f $(BIN) $(TESTS) *.png

# The host tests each carry their own main(), so each links as its own
# binary. They are listed explicitly rather than globbed: a test that stops
# being built is a test that stops failing, and nobody notices.
# palette.cpp reaches themepack.cpp (sanitizeStatus) and muffinseal.cpp
# (muffinEmblem), so any rule linking it links those too.
PAL_DEPS = ../ui/palette.cpp ../ui/themepack.cpp ../ui/muffinseal.cpp ../ui/canvas.cpp ../ui/font5x7.cpp
UI_CORE = ../ui/runes.cpp $(PAL_DEPS)

test_chanplan: test_chanplan.cpp ../device/chanplan.h ../device/mesh.h ../ui/model.h
	$(CXX) $(CXXFLAGS) -I../ui -o $@ test_chanplan.cpp -lm

test_phase: test_phase.cpp ../device/phase.h
	$(CXX) $(CXXFLAGS) -o $@ test_phase.cpp -lm

test_wigle: test_wigle.cpp ../device/wigle.h
	$(CXX) $(CXXFLAGS) -o $@ test_wigle.cpp -lm

test_logging: test_logging.cpp ../device/logging.cpp ../device/logging.h ../device/sdlog.h ../device/csv.h
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
test_enrollwin: test_enrollwin.cpp ../device/enrollwin.h
	$(CXX) $(CXXFLAGS) -o $@ test_enrollwin.cpp -lm

# nav.cpp reaches every screen, and the screens share chrome and name tables,
# so this links the same sources as the simulator (minus its main) rather than
# a hand-picked subset that breaks each time a screen grows a dependency.
SRC_NOMAIN = $(filter-out main_sim.cpp,$(SRC))

test_settings: test_settings.cpp $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_settings.cpp $(SRC_NOMAIN) -lm

test_nav: test_nav.cpp $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_nav.cpp $(SRC_NOMAIN) -lm

# Every trig call is routed through an auditing shim (trig_audit.h) so the test
# can assert none is ever handed an argument beyond the FPU-less fast path.
test_contrast: test_contrast.cpp $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_contrast.cpp $(SRC_NOMAIN) -lm

test_trig: test_trig.cpp trig_audit.h $(SRC_NOMAIN) $(HDR)
	$(CXX) $(CXXFLAGS) -include trig_audit.h -o $@ test_trig.cpp $(SRC_NOMAIN) -lm

TESTS = test_chanplan test_gnss test_signatures test_wifi_auth test_settings test_nav test_csv test_wigle test_logging test_mesh test_phase test_cluster test_rehome test_ble_ad test_themepack test_elog test_palette test_muffin test_sweep test_enrollwin test_perflog test_trig test_contrast

test: $(TESTS)
	@rc=0; for t in $(TESTS); do \
	   echo "== $$t"; ./$$t || rc=1; done; \
	 exit $$rc

# UMBRAVOX must render exactly as it did before this work started. With no
# version control there is nothing to diff against after the fact, so the
# baseline was captured up front and this target is the contract.
SCREENS = scan codex detail domain mesh omens echoes grimoire attune

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
	 cd "$$d" && md5sum -c --quiet $(MANIFEST); \
	 echo "regress: UMBRAVOX unchanged"

.PHONY: run sheet clean test regress
