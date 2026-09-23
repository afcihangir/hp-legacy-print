# Architecture

## Goal

Provide a stable modern Linux printing path for older host-based HP LaserJet printers without tying reliability to one desktop application or one legacy PPD queue.

## Planned layers

1. **Application/CUPS side**
   - LibreOffice Writer/Calc, PDF viewers, browsers and other applications print normally.
   - Long-term target: expose the printer through a modern IPP/Printer Application interface.

2. **Input/rendering**
   - Accept standard print input from CUPS/IPP.
   - Render to a deterministic monochrome raster format.
   - Rendering failures are explicit and never reported as successful jobs.

3. **Model encoder**
   - P1102 family: ZJS/Z2.
   - P1005/P1006/P1007/P1008/P1505 family: XQX.
   - Existing protocol knowledge may be reused where licensing permits; the transport state machine remains independent.

4. **Reliable USB transport**
   - Identify devices by VID/PID and serial where available.
   - Explicit connection states.
   - Bounded timeouts.
   - Reconnect after unplug/replug or power cycle.
   - Never allow concurrent writers to one physical printer.
   - Do not mark a job successful until the transport stage has completed.
   - Model quirks live in data, not scattered conditionals.

5. **Firmware manager**
   - Only for models that require downloadable firmware after power-up.
   - Firmware blobs are not committed to this repository.
   - Validate firmware presence before attempting a print job.

6. **Power policy**
   - Prevent host USB autosuspend for supported devices when the service is enabled.
   - Printer-internal sleep/auto-off controls will only be changed where a model-specific command is verified.
   - Do not send guessed vendor commands.

## Development order

- M0: read-only USB discovery and state monitoring.
- M1: robust USB claim/release/reconnect state machine.
- M2: P1102 raw transport test.
- M3: P1102 ZJS/Z2 rendering/encoding path.
- M4: CUPS/IPP integration.
- M5: P1006 firmware manager and XQX path.
- M6: broader model table and long-running reliability tests.

## Safety rule for early milestones

Until M1 is intentionally enabled, the project must not detach kernel drivers, claim printer interfaces, change power management, or send bytes to a printer. This keeps development from disrupting an existing CUPS setup.


## Verified P1102 USB profile

Observed on HP LaserJet Pro P1102 (03f0:002a):

- Printer interface: 0
- USB class/subclass/protocol: 7/1/2
- Bulk OUT endpoint: 0x01
- Bulk IN endpoint: 0x81
- Kernel usblp driver: active on interface 0
- Vendor-specific interface 1 is present but is not used by the transport until its purpose is verified.
- The model carries a no-reattach transport quirk. The future daemon must keep ownership and must not blindly reattach usblp after each job.

The transport profile is stored in the model table so endpoint choices are data-driven rather than hard-coded throughout the code.
