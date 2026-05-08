## pico_nix – Root Justfile
## Run `just` or `just --list` to see available recipes.

default:
    @just --list --unsorted

# ── Project builds ────────────────────────────────────────────────────────────

# Build hello-world
build-hello-world:
    cd projects/hello-world && just build

# Build pico-w-ha-sensor and link compile_commands.json at repo root.
build-pico-w-ha-sensor:
    cd projects/pico-w-ha-sensor && just build
    ln -sf projects/pico-w-ha-sensor/build/compile_commands.json compile_commands.json

# Build bmp180-sensor and link compile_commands.json at repo root.
# The root-level symlink lets clangd resolve headers for libs/bmp180/ — clangd
# walks up the directory tree from the file being edited, so placing it here
# covers both projects/ and libs/ source trees.
build-bmp180:
    cd projects/bmp180-sensor && just build
    ln -sf projects/bmp180-sensor/build/compile_commands.json compile_commands.json

# ── Docs / Datasheets ────────────────────────────────────────────────────────

# Download pinout diagrams for Pico (RP2040) and Pico 2 (RP2350) as PNG.
# Requires poppler-utils (pdftoppm) — available in `nix develop`.
fetch-pinouts:
    mkdir -p docs/pinouts
    curl -fL -o docs/pinouts/_pico-rp2040-pinout.pdf \
        https://datasheets.raspberrypi.com/pico/Pico-R3-A4-Pinout.pdf
    curl -fL -o docs/pinouts/_pico2-rp2350-pinout.pdf \
        https://datasheets.raspberrypi.com/pico/Pico-2-Pinout.pdf
    pdftoppm -r 250 -png -singlefile \
        docs/pinouts/_pico-rp2040-pinout.pdf  docs/pinouts/pico-rp2040-pinout
    pdftoppm -r 250 -png -singlefile \
        docs/pinouts/_pico2-rp2350-pinout.pdf docs/pinouts/pico2-rp2350-pinout
    rm docs/pinouts/_*.pdf
    @echo "Pinouts saved to docs/pinouts/ (PNG, 250 dpi)"

# Download chip and board datasheets for RP2040, RP2350, and current components
fetch-datasheets:
    mkdir -p docs/datasheets
    # RP2040 chip + Pico board
    curl -fL -o docs/datasheets/rp2040-datasheet.pdf \
        https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf
    curl -fL -o docs/datasheets/pico-board-datasheet.pdf \
        https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf
    # RP2350 chip + Pico 2 board
    curl -fL -o docs/datasheets/rp2350-datasheet.pdf \
        https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf
    curl -fL -o docs/datasheets/pico2-board-datasheet.pdf \
        https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf
    # Components
    curl -fL -o docs/datasheets/bmp180-datasheet.pdf \
        https://cdn-shop.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf
    # INA219 current/power monitor (used on Waveshare Pico-UPS-A)
    curl -fL -o docs/datasheets/ina219-datasheet.pdf \
        https://www.ti.com/lit/ds/symlink/ina219.pdf
    # Waveshare Pico-UPS-A schematic
    curl -fL -o docs/datasheets/pico-ups-a-schematic.pdf \
        https://files.waveshare.com/upload/4/45/Pico-UPS-A_Schematic.pdf
    @echo "Datasheets saved to docs/datasheets/"

# Download all docs (pinouts + datasheets)
fetch-docs: fetch-pinouts fetch-datasheets
    @echo "All docs fetched."

# List what has been downloaded
list-docs:
    @find docs -type f \( -name "*.pdf" -o -name "*.png" \) | sort
