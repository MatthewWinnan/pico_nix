# pico_nix

Mono-repo for Raspberry Pi Pico firmware projects. Targets both C/C++ (Pico SDK) and MicroPython. All development environments and toolchains are managed with Nix flakes — no manual SDK or toolchain installation required.

## Requirements

- [Nix](https://nixos.org/download/) with flakes enabled

## Dev Shells

Two language-specific shells are provided. Enter one with `nix develop`:

```sh
# C/C++ — pico-sdk, gcc-arm-embedded, cmake, picotool, openocd, tio, picocom
nix develop .#c

# MicroPython — mpremote, rshell, mpy-utils, tio, picocom
nix develop .#micropython
```

## Serial Communication

Both shells include `tio` (preferred) and `picocom` for serial monitoring. `tio` automatically reconnects when the Pico re-enumerates after a flash, which makes it the better choice during development.

Use `lsusb` to identify which port the device is on, then:

```sh
tio /dev/ttyACM0          # connect at default 115200 baud
tio -b 9600 /dev/ttyACM0  # specify baud rate explicitly
```

## RPi Debug Probe

The [Debug Probe](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html) exposes two interfaces over USB:

| Interface | Protocol | Use |
|---|---|---|
| CMSIS-DAP | SWD | Flashing and GDB debugging via openocd |
| USB-UART bridge | CDC ACM | Serial monitor (appears as `/dev/ttyACM*`) |

**Flash and reset without BOOTSEL:**

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "program result/<project>.elf verify reset exit"
```

**Start a GDB debug session:**

```sh
# Terminal 1 — start openocd server
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg

# Terminal 2 — connect GDB
arm-none-eabi-gdb result/<project>.elf \
  -ex "target extended-remote :3333" \
  -ex "monitor reset halt"
```

**Monitor serial output from the probe's UART bridge:**

```sh
tio /dev/ttyACM1   # ACM0 is usually CMSIS-DAP, ACM1 the UART — verify with lsusb
```

> **Note — udev rules:** By default, USB devices require root access. To use the debug probe and serial ports as a normal user, add yourself to the `dialout` group and configure udev rules. On NixOS, add to your system config:
> ```nix
> users.users.<youruser>.extraGroups = [ "dialout" ];
> services.udev.packages = [ pkgs.openocd ];
> ```
> The `pkgs.openocd` udev package includes rules for CMSIS-DAP devices.

## Building a Project

Each project under `projects/` is a separate Nix package. Build any project by name:

```sh
nix build .#<project-name>
```

Output files (`.uf2`, `.elf`, `.bin`) are placed in `result/`. Flash with drag-and-drop (UF2):

```sh
# Hold BOOTSEL on the Pico while plugging in, then:
picotool load result/*.uf2 --force
```

Or via the debug probe (no BOOTSEL required) — see above.

## Repo Structure

```
pico_nix/
├── flake.nix                    # Root flake — all dev shells and packages
├── nix/
│   ├── shells/
│   │   ├── c.nix                # C/C++ dev shell definition
│   │   └── micropython.nix      # MicroPython dev shell definition
│   └── builders/
│       └── pico-c-project.nix   # Shared builder for C/C++ firmware projects
└── projects/
    └── <project-name>/
        ├── CMakeLists.txt
        ├── src/
        └── package.nix          # Project-specific build args
```

## Adding a New C/C++ Project

1. Create `projects/<name>/` with your `CMakeLists.txt` and source files.
2. Add a `package.nix`:

```nix
{ pkgs, buildPicoProject }:
buildPicoProject {
  name = "<name>";
  src = ./.;
  board = "pico";  # or "pico2" for RP2350-based boards
}
```

3. Register it in `flake.nix` under `packages`:

```nix
packages = {
  "<name>" = mkProject ./projects/<name>/package.nix;
};
```

4. Build: `nix build .#<name>`

## Adding a New MicroPython Project

No build step is needed for source-only MicroPython projects. Enter the shell and use `mpremote` directly:

```sh
nix develop .#micropython
mpremote connect auto run projects/<name>/main.py
mpremote connect auto fs cp projects/<name>/main.py :main.py
```

## License

MIT — see [LICENSE](LICENSE).
