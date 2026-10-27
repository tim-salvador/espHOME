CORE := ../../components/stair_engine
CXXFLAGS ?= -std=gnu++20 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Werror
SRCS := sim.cpp $(CORE)/stair_geometry.cpp $(CORE)/stair_effects_builtin.cpp $(CORE)/stair_effects_ambient.cpp $(CORE)/stair_effects_custom.cpp $(CORE)/stair_director.cpp

.PHONY: all run show clean
all: run

sim: $(SRCS) $(wildcard $(CORE)/*.h)
	$(CXX) $(CXXFLAGS) -I$(CORE) $(SRCS) -o $@

run: sim
	./sim

show: sim
	./sim --show

clean:
	rm -f sim
