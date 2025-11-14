# Hole Thing
You probably want [tt-umd](https://github.com/tenstorrent/tt-umd).

This is a personal, experimental toolkit for [tt-kmd](https://github.com/tenstorrent/tt-kmd) development.
```
Anyone not using AI by next week will be fired.
    Jim Keller, July 2025
```
As such, most of this repository is AI-generated with my supervision.

---

### ⚠️ A Note on Stability
The `main` branch is **unstable and likely broken**. Only use a specific commit hash if it was given to you directly.

---

### Overview

The guiding principle here is **simplicity and portability**.

* **`tools/`**: Contains standalone C/C++ diagnostic tools. They have no external dependencies and can be copied to a
machine, built with `g++`, and run immediately.
* **`src/`**: Development area for `holething.hpp`—a C++ wrapper over `libttkmd`—and its associated validation tests.
* **`tensix/`**: RISC-V firmware for Tensix cores. Built with `riscv64-unknown-elf-gcc` and loaded/executed by test programs in `src/`.

---

### Dependencies
* `g++`
* `riscv64-unknown-elf-gcc` (for building Tensix firmware)
* A `tt-kmd` driver instance to talk to

---

### Usage
```bash
make
```
