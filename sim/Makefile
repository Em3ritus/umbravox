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
	rm -f $(BIN) *.png

.PHONY: run sheet clean
