{ pkgs }:

pkgs.mkShell {
  name = "pico-c";

  packages = with pkgs; [
    # Cross-compilation toolchain for ARM Cortex-M
    gcc-arm-embedded

    # Pico SDK — sets PICO_SDK_PATH via setup hook
    pico-sdk

    # Build system
    cmake
    ninja
    gnumake

    # Device interaction, flashing & debugging
    picotool  # UF2/BOOTSEL flashing and device info
    openocd   # SWD debugging — RPi Debug Probe uses CMSIS-DAP, supported natively
              # Connect: openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg

    # PIO assembler (often included via pico-sdk, but explicit here)
    pioasm

    # Serial communication
    # The RPi Debug Probe exposes a USB-UART bridge alongside the CMSIS-DAP interface.
    # It typically appears as /dev/ttyACM0 or /dev/ttyACM1.
    tio       # preferred — auto-reconnects when the Pico re-enumerates
    picocom   # lightweight alternative

    # USB / device discovery
    usbutils  # lsusb — identify which /dev/ttyACM* is the UART bridge

    git
  ];

  # pico-sdk's setup hook sets PICO_SDK_PATH automatically.
  # PICO_TOOLCHAIN_PATH tells cmake where the ARM gcc lives.
  shellHook = ''
    export PICO_TOOLCHAIN_PATH="${pkgs.gcc-arm-embedded}"
    echo "Pico C/C++ dev shell"
    echo "  SDK:       $PICO_SDK_PATH"
    echo "  Toolchain: $PICO_TOOLCHAIN_PATH"
    echo ""
    echo "  Serial:    tio /dev/ttyACM0   (debug probe UART bridge)"
    echo "  Debug:     openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg"
  '';
}
