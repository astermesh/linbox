BUILD_DIR ?= build
CMAKE ?= cmake
CTEST ?= ctest
CLANG_FORMAT ?= clang-format

C_FORMAT_FILES := $(shell find src -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null)

.PHONY: build debug release asan test clean fmt lint

build:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(BUILD_DIR)

debug:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(BUILD_DIR)

release:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR)

asan:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=ASAN
	$(CMAKE) --build $(BUILD_DIR)

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

fmt:
	@if [ -n "$(C_FORMAT_FILES)" ]; then \
		$(CLANG_FORMAT) -i $(C_FORMAT_FILES); \
	else \
		echo "No C files to format."; \
	fi

lint:
	@if [ -n "$(C_FORMAT_FILES)" ]; then \
		$(CLANG_FORMAT) --dry-run --Werror $(C_FORMAT_FILES); \
	else \
		echo "No C files to lint."; \
	fi

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)
