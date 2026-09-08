---
name: velapoka-hardware-validation
description: Validate the VelaPoka ESP32-P4 contest terminal on real hardware, including startup, camera preview, touch inspection, MicroSD persistence, Ethernet export, and safe shutdown. Use when preparing a demo, checking a release candidate, or diagnosing a failed acceptance step; do not mark hardware checks as passed from build output alone.
---

# VelaPoka Hardware Validation

Validate one observable layer at a time and preserve the serial output as
evidence. Use the `configs/velapoka` product image for acceptance and
`configs/nsh` only as a recovery baseline.

## Preconditions

- Confirm the image was built from the intended commit and product config.
- Connect the SC2336 camera, 7-inch display/touch panel, FAT32 MicroSD and
  RJ45 cable before power-on.
- Keep UART at 115200 8N1. Do not use a successful build as evidence that a
  device works on the board.

## Validation workflow

1. Power-cycle the board and capture the complete boot log. Require 32 MiB
   PSRAM and the `/dev/fb0`, `/dev/input0`, `/dev/video0`, `/dev/mmcsd0` and
   `eth0` initialization messages that apply to the test.
2. Confirm VelaPoka starts automatically, reaches `READY` when a stored
   reference is valid, and maintains a live camera preview.
3. Use the touchscreen to enroll a standard sample. Run one unchanged
   inspection that should PASS, then introduce a visible difference and
   require FAIL plus an on-screen anomaly box.
4. Wait for queued storage work, press `Exit`, and require both
   `VelaPoka storage: unmounted /mnt/sdcard` and
   `velapoka: safe shutdown complete` before removing power or the card.
5. Power-cycle once more. Require template and threshold restoration without
   enrolling again, then confirm the next record ID continues from history.
6. On a PC configured as `10.0.0.1/24`, first wait for stable Ping to
   `10.0.0.2`. Verify HTTP 200 from `/api/status` and `/api/results`, and
   download one FAIL image using the path stored in the JSONL record.

## Failure routing

- `NO REFERENCE` with storage offline is a MicroSD/mount failure, not an
  inspection algorithm failure. Check CMD0 response, card power and mount
  logs before re-enrolling.
- After cable insertion or link recovery, allow PHY negotiation and retry an
  initial HTTP timeout. Ping alone does not prove that port 8080 is running.
- Clicking `Exit` intentionally stops HTTP before the camera and storage.
  Reset or power-cycle the board before a new demonstration.
- Never reformat the card or overwrite a template until its files and boot
  errors have been inspected.

## Output

Report each item as `PASS`, `FAIL`, or `NOT TESTED`. Include the firmware
commit, observed device nodes, PASS/FAIL scores, restored threshold and record
ID, exported file names, and the safe-shutdown log. Clearly separate measured
results from expected behavior and list every feature outside the tested
delivery scope.
