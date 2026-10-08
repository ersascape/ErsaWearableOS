.PHONY: all firmware test docs clean

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -I include -I src -I tests

TEST_SRCS = \
	tests/main_test.cpp \
	tests/apple_protocol_test.cpp \
	src/ersa/common/calendar_time.cpp \
	src/ersa/events/event_bus.cpp \
	src/ersa/app/application_manager.cpp \
	src/ersa/services/time_service.cpp \
	src/ersa/services/power_manager.cpp \
	src/ersa/services/display_manager.cpp \
	src/ersa/services/network_manager.cpp \
	src/ersa/services/storage_service.cpp \
	src/ersa/services/logging_service.cpp \
	src/ersa/services/settings_service.cpp \
	src/ersa/services/bluetooth_manager.cpp \
	src/ersa/services/session_stats.cpp \
	src/ersa/board.cpp

TEST_BIN = tests/run_tests
HEADERS = $(shell find include -type f -name '*.h')

all: firmware test

firmware:
	./scripts/pio.sh run

test: $(TEST_BIN)
	./$(TEST_BIN)

docs:
	./scripts/build_docs.sh

$(TEST_BIN): $(TEST_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_SRCS)

clean:
	rm -f $(TEST_BIN)
	./scripts/pio.sh run -t clean || true
