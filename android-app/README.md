# BalanceBot Remote — Android App

An Android phone app that drives my [self-balancing two-wheel robot](https://github.com/ChenSiyun1234/BalanceBot-STM32-CC3200)
over its **HC-05 Bluetooth** module using the classic-Bluetooth **Serial Port Profile (SPP)**.
It's the mobile companion to the robot's bare-metal STM32 firmware: the firmware balances the
robot and listens on the HC-05 UART; this app is the remote.

Built in **Kotlin** to round out the project with the full **hardware → firmware → mobile-software** stack.

## Demo
![BalanceBot Remote — app interface](docs/screenshot.png)

*The phone app driving the self-balancing robot over Bluetooth — hold a direction to move, release to stop.*

> **To capture this:** pair the HC-05 in phone settings, open the app, and use Android's built-in
> **screen recorder** while you drive the robot. Trim to ~5–10 s, export a small `docs/demo.gif`
> (e.g. via [ezgif.com](https://ezgif.com) or `ffmpeg`), and drop two screenshots in `docs/`.
> A short driving clip is the single highest-impact thing you can add here — it proves it actually works.

## Features
- **Touch D-pad** with press-and-hold control — hold a direction to drive, release to stop (like a real RC remote).
- **Speed slider (0–9)** sent alongside each command.
- **Classic-Bluetooth SPP** connection to a paired HC-05 (`createRfcommSocketToServiceRecord`).
- **Runtime-permission handling** for Android 12+ (`BLUETOOTH_CONNECT`) and older APIs.
- Blocking socket connect / writes done **off the UI thread**; safety **STOP** sent on disconnect.

## Command protocol
One ASCII char per action, plus a speed digit (`S` = stop):
`F5` forward · `B5` back · `L5` left · `R5` right · `S` stop — matching the firmware's HC-05 command set.

## How it works
1. The HC-05 is paired to the phone in Android Bluetooth settings.
2. The app finds the bonded device named `HC-05`, opens an RFCOMM socket on the SPP UUID
   `00001101-0000-1000-8000-00805F9B34FB`, and connects on a background thread.
3. Touch events on the D-pad write command bytes to the socket's `OutputStream`.

## Build the APK

This is a **complete Gradle project** (AGP 8.5, Kotlin 1.9, `compileSdk 34`) with a committed
Gradle wrapper — no manual project setup needed.

**Command line** — needs JDK 17 and the Android SDK (`platform 34` + `build-tools 34`):
```bash
cd android-app
./gradlew assembleDebug          # Windows: .\gradlew.bat assembleDebug
# output: app/build/outputs/apk/debug/app-debug.apk
```

**Android Studio** — *File → Open* the `android-app/` folder, let it sync, then **Run** ▶.

If your Bluetooth module isn't named `HC-05`, change `hc05Name` in `MainActivity.kt`.

### Install on a phone
```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```
Or copy `app-debug.apk` to the phone and tap it (enable "install unknown apps").

## Use it
Pair the HC-05 in phone settings → open the app → **Connect to HC-05** → hold the arrows to drive.

---
*Self-contained Gradle project: `MainActivity.kt`, `activity_main.xml`, `AndroidManifest.xml`,
plus the Gradle build scripts and wrapper. `local.properties` (your machine's SDK path) and
`build/` are intentionally not committed.*
