## pico_nix – Root Justfile
## Run `just` or `just --list` to see available recipes.

default:
    @just --list --unsorted

# ── Project builds ────────────────────────────────────────────────────────────

# Build hello-world
build-hello-world:
    cd projects/hello-world && just build

# Build pico-w-env-sensor and link compile_commands.json at repo root.
build-pico-w-env-sensor:
    cd projects/pico-w-env-sensor && just build
    ln -sf projects/pico-w-env-sensor/build/compile_commands.json compile_commands.json

# Build pico-w-air-sensor and link compile_commands.json at repo root.
build-pico-w-air-sensor:
    cd projects/pico-w-air-sensor && just build
    ln -sf projects/pico-w-air-sensor/build/compile_commands.json compile_commands.json

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
# Skips any pinout that already exists on disk.
fetch-pinouts:
    mkdir -p docs/pinouts
    test -f docs/pinouts/pico-rp2040-pinout.png || { \
        curl -fL -o docs/pinouts/_pico-rp2040-pinout.pdf \
            https://datasheets.raspberrypi.com/pico/Pico-R3-A4-Pinout.pdf && \
        pdftoppm -r 250 -png -singlefile \
            docs/pinouts/_pico-rp2040-pinout.pdf docs/pinouts/pico-rp2040-pinout && \
        rm docs/pinouts/_pico-rp2040-pinout.pdf; \
    }
    test -f docs/pinouts/pico2-rp2350-pinout.png || { \
        curl -fL -o docs/pinouts/_pico2-rp2350-pinout.pdf \
            https://datasheets.raspberrypi.com/pico/Pico-2-Pinout.pdf && \
        pdftoppm -r 250 -png -singlefile \
            docs/pinouts/_pico2-rp2350-pinout.pdf docs/pinouts/pico2-rp2350-pinout && \
        rm docs/pinouts/_pico2-rp2350-pinout.pdf; \
    }
    @echo "Pinouts up to date in docs/pinouts/"

# Download chip and board datasheets for RP2040, RP2350, and current components.
# Skips any file that already exists on disk.
fetch-datasheets:
    mkdir -p docs/datasheets
    # RP2040 chip + Pico board
    test -f docs/datasheets/rp2040-datasheet.pdf || \
        curl -fL -o docs/datasheets/rp2040-datasheet.pdf \
            https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf
    test -f docs/datasheets/pico-board-datasheet.pdf || \
        curl -fL -o docs/datasheets/pico-board-datasheet.pdf \
            https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf
    # RP2350 chip + Pico 2 board
    test -f docs/datasheets/rp2350-datasheet.pdf || \
        curl -fL -o docs/datasheets/rp2350-datasheet.pdf \
            https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf
    test -f docs/datasheets/pico2-board-datasheet.pdf || \
        curl -fL -o docs/datasheets/pico2-board-datasheet.pdf \
            https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf
    # Components
    test -f docs/datasheets/bmp180-datasheet.pdf || \
        curl -fL -o docs/datasheets/bmp180-datasheet.pdf \
            https://cdn-shop.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf
    test -f docs/datasheets/bme280-datasheet.pdf || \
        curl -fL -o docs/datasheets/bme280-datasheet.pdf \
            https://cdn-shop.adafruit.com/datasheets/BST-BME280_DS001-10.pdf
    # INA219 current/power monitor (used on Waveshare Pico-UPS-A)
    test -f docs/datasheets/ina219-datasheet.pdf || \
        curl -fL -o docs/datasheets/ina219-datasheet.pdf \
            https://www.ti.com/lit/ds/symlink/ina219.pdf
    # Waveshare Pico-UPS-A schematic
    test -f docs/datasheets/pico-ups-a-schematic.pdf || \
        curl -fL -o docs/datasheets/pico-ups-a-schematic.pdf \
            https://files.waveshare.com/upload/4/45/Pico-UPS-A_Schematic.pdf
    # SB Components Pico Air Monitoring Expansion sensors
    test -f docs/datasheets/pmsa003-datasheet.pdf || \
        curl -fL -o docs/datasheets/pmsa003-datasheet.pdf \
            https://www.aqmd.gov/docs/default-source/aq-spec/resources-page/plantower-pms5003-manual_v2-3.pdf
    test -f docs/datasheets/ssd1306-datasheet.pdf || \
        curl -fL -o docs/datasheets/ssd1306-datasheet.pdf \
            https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf
    @echo "Datasheets up to date in docs/datasheets/"

# Download all the standards I want to follow
fetch-standards:
    mkdir -p docs/standards
    test -f docs/standards/WMO-8-vI-2024_en.pdf || \
        curl -fL -o docs/standards/WMO-8-vI-2024_en.pdf \
            https://library.wmo.int/viewer/68695/download?file=WMO-8-vI-2024_en.pdf

# Download all docs (pinouts + datasheets)
fetch-docs: fetch-pinouts fetch-datasheets fetch-standards
    @echo "All docs fetched."

# List what has been downloaded
list-docs:
    @find docs -type f \( -name "*.pdf" -o -name "*.png" \) | sort
