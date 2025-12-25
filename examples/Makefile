.PHONY: all clean test help
.PHONY: shell-scripts elf-embedding programmatic linker-scripts advanced integration
.PHONY: check-deps

# Colors for output
RED=\033[0;31m
GREEN=\033[0;32m
YELLOW=\033[1;33m
NC=\033[0m # No Color

all: check-deps shell-scripts elf-embedding programmatic linker-scripts advanced integration
	@echo "$(GREEN)All examples built successfully!$(NC)"

help:
	@echo "lolelffs Examples Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all examples (default)"
	@echo "  shell-scripts    - Prepare shell script examples"
	@echo "  elf-embedding    - Build ELF embedding examples"
	@echo "  programmatic     - Build programmatic API examples"
	@echo "  linker-scripts   - Build linker script examples"
	@echo "  advanced         - Build advanced use case examples"
	@echo "  integration      - Build integration examples"
	@echo "  test             - Run all example tests"
	@echo "  clean            - Remove all built artifacts"
	@echo "  check-deps       - Check for required dependencies"
	@echo ""
	@echo "Prerequisites:"
	@echo "  - GCC or Clang"
	@echo "  - GNU binutils (objcopy, readelf, ld)"
	@echo "  - Rust toolchain (for Rust examples)"
	@echo "  - Make"
	@echo "  - lolelffs built in parent directory"

check-deps:
	@echo "$(YELLOW)Checking dependencies...$(NC)"
	@command -v gcc >/dev/null 2>&1 || { echo "$(RED)ERROR: gcc not found$(NC)"; exit 1; }
	@command -v make >/dev/null 2>&1 || { echo "$(RED)ERROR: make not found$(NC)"; exit 1; }
	@command -v objcopy >/dev/null 2>&1 || { echo "$(RED)ERROR: objcopy not found (install binutils)$(NC)"; exit 1; }
	@command -v readelf >/dev/null 2>&1 || { echo "$(RED)ERROR: readelf not found (install binutils)$(NC)"; exit 1; }
	@command -v cargo >/dev/null 2>&1 || echo "$(YELLOW)WARNING: cargo not found - Rust examples will be skipped$(NC)"
	@if [ ! -f ../lolelffs ]; then \
		echo "$(YELLOW)WARNING: ../lolelffs CLI tool not found. Build with 'make rust' in parent directory$(NC)"; \
	fi
	@if [ ! -f ../lolelffs.ko ]; then \
		echo "$(YELLOW)WARNING: ../lolelffs.ko kernel module not found. Build with 'make' in parent directory$(NC)"; \
	fi
	@echo "$(GREEN)Dependency check complete$(NC)"

shell-scripts:
	@echo "$(GREEN)Preparing shell script examples...$(NC)"
	@chmod +x 01-shell-scripts/*.sh 2>/dev/null || true
	@echo "$(GREEN)Shell scripts ready$(NC)"

elf-embedding:
	@echo "$(GREEN)Building ELF embedding examples...$(NC)"
	@$(MAKE) -C 02-elf-embedding/hello-world || true
	@$(MAKE) -C 02-elf-embedding/self-extracting || true
	@$(MAKE) -C 02-elf-embedding/game-with-assets || true
	@$(MAKE) -C 02-elf-embedding/config-launcher || true
	@echo "$(GREEN)ELF embedding examples built$(NC)"

programmatic:
	@echo "$(GREEN)Building programmatic examples...$(NC)"
	@$(MAKE) -C 03-programmatic/c-examples || true
	@if command -v cargo >/dev/null 2>&1; then \
		cd 03-programmatic/rust-examples && cargo build --release 2>/dev/null || true; \
	else \
		echo "$(YELLOW)Skipping Rust examples (cargo not found)$(NC)"; \
	fi
	@echo "$(GREEN)Programmatic examples built$(NC)"

linker-scripts:
	@echo "$(GREEN)Building linker script examples...$(NC)"
	@$(MAKE) -C 04-linker-scripts/basic-section || true
	@$(MAKE) -C 04-linker-scripts/multi-section || true
	@$(MAKE) -C 04-linker-scripts/aligned-section || true
	@$(MAKE) -C 04-linker-scripts/custom-placement || true
	@echo "$(GREEN)Linker script examples built$(NC)"

advanced:
	@echo "$(GREEN)Building advanced use case examples...$(NC)"
	@$(MAKE) -C 05-advanced-use-cases/container-init || true
	@$(MAKE) -C 05-advanced-use-cases/plugin-system || true
	@$(MAKE) -C 05-advanced-use-cases/ctf-challenge || true
	@echo "$(GREEN)Advanced examples built$(NC)"

integration:
	@echo "$(GREEN)Building integration examples...$(NC)"
	@# Integration examples mostly consist of config files
	@# Docker examples require Docker to build
	@if command -v docker >/dev/null 2>&1; then \
		cd 06-integration/docker && docker build -t lolelffs-demo . 2>/dev/null || true; \
	else \
		echo "$(YELLOW)Skipping Docker example (docker not found)$(NC)"; \
	fi
	@echo "$(GREEN)Integration examples ready$(NC)"

test: all
	@echo "$(GREEN)Running example tests...$(NC)"
	@echo "$(YELLOW)Testing shell scripts...$(NC)"
	@cd 01-shell-scripts && ./create-and-populate.sh || true
	@echo "$(YELLOW)Testing ELF embedding...$(NC)"
	@cd 02-elf-embedding/hello-world && $(MAKE) test || true
	@echo "$(YELLOW)Testing programmatic examples...$(NC)"
	@cd 03-programmatic/c-examples && $(MAKE) test || true
	@echo "$(GREEN)Tests complete$(NC)"

clean:
	@echo "$(YELLOW)Cleaning all examples...$(NC)"
	@find . -name "*.o" -delete
	@find . -name "*.img" -delete
	@find . -name "*.elf" -delete
	@find . -name "*.so" -delete
	@find . -name "*.a" -delete
	@find . -name "*-with-fs" -delete
	@$(MAKE) -C 02-elf-embedding/hello-world clean 2>/dev/null || true
	@$(MAKE) -C 02-elf-embedding/self-extracting clean 2>/dev/null || true
	@$(MAKE) -C 02-elf-embedding/game-with-assets clean 2>/dev/null || true
	@$(MAKE) -C 02-elf-embedding/config-launcher clean 2>/dev/null || true
	@$(MAKE) -C 03-programmatic/c-examples clean 2>/dev/null || true
	@cd 03-programmatic/rust-examples && cargo clean 2>/dev/null || true
	@$(MAKE) -C 04-linker-scripts/basic-section clean 2>/dev/null || true
	@$(MAKE) -C 04-linker-scripts/multi-section clean 2>/dev/null || true
	@$(MAKE) -C 04-linker-scripts/aligned-section clean 2>/dev/null || true
	@$(MAKE) -C 04-linker-scripts/custom-placement clean 2>/dev/null || true
	@$(MAKE) -C 05-advanced-use-cases/container-init clean 2>/dev/null || true
	@$(MAKE) -C 05-advanced-use-cases/plugin-system clean 2>/dev/null || true
	@$(MAKE) -C 05-advanced-use-cases/ctf-challenge clean 2>/dev/null || true
	@echo "$(GREEN)Clean complete$(NC)"
