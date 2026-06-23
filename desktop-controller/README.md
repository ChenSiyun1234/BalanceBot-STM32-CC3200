# BalanceBot Remote Controller

A Python (Tkinter) desktop remote that drives a self-balancing two-wheel robot over its HC-05 Bluetooth link by streaming single-character drive commands to the robot's STM32 firmware.

## Overview

This sub-project is the software companion to a self-balancing two-wheel robot. The robot itself is split across three layers:

- Hardware: the chassis, motors, IMU, motor driver, and an HC-05 Bluetooth module.
- Firmware: bare-metal STM32 code that runs the balance loop (keeps the robot upright) and listens for drive commands on the HC-05 UART link.
- Software (this project): a desktop "remote" that lets a human steer the robot while the firmware handles balancing.

The firmware keeps the robot upright on its own. It does not need this app to stay balanced — this app only sends the higher-level intent (drive forward, turn, stop, change speed). The division of responsibility is deliberate: balancing is a hard real-time control problem that belongs on the microcontroller, while steering is a soft, human-paced task that is comfortable to do from a laptop GUI.

The connection path is entirely standard Windows Bluetooth serial. Once the HC-05 module is paired with the PC, Windows assigns it an outgoing serial COM port. Opening that COM port is what establishes the Bluetooth connection — there is no separate "connect" handshake in the protocol. This controller opens the port, then writes ASCII command characters that the firmware parses one byte at a time.

When `pyserial` is not installed, or no port is given, or the named port cannot be opened, the app falls back to a no-hardware simulation mode so the GUI and command logic remain fully demoable and testable offline.

## Demo / Screenshot

![BalanceBot desktop controller](docs/screenshot.png)

Desktop controller showing the connection banner, status line, on-screen D-pad, and the speed controls. Drive with WASD or the arrow keys; press Space to stop.

## Features

- Tkinter desktop GUI with an on-screen directional pad (forward / back / left / right / stop) and speed up / down buttons.
- Full keyboard control: `WASD` and arrow keys for direction, `Space` to stop, `+` / `-` (and `=`) to change speed.
- Single-character command protocol decoupled from the UI, implemented in the reusable `BalanceBotLink` class.
- Adjustable speed `0`-`9`, sent alongside each direction command (the current speed defaults to `5`).
- No-hardware simulation fallback: runs and logs commands with no `pyserial` and no serial port, so the logic is testable and the app demos offline.
- Connection state surfaced in the UI: a green "CONNECTED" banner with the port name, or an amber "SIMULATION (no serial port)" banner.
- Safety stop on disconnect: the link sends `S` (stop) before closing the serial port, so the robot does not keep driving if the window is closed.
- Headless self-test (`--selftest`) that exercises the command protocol with no GUI and no hardware.
- Minimal dependencies: standard-library `tkinter` and `argparse` only; `pyserial` is an optional add-on for the real Bluetooth link.

## Architecture

The app is a thin, two-layer design. The GUI layer (`run_gui`) captures user intent from buttons and key bindings and translates it into protocol calls. The link layer (`BalanceBotLink`) owns the serial port and the wire format. Either a real `pyserial` port or an in-memory log sits behind the same `send()` method, so the GUI does not know or care whether hardware is present.

```
   +---------------------------------------------------------+
   |  User                                                   |
   |  keyboard (WASD / arrows / Space / + -) or D-pad clicks |
   +----------------------------+----------------------------+
                                |
                                v
   +---------------------------------------------------------+
   |  Tkinter GUI  (run_gui)                                 |
   |  - keymap: w/Up->forward, s/Down->backward,             |
   |            a/Left->left,  d/Right->right, space->stop    |
   |  - state["speed"] in 0..9 (default 5)                   |
   |  - act(name) / change_speed(delta) -> status line       |
   +----------------------------+----------------------------+
                                | COMMANDS[name] + speed
                                v
   +---------------------------------------------------------+
   |  BalanceBotLink.send(cmd, speed)                        |
   |  - validates cmd in {F,B,L,R,S} and speed in 0..9       |
   |  - builds ASCII payload  e.g. "F5"                      |
   +-------------------+-----------------+-------------------+
                       |                 |
            real port  |                 |  no port / no pyserial
                       v                 v
        +---------------------+   +-------------------------+
        | serial.Serial.write |   | self.log.append(...)    |
        | (pyserial)          |   | (simulation mode)       |
        +----------+----------+   +-------------------------+
                   |
                   v
        +---------------------------+
        | HC-05 (outgoing COM port) |
        |   Bluetooth SPP           |
        +-------------+-------------+
                      |
                      v
        +---------------------------+
        | STM32 firmware (UART)     |
        | parses F/B/L/R/S + digit  |
        | balance loop stays local  |
        +---------------------------+
```

Data flow for one keypress:

1. The user presses a key (for example `w`) or clicks a D-pad button.
2. The bound handler calls `act("forward")`.
3. `act` looks up `COMMANDS["forward"]` (the char `F`) and calls `link.send("F", state["speed"])`.
4. `BalanceBotLink.send` validates inputs, builds the ASCII payload (for example `F5`), and either writes it to the open serial port or appends it to the in-memory `log` in simulation mode.
5. The GUI status line updates to reflect what was sent.

## Project layout

```
desktop-controller/
|-- balancebot_controller.py   # entire app: protocol, link class, GUI, self-test, CLI
|-- README.md                  # this document
`-- docs/
    `-- screenshot.png         # desktop controller UI (used in Demo section)
```

Inside `balancebot_controller.py`:

| Symbol | Kind | Responsibility |
|---|---|---|
| `COMMANDS` | dict | Maps action names (`forward`, `backward`, `left`, `right`, `stop`) to the protocol chars (`F`, `B`, `L`, `R`, `S`). |
| `BalanceBotLink` | class | Owns the serial port; provides `send()` and `close()`; falls back to simulation when no port / no `pyserial`. |
| `BalanceBotLink.send(cmd, speed=None)` | method | Validates and encodes a command, writes to the port or to the in-memory log; returns the bytes sent. |
| `BalanceBotLink.close()` | method | Sends a safety `S` then closes the port. |
| `run_gui(port=None)` | function | Builds the Tkinter window, D-pad, speed controls, key bindings, and status line. |
| `selftest()` | function | Headless check of the command protocol (no GUI, no hardware). |
| `__main__` block | CLI | Parses `--port` and `--selftest`; dispatches to `selftest()` or `run_gui()`. |

## Command-protocol reference

The protocol is one ASCII character per action, optionally followed by a single speed digit `0`-`9`. Commands are written to the serial port as raw ASCII bytes with no terminator, newline, or framing. This matches the command set the robot's firmware expects on its HC-05 UART link.

| Action | Command char | Bytes on the wire | Example with speed 5 |
|---|---|---|---|
| Forward | `F` | `0x46` | `F5` (`0x46 0x35`) |
| Backward | `B` | `0x42` | `B5` (`0x42 0x35`) |
| Left | `L` | `0x4C` | `L5` (`0x4C 0x35`) |
| Right | `R` | `0x52` | `R5` (`0x52 0x35`) |
| Stop | `S` | `0x53` | `S` (sent without a speed digit) |

Encoding rules, as enforced by `BalanceBotLink.send`:

- `cmd` must be one of the five values in `COMMANDS` (`F`, `B`, `L`, `R`, `S`); any other character raises `ValueError("unknown command: ...")`.
- `speed`, when provided, must satisfy `0 <= speed <= 9`; otherwise it raises `ValueError("speed must be 0-9")`.
- If `speed` is `None`, only the single command char is sent (for example a bare `S` for stop).
- If `speed` is given, the payload is the command char immediately followed by the digit (for example `F5`), encoded as ASCII.
- `send()` returns the exact `bytes` object it sent, which is what the self-test asserts against (for example `link.send("F", 5)` returns `b"F5"` and `link.send("F")` returns `b"F"`).

The protocol is request-only. There is no response message defined in this app; the firmware acts on each character as it arrives. In simulation mode the "response" is observational only: the payload string is appended to `BalanceBotLink.log` so tests and demos can inspect what would have been transmitted.

In the GUI, the current speed (`state["speed"]`, default `5`, clamped to `0`-`9`) is sent with every direction press, so a forward press at speed 7 sends `F7`. The stop button and the `Space` key send the stop command through the same `act("stop")` path, which includes the current speed digit (for example `S5`); the firmware treats `S` as stop regardless of any trailing digit.

## Build and Run

This is a single-file Python app. There is no build step.

Prerequisites:

- Python 3 with the standard library (`tkinter` ships with the standard CPython installer on Windows; `argparse` is built in).
- Optional: `pyserial`, required only for talking to real hardware.

Run the GUI in simulation mode (no hardware, no port):

```bash
python balancebot_controller.py
```

Connect to the paired HC-05 and drive the real robot:

```bash
python balancebot_controller.py --port COM5
```

Run the headless self-test (no GUI, no hardware):

```bash
python balancebot_controller.py --selftest
```

Install the optional serial dependency when you are ready to use real hardware:

```bash
pip install pyserial
```

## Configuration / options

Command-line flags (parsed in the `__main__` block via `argparse`):

| Flag | Argument | Default | Effect |
|---|---|---|---|
| `--port` | COM port name, for example `COM5` | none | Serial COM port of the paired HC-05. If omitted, the app starts in simulation mode. |
| `--selftest` | (boolean flag) | off | Runs `selftest()` headlessly and exits; no GUI is shown. |

In-code constants and defaults:

- Baud rate defaults to `9600` (the `baud` argument of `BalanceBotLink.__init__`). It is not exposed as a CLI flag; change it in code if your firmware uses a different rate.
- Serial read timeout is `1` second (passed to `serial.Serial(..., timeout=1)`).
- The initial GUI speed is `5` (`state = {"speed": 5}`), clamped to the inclusive range `0`-`9` by `change_speed`.
- The window is fixed at `340x340` and non-resizable.

Connecting to hardware (Windows / HC-05):

1. Power on the robot so the HC-05 is advertising.
2. Pair the HC-05 in Windows Bluetooth settings (default PIN is commonly `1234` or `0000`, depending on your module).
3. Open "More Bluetooth options" / "COM Ports" and note the Outgoing port assigned to the HC-05 (for example `COM5`).
4. Launch with that port: `python balancebot_controller.py --port COM5`. Opening the outgoing COM port is what brings up the Bluetooth connection.

## Testing

The built-in self-test exercises the protocol without a GUI or hardware:

```bash
python balancebot_controller.py --selftest
```

`selftest()` verifies, in order:

- A `BalanceBotLink(None)` starts in simulation mode (`link.sim is True`).
- Every command in `COMMANDS` encodes correctly with a speed digit, for example `link.send("F", 5) == b"F5"`.
- A bare command with no speed encodes to a single byte, `link.send("F") == b"F"`.
- An invalid command (`link.send("Z")`) raises `ValueError`.

On success it prints the ordered list of simulated commands and `SELFTEST OK - command protocol works`. Because `BalanceBotLink` and the command logic are decoupled from Tkinter, the same `send()` path that is unit-tested here is the one used in production against real hardware.

## Deployment / install notes

- No packaging or install is required: copy `balancebot_controller.py` and run it with a Python 3 interpreter.
- The only optional dependency is `pyserial` (`pip install pyserial`), and only for real-hardware use. Everything else is in the Python standard library.
- The GUI relies on `tkinter`, which is bundled with the standard Windows and macOS CPython installers. On some Linux distributions you may need to install it separately (for example `sudo apt install python3-tk`).
- This controller is the consumer side of the link. The robot must be running its STM32 firmware and have the HC-05 paired before the app can do anything useful against hardware.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Banner shows "SIMULATION (no serial port)" even though you passed `--port` | `pyserial` not installed, or the port could not be opened. The app prints `[sim] could not open <port> (...)` to the console with the underlying exception. | Read the printed exception. Run `pip install pyserial`, confirm the COM port name, and make sure nothing else has the port open. |
| `could not open COMx ... Access is denied` / `PermissionError` | The port is already open in another program (a serial monitor, IDE, or a previous instance of this app). | Close the other program or the stale window, then relaunch. |
| Wrong COM port number | Windows can assign the HC-05 both an incoming and an outgoing port. | Use the Outgoing port from the Bluetooth COM Ports dialog; that is the one that initiates the connection. |
| Robot does not move although the status line shows commands being sent | Not paired, wrong port, wrong baud, or robot powered off. | Re-pair the HC-05, verify the outgoing port, confirm the firmware baud matches `9600`, and check the robot is powered. |
| Robot moves but ignores speed | Firmware may not parse the trailing speed digit, or expects a different speed encoding. | Confirm the firmware reads the digit after the command char; align the firmware's expected format with `F`/`B`/`L`/`R` + digit. |
| `ModuleNotFoundError: No module named 'tkinter'` (Linux) | `tkinter` not installed for your Python. | Install the Tk bindings, for example `sudo apt install python3-tk`. |
| Robot keeps driving after you close the window | Window was force-killed before `close()` ran. | Use the window close button (which triggers `link.close()` and sends a safety `S`), or press `Space` / Stop first. |
| `ValueError: unknown command` or `speed must be 0-9` | Calling `BalanceBotLink.send` with a char outside `F/B/L/R/S` or a speed outside `0`-`9`. | Use only the defined command chars and keep speed within `0`-`9`. |

## Tech stack

- Language: Python 3
- GUI: Tkinter (Python standard library)
- Serial / Bluetooth I/O: pyserial (optional), over an HC-05 SPP COM port
- CLI: argparse (Python standard library)
- Target device: STM32 bare-metal firmware with an HC-05 UART link
- Platform: Windows (Bluetooth serial COM ports); the code is portable to any OS that exposes the HC-05 as a serial port

---

Companion to the embedded firmware project. Built to round out the robot with the full hardware to firmware to control-software stack.
