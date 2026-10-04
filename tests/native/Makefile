# Tests natifs de la logique du firmware (sans matériel, sans Arduino).
#   make        compile et exécute tous les tests
CORE = ../../trimbox_s3/src/core
SRC  = $(wildcard $(CORE)/*.cpp)
CXX ?= g++
CXXFLAGS = -std=c++17 -O1 -Wall -Wextra -Werror -I$(CORE)
TESTS = test_proto test_lap test_logic

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done
	@echo "TOUS LES TESTS NATIFS PASSENT"

%: %.cpp $(SRC) t.h
	$(CXX) $(CXXFLAGS) -o $@ $< $(SRC) -lm

clean:
	rm -f $(TESTS) *.json
.PHONY: all clean
