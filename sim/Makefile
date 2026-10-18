# No libraries. `make` and a C++17 compiler is the whole requirement, which is
# what keeps the iteration loop fast enough to actually do art in.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

BIN = aethersim
SRC = main_sim.cpp ../ui/canvas.cpp ../ui/gnss.cpp ../ui/nav.cpp ../ui/screen_codex.cpp ../ui/screen_attune.cpp ../ui/screen_detail.cpp ../ui/screen_mesh.cpp ../ui/screen_domain.cpp ../ui/screen_omens.cpp ../ui/screen_echoes.cpp ../ui/screen_grimoire.cpp ../ui/signatures.cpp ../ui/wifi_auth.cpp ../ui/demo.cpp ../ui/font5x7.cpp ../ui/runes.cpp ../ui/screen_scan.cpp
HDR = ../ui/demo.h ../ui/canvas.h ../ui/font5x7.h ../ui/runes.h ../ui/palette.h ../ui/model.h ../ui/screens.h png_writer.h

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lm

run: $(BIN)
	./$(BIN) aetherscan.png

sheet: $(BIN)
	./$(BIN) sheet.png --sheet 6

clean:
	rm -f $(BIN) $(TESTS) test_settings *.png

# The host tests each carry their own main(), so each links as its own
# binary. They are listed explicitly rather than globbed: a test that stops
# being built is a test that stops failing, and nobody notices.
UI_CORE = ../ui/canvas.cpp ../ui/font5x7.cpp ../ui/runes.cpp ../ui/palette.cpp

test_chanplan: test_chanplan.cpp ../device/chanplan.h ../device/mesh.h ../ui/model.h
	$(CXX) $(CXXFLAGS) -I../ui -o $@ test_chanplan.cpp -lm

test_gnss: test_gnss.cpp ../ui/gnss.cpp ../ui/gnss.h
	$(CXX) $(CXXFLAGS) -o $@ test_gnss.cpp ../ui/gnss.cpp -lm

test_signatures: test_signatures.cpp ../ui/signatures.cpp ../ui/signatures.h
	$(CXX) $(CXXFLAGS) -o $@ test_signatures.cpp ../ui/signatures.cpp -lm

test_wifi_auth: test_wifi_auth.cpp ../ui/wifi_auth.cpp ../ui/wifi_auth.h
	$(CXX) $(CXXFLAGS) -o $@ test_wifi_auth.cpp ../ui/wifi_auth.cpp -lm

test_settings: test_settings.cpp ../ui/nav.cpp ../ui/screen_grimoire.cpp $(UI_CORE)
	$(CXX) $(CXXFLAGS) -o $@ test_settings.cpp ../ui/nav.cpp \
	    ../ui/screen_grimoire.cpp $(UI_CORE) -lm

TESTS = test_chanplan test_gnss test_signatures test_wifi_auth

test: $(TESTS)
	@rc=0; for t in $(TESTS); do \
	   echo "== $$t"; ./$$t || rc=1; done; \
	 exit $$rc

# AETHERSCAN must render exactly as it did before this work started. With no
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
	 echo "regress: AETHERSCAN unchanged"

.PHONY: run sheet clean test regress
