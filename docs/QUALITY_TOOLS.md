# MCPP Quality Tools

This document describes the quality tools available in the MCPP project and how to use them.

## Quick Start

```bash
# Run all quality checks
make quality

# Before committing
make format-check  # Verify formatting
make quality       # Run all checks
```

## Available Tools

### 1. Code Formatting (clang-format)

Ensures consistent code style across the project.

```bash
# Check formatting (non-destructive)
make format-check

# Auto-format code
make format
```

**Configuration:** `.clang-format`

**Style:** Based on Google style with modifications for modern C++20.

### 2. Static Analysis (clang-tidy)

Catches bugs, style issues, and suggests modernizations.

```bash
# Enable during build
cmake -DMCPP_ENABLE_CLANG_TIDY=ON -B build
make

# Or run manually
clang-tidy src/*.cpp -- -Iinclude -std=c++20
```

**Configuration:** `.clang-tidy`

**Checks enabled:**
- `bugprone-*` - Common bug patterns
- `cert-*` - CERT secure coding guidelines
- `cppcoreguidelines-*` - C++ Core Guidelines
- `modernize-*` - Modern C++ suggestions
- `performance-*` - Performance improvements
- `readability-*` - Code readability

### 3. Static Analysis (cppcheck)

Complementary static analyzer that catches different issues than clang-tidy.

```bash
# Run standalone
make cppcheck

# Enable during build
cmake -DMCPP_ENABLE_CPPCHECK=ON -B build
make
```

**Checks enabled:**
- Warning, performance, portability, style

### 4. Include What You Use (IWYU)

Ensures headers are properly included and removes unnecessary ones.

```bash
cmake -DMCPP_ENABLE_INCLUDE_WHAT_YOU_USE=ON -B build
make
```

### 5. Code Coverage

Measures test coverage to identify untested code.

```bash
# Build with coverage
cmake -DMCPP_ENABLE_COVERAGE=ON -B build
cd build
make

# Generate coverage report
make coverage

# Open in browser
make coverage-open
```

**Requirements:** `lcov`, `genhtml`

**Report location:** `build/coverage_report/index.html`

### 6. Sanitizers

Runtime error detection for memory bugs, undefined behavior, and data races.

#### AddressSanitizer (ASan)

Detects memory errors: buffer overflows, use-after-free, memory leaks.

```bash
cmake -DMCPP_ENABLE_ASAN=ON -B build
make
./build/mcpp_tests
```

#### UndefinedBehaviorSanitizer (UBSan)

Detects undefined behavior: integer overflow, null pointer dereference, etc.

```bash
cmake -DMCPP_ENABLE_UBSAN=ON -B build
make
./build/mcpp_tests
```

#### ThreadSanitizer (TSan)

Detects data races in multithreaded code.

```bash
cmake -DMCPP_ENABLE_TSAN=ON -B build
make
./build/mcpp_tests
```

**Note:** Don't combine ASan with TSan (incompatible).

## CMake Options Summary

| Option | Description |
|--------|-------------|
| `MCPP_ENABLE_COVERAGE` | Enable code coverage instrumentation |
| `MCPP_ENABLE_ASAN` | Enable AddressSanitizer |
| `MCPP_ENABLE_UBSAN` | Enable UndefinedBehaviorSanitizer |
| `MCPP_ENABLE_TSAN` | Enable ThreadSanitizer |
| `MCPP_ENABLE_CLANG_TIDY` | Enable clang-tidy during builds |
| `MCPP_ENABLE_CPPCHECK` | Enable cppcheck during builds |
| `MCPP_ENABLE_INCLUDE_WHAT_YOU_USE` | Enable IWYU during builds |

## Make Targets Summary

| Target | Description |
|--------|-------------|
| `make quality` | Run all quality checks (tests + format + cppcheck) |
| `make format-check` | Check code formatting |
| `make format` | Auto-format code |
| `make cppcheck` | Run cppcheck static analysis |
| `make coverage` | Generate coverage report |
| `make coverage-open` | Generate and open coverage report |
| `make help-quality` | Show quality targets help |

## Installation

### macOS (Homebrew)

```bash
brew install llvm           # clang-format, clang-tidy
brew install cppcheck
brew install include-what-you-use
brew install lcov           # For coverage
```

### Ubuntu/Debian

```bash
sudo apt install clang-format clang-tidy
sudo apt install cppcheck
sudo apt install iwyu
sudo apt install lcov
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Quality Checks

on: [push, pull_request]

jobs:
  quality:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y clang-format cppcheck lcov libcurl4-openssl-dev
      
      - name: Configure
        run: cmake -DMCPP_ENABLE_COVERAGE=ON -B build
      
      - name: Build
        run: cmake --build build -j$(nproc)
      
      - name: Format check
        run: make -C build format-check
      
      - name: Cppcheck
        run: make -C build cppcheck
      
      - name: Test with coverage
        run: make -C build coverage
      
      - name: Upload coverage
        uses: codecov/codecov-action@v3
        with:
          files: build/coverage.info

  sanitizers:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        sanitizer: [ASAN, UBSAN]
    steps:
      - uses: actions/checkout@v4
      
      - name: Install dependencies
        run: sudo apt install -y libcurl4-openssl-dev
      
      - name: Configure with ${{ matrix.sanitizer }}
        run: cmake -DMCPP_ENABLE_${{ matrix.sanitizer }}=ON -B build
      
      - name: Build
        run: cmake --build build -j$(nproc)
      
      - name: Test
        run: ./build/mcpp_tests
```

## What Each Tool Catches

| Issue Type | clang-format | clang-tidy | cppcheck | ASan | UBSan | TSan | Coverage |
|------------|:------------:|:----------:|:--------:|:----:|:-----:|:----:|:--------:|
| Style/formatting | ✅ | | | | | | |
| Bug patterns | | ✅ | ✅ | | | | |
| Memory leaks | | | | ✅ | | | |
| Buffer overflow | | | | ✅ | | | |
| Use-after-free | | | | ✅ | | | |
| Integer overflow | | ✅ | | | ✅ | | |
| Null dereference | | ✅ | | ✅ | ✅ | | |
| Data races | | | | | | ✅ | |
| Dead code | | ✅ | ✅ | | | | ✅ |
| Unused includes | | | | | | | |
| Modernization | | ✅ | | | | | |
| Performance | | ✅ | ✅ | | | | |

## Recommended Workflow

### During Development

1. Write code
2. Run `make format` to auto-format
3. Run `make` to compile (with clang-tidy if enabled)
4. Run tests

### Before Committing

```bash
make format-check  # Ensure formatting
make quality       # Run all checks
```

### For Thorough Testing

```bash
# Full quality suite with coverage
cmake -DMCPP_ENABLE_COVERAGE=ON -B build
cd build
make
make quality
make coverage-open
```

### Debugging Memory Issues

```bash
cmake -DMCPP_ENABLE_ASAN=ON -DMCPP_ENABLE_UBSAN=ON -B build
cd build
make
./mcpp_tests  # Will report any memory errors
```

## Suppressing False Positives

### clang-tidy

```cpp
// NOLINTNEXTLINE(bugprone-exception-escape)
void my_function() { ... }

// Or for a range:
// NOLINTBEGIN(readability-magic-numbers)
const int BUFFER_SIZE = 1024;
// NOLINTEND(readability-magic-numbers)
```

### cppcheck

```cpp
// cppcheck-suppress unusedFunction
void internal_helper() { ... }
```

### clang-format

```cpp
// clang-format off
const int matrix[3][3] = {
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1}
};
// clang-format on
```

