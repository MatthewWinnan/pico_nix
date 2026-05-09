# pico_nix Project Context

## Overview
Mono-repo for Raspberry Pi Pico firmware projects. Mix of C/C++ (using the Pico SDK) and MicroPython. All development environments and toolchains are managed via Nix flakes.

## Repo Structure
```
pico_nix/
├── flake.nix                        # Root: all devShells + packages
├── nix/
│   ├── shells/
│   │   ├── c.nix                    # C/C++ dev shell
│   │   └── micropython.nix          # MicroPython dev shell
│   └── builders/
│       └── pico-c-project.nix       # Reusable builder fn -> .uf2/.elf derivation
├── projects/
│   └── <project-name>/
│       ├── CMakeLists.txt
│       ├── src/
│       └── package.nix              # Calls buildPicoProject, project-specific args
├── context/                         # Claude-managed persistent context (git-tracked)
└── notes -> /home/matthew/OBSIDIAN_VAULT  # Gitignored symlink
```

## Nix Design Decisions
- Single root `flake.nix` exposes all shells and packages
- **Dev shells**: `nix develop .#c` (C/C++), `nix develop .#micropython`
- **Packages**: `nix build .#<project-name>` — each project's `package.nix` calls `buildPicoProject`
- All tooling from nixpkgs unstable — well maintained, no need to pin pico-sdk from GitHub
- `pico-sdk` nixpkg sets `PICO_SDK_PATH` via setup hook automatically
- C toolchain: `gcc-arm-embedded` (ARM's pre-built GCC for Cortex-M bare metal)
- MicroPython tooling: `mpremote` (primary), `rshell`, `mpy-utils`
- Flash/debug: `picotool` (UF2/BOOTSEL), `openocd` (SWD debugging)

## Key Packages (nixpkgs unstable, as of 2026-05-08)
| Package | Version | Purpose |
|---|---|---|
| pico-sdk | 2.2.0 | C/C++ SDK (sets PICO_SDK_PATH) |
| gcc-arm-embedded | 15.2.rel1 | ARM Cortex-M toolchain |
| picotool | 2.2.0-a4 | Device interaction / flash |
| openocd | 0.12.0 | SWD debugging |
| pioasm | 2.1.1 | PIO assembler |
| micropython | 1.27.0 | MicroPython interpreter |
| mpremote | 1.25.0 | Serial device interaction |

## Project Naming Schema

Projects are named `<board>-<function>` where:

| Segment | Values | Meaning |
|---------|--------|---------|
| `pico-w` | Pico W (RP2040 + CYW43439 WiFi) | Board type |
| `pico` | Pico / Pico H (RP2040, no WiFi) | Board type |
| `pico2-w` | Pico 2 W (RP2350 + WiFi) | Board type |
| `pico2` | Pico 2 (RP2350, no WiFi) | Board type |
| `<function>` | short descriptor of purpose | e.g. `env-sensor`, `air-sensor` |

Always use `sensor` for sensor nodes that publish data, `monitor` for standalone display-only projects.

**Current projects:**
| Project | Board | Description |
|---------|-------|-------------|
| `pico-w-env-sensor` | pico_w | BMP180 + BME280 + INA219 → MQTT/HA |
| `pico-w-air-sensor` | pico_w | PMSA003 particulate + SSD1306 OLED |

## Adding a New C/C++ Project
1. Create `projects/<name>/` with `CMakeLists.txt` and `src/`
2. Create `projects/<name>/package.nix`:
   ```nix
   { pkgs, buildPicoProject }:
   buildPicoProject {
     name = "<name>";
     src = ./.;
     board = "pico";  # or "pico2" for RP2350
   }
   ```
3. Add to root `flake.nix` packages: `<name> = mkProject ./projects/<name>/package.nix;`
4. Build: `nix build .#<name>` → outputs `.uf2`, `.elf`, `.bin` in `result/`

## Adding a New MicroPython Project
- No derivation needed for source-only projects
- Use `nix develop .#micropython` then `mpremote connect auto` for interactive dev
- If packaging is desired, a simple copy derivation can be added later

## Sessions Log
- 2026-05-08: Repo initialized. Set up context dir, notes symlink, .gitignore.
- 2026-05-08: Nix structure designed and implemented. Shells + builder in place. Ready for first real project.
