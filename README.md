# HP Legacy Print

A reliability-focused Linux printing project for older host-based HP LaserJet printers.

## First targets

- HP LaserJet Pro P1102 — USB VID:PID `03f0:002a`, ZJS/Z2 family
- HP LaserJet P1006 — USB VID:PID `03f0:3e17`, XQX family, firmware required after power-up

The project is being built for modern Debian/Pardus systems with the goal of making old printers behave predictably again: reconnect after USB/power events, avoid stale queues and transport hangs, and provide one stable path for LibreOffice Writer/Calc, PDF viewers, browsers and other applications through CUPS/IPP.

## Design principles

1. Do not special-case applications. Applications print through CUPS/IPP.
2. Keep transport, rendering and printer-language encoding separate.
3. Never silently report success before transport completion is known.
4. Recover automatically from unplug/replug and printer power-cycle events.
5. Apply model-specific quirks only from an explicit model table.
6. Keep proprietary firmware outside the source tree.
7. Prefer current driverless/Printer Application architecture over adding another legacy PPD-only driver.

## Current milestone

**M0 — USB discovery and connection state monitor**

The first executable only discovers supported printers and reports connection state. It deliberately does not claim the USB interface or send print data yet, so it cannot interfere with CUPS during early testing.

## Build on Pardus/Debian

```bash
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev
git clone https://github.com/afcihangir/hp-legacy-print.git
cd hp-legacy-print
cmake -S . -B build
cmake --build build
./build/hp-legacy-print --list
```

For continuous connection monitoring:

```bash
./build/hp-legacy-print --watch
```

## Status

Early development. Do not replace a production print setup with this project yet.
