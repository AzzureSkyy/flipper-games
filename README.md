# Flipper Games

Pong, Space Invaders and DigiPet for the Flipper Zero.

## Games
- **pong**: Up/Down move, OK pause/restart, Back exit. First to 7 wins.
- **space_invaders**: Left/Right move, OK fire/restart, Back exit. 3 lives.
- **digipet**: original digital-monster virtual pet. Left/Right choose Feed/Train/Battle/Clean/Medic/Status, OK selects, Back exits. Hatches from an egg into Blipling, then Clawpup, then Pyrodrake (good care) or Sludgeon (neglect). Saved to the SD card and keeps aging while closed (up to 24 h).
- **imu_test** (Tools): diagnostic for the Video Game Module motion sensor. See [Troubleshooting tilt](#troubleshooting-tilt).

## Video Game Module (TV + tilt controls)
Built for the official Flipper Zero firmware.

- **TV output:** with the Video Game Module attached, the official firmware mirrors the Flipper screen to a TV over HDMI (128x64 resolution). No code changes needed.
- **Tilt controls:** the games read the module's ICM-42688-P motion sensor.
  - **Pong:** tilt forward/back to move your paddle.
  - **Space Invaders:** tilt left/right to move the ship; OK still fires.
  - **Recalibrate:** hold OK to set your current position as "level". This also happens once at startup.
  - **Without the module:** the games fall back to buttons. Buttons also work alongside tilt.

### Tuning tilt
If a game moves the wrong way or responds to the wrong kind of tilt, edit the settings at the top of `pong.c` or `space_invaders.c`:

| Setting | Purpose |
|---|---|
| `TILT_SIGN` | Set to `-1.0f` to reverse direction |
| `TILT_AXIS` | Sensor axis to use: `0` = X, `1` = Y, `2` = Z |
| `TILT_SPEED` | Sensitivity (higher = faster) |
| `TILT_DEADZONE` | How much tilt is ignored around level |

### Troubleshooting tilt
The games turn on the GPIO 5V supply for the module, set up the GPIO SPI pins and retry sensor detection 5 times. If a game still only responds to buttons, run **IMU Test** (`Apps → Tools → IMU Test`):

| Screen shows | Meaning |
|---|---|
| `Sensor: FOUND` and changing X/Y/Z | Sensor works. Tune the tilt settings above. |
| `WHO_AM_I: 0x00` or `0xFF` | No reply over the GPIO pins. The module may route the sensor to its own RP2040 controller instead of the Flipper. |
| Another `WHO_AM_I` value | A different sensor chip; `imu.c` needs a driver for it. |
| `5V OTG: OFF` | The Flipper couldn't power the module. Check the connection and battery level. |

Press OK to retry detection and Back to exit.

## Build
Copy an app folder into `applications_user/` of the firmware and run `./fbt fap_pong` / `./fbt fap_space_invaders` / `./fbt fap_imu_test`, or run `ufbt` inside the app folder.

## Install
Copy the built `.fap` files (`pong/dist/pong.fap`, `space_invaders/dist/space_invaders.fap` when using `ufbt`) to `SD:/apps/Games/` on the Flipper, and `imu_test/dist/imu_test.fap` to `SD:/apps/Tools/`.
