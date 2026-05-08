{ pkgs }:

pkgs.mkShell {
  name = "pico-micropython";

  packages = with pkgs; [
    # Primary tool for interacting with MicroPython devices over serial
    mpremote

    # Flash MicroPython firmware images to ESP32 / ESP8266 (or erase flash)
    esptool

    # Alternative tools
    rshell
    mpy-utils

    # Serial terminals
    tio       # auto-reconnects when Pico re-enumerates — preferred for dev
    picocom   # lightweight alternative

    # USB / device discovery
    usbutils  # lsusb

    # Python env for scripting / host-side tooling
    python3
    python3Packages.pyserial

    git
  ];

  shellHook = ''
    echo "Pico MicroPython dev shell"
    echo "  mpremote $(mpremote version 2>/dev/null || echo 'available')"
    echo "  Connect device and use: mpremote connect auto"
  '';
}
