CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -pthread

LIBS     = -lsfml-graphics -lsfml-window -lsfml-audio -lsfml-system -lrt
TUI_LIBS = -lncurses -lrt

# A2 FIX: Executables named exactly as Docker guide Section 3 specifies.
# We output to arbiter_bin/hip_bin/asp_bin then rename to avoid colliding
# with source folders arbiter/ hip/ asp/ during the build.
all: clean_bins arbiter_bin hip_bin hip_gui asp_bin rename
	@echo "Build complete."
	@echo "Run inside Docker: bash run.sh"

arbiter_bin: arbiter/arbiter.cpp arbiter/shared.h
	$(CXX) $(CXXFLAGS) arbiter/*.cpp -o arbiter_bin $(TUI_LIBS)

hip_bin: hip/hip.cpp hip/shared.h
	$(CXX) $(CXXFLAGS) hip/hip.cpp -o hip_bin $(TUI_LIBS)

hip_gui: hip/hip_gui.cpp hip/shared.h
	$(CXX) $(CXXFLAGS) hip/hip_gui.cpp -o hip_gui $(LIBS)

asp_bin: asp/asp.cpp asp/shared.h
	$(CXX) $(CXXFLAGS) asp/*.cpp -o asp_bin $(TUI_LIBS)

rename:
	mv arbiter_bin arbiter
	mv hip_bin hip
	mv asp_bin asp

clean_bins:
	@test -f arbiter_bin && rm arbiter_bin || true
	@test -f hip_bin     && rm hip_bin     || true
	@test -f asp_bin     && rm asp_bin     || true
	@test -f hip_gui     && rm hip_gui     || true
	@test -f arbiter     && rm arbiter     || true
	@test -f hip         && rm hip         || true
	@test -f asp         && rm asp         || true

clean: clean_bins

.PHONY: all clean clean_bins rename
