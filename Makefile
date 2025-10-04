.PHONY: all configure build test clean rebuild help dqxclarity test-dqxclarity

BUILD_DIR := out/linux-clang-release
PRESET := linux-clang-release

help:
	@echo "DQX Utility Development Makefile"
	@echo "================================"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  configure       - Configure CMake build"
	@echo "  build           - Build all targets"
	@echo "  test            - Run all tests"
	@echo "  clean           - Clean build directory"
	@echo "  rebuild         - Clean and rebuild"
	@echo "  dqxclarity      - Build only dqxclarity-cpp"
	@echo "  test-dqxclarity - Run only dqxclarity tests"
	@echo "  all             - Configure, build, and test (default)"
	@echo ""

all: configure build test

configure:
	@echo "Configuring build with preset: $(PRESET)..."
	@cmake --preset $(PRESET)

build:
	@echo "Building project..."
	@cmake --build --preset $(PRESET) -j4

test:
	@echo "Running all tests..."
	@cd $(BUILD_DIR) && ./dqx_utility_tests

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)

rebuild: clean configure build

dqxclarity:
	@echo "Building dqxclarity-cpp only..."
	@cmake --build --preset $(PRESET) --target dqxclarity-cpp -j4

test-dqxclarity:
	@echo "Running dqxclarity tests..."
	@cd $(BUILD_DIR) && ./dqx_utility_tests "[memory],[pattern],[region],[scanner],[signatures]"
	@echo ""
	@echo "Test Summary:"
	@echo "  Pattern Scanner: PASS"
	@echo "  Memory Regions:  PASS"
	@echo "  Signatures:      PASS"
