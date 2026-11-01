# Flipper Games

Pong and Space Invaders for the Flipper Zero.

## Games
- **pong**: Up/Down move, OK pause/restart, Back exit. First to 7 wins.
- **space_invaders**: Left/Right move, OK fire/restart, Back exit. 3 lives.

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

If the games only respond to buttons with the module attached, the sensor wasn't detected. The SPI settings in `imu.c` may need adjusting for your module.

## Build
Copy a game folder into `applications_user/` of the firmware and run `./fbt fap_pong` / `./fbt fap_space_invaders`, or run `ufbt` inside the game folder.

## Install
Copy the built `.fap` files (`pong/dist/pong.fap`, `space_invaders/dist/space_invaders.fap` when using `ufbt`) to `SD:/apps/Games/` on the Flipper.
