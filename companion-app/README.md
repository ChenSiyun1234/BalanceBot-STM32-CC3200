# BalanceBot Companion App — live web dashboard, REST API & cloud deploy

A small full-stack application that turns the self-balancing robot into something
you can **watch and tune from a browser**. It streams the robot's live telemetry
to a web dashboard and lets you push new PID gains back to it — over the same
**AWS IoT** path the CC3200 firmware already uses (see the repo
[root README](../README.md) → *IoT / networking*).

The whole thing runs with **zero hardware and zero cloud credentials**, thanks to
a built-in simulator, so it can be developed, demoed, and tested offline.

> Sister project to the embedded firmware in [`STM32/`](../STM32) and
> [`CC3200/`](../CC3200) — this is the software / web / cloud layer on top of it.

---

## Live demo (served to an Android phone over Wi-Fi)

The dashboard is responsive and works from any browser on the network. Below it
is served by `python -m api.server` on a laptop and opened on an Android phone at
`http://<laptop-ip>:8000`:

| Balancing — Angle Kp = 14 | Unstable — Angle Kp = 0 |
|:---:|:---:|
| ![Dashboard streaming, cart upright](docs/screenshots/01-dashboard.png) | ![Dashboard after dropping the gain, cart fallen](docs/screenshots/02-fallen.png) |

Dropping the **Angle Kp** gain to 0 from the phone is sent to the API over REST;
the simulator immediately destabilises — the sawtooth trace is the cart repeatedly
toppling and re-righting — and the **State** card flips to *fallen*. That is the
full **phone → REST → simulator → Server-Sent-Events → phone** round trip.

---

## Architecture

```
   ┌─────────────┐   MQTT/TLS    ┌──────────────────┐   SSE + REST   ┌──────────────┐
   │  BalanceBot  │ ───────────▶ │  Python API       │ ─────────────▶ │  Web         │
   │  (CC3200,    │   AWS IoT     │  (stdlib only)    │  /api/stream   │  dashboard   │
   │   Wi-Fi)     │ ◀─────────── │  server.py        │  /api/pid      │  (web/ or    │
   └─────────────┘   PID cmds     └──────────────────┘                │   web-react/)│
                                          ▲                            └──────────────┘
                                          │ default: no hardware
                                   ┌──────────────┐
                                   │  SimSource    │  inverted-pendulum simulator
                                   └──────────────┘
```

- **`api/`** — a dependency-free Python HTTP service (standard library only).
  REST endpoints for the PID gains, **Server-Sent Events** for live telemetry,
  and it serves the static dashboard. Data comes from either the **simulator**
  (`telemetry.SimSource`) or **AWS IoT Core** (`iot.AwsIotSource`).
- **`web/`** — a **buildless** dashboard (HTML + CSS + vanilla JS, Chart.js via
  CDN). Runs straight from the Python server, no toolchain.
- **`web-react/`** — the same dashboard as a **React + TypeScript** app (Vite).
- **`deploy/`** — Infrastructure-as-Code for a serverless deployment
  (**AWS SAM** `template.yaml` and **Serverless Framework** `serverless.yml`),
  backed by a Lambda + DynamoDB + an IoT TopicRule.

## Run it (no hardware, no install)

```bash
cd companion-app
python -m api.server            # → http://127.0.0.1:8000
```

Open <http://127.0.0.1:8000> and you'll see live (simulated) telemetry. Lower the
**Angle Kp** gain and apply — the cart starts to wobble and "fall"; raise it and
it settles. That's the same cascaded-PID intuition as the firmware, made visible.

### Live mode (real robot, via AWS IoT)

```bash
pip install paho-mqtt
python -m api.server --source aws \
  --endpoint <id>-ats.iot.<region>.amazonaws.com \
  --ca AmazonRootCA1.pem --cert device.pem.crt --key private.pem.key
```

### React / TypeScript dashboard

Requires Node.js. In one terminal run the Python API (`python -m api.server`);
in another:

```bash
cd companion-app/web-react
npm install
npm run dev                     # Vite dev server; /api is proxied to :8000
```

## Test

```bash
cd companion-app
python -m unittest api.tests.test_api -v     # 8 integration tests, stdlib only
```

## API

| Method | Path             | Description                                   |
|--------|------------------|-----------------------------------------------|
| GET    | `/api/health`    | `{"status":"ok","source":"sim"\|"aws-iot"}`   |
| GET    | `/api/pid`       | current PID gains                             |
| POST   | `/api/pid`       | apply a (partial) gain update; `400` on bad input |
| GET    | `/api/telemetry` | latest telemetry sample                       |
| GET    | `/api/history`   | recent telemetry samples                      |
| GET    | `/api/stream`    | `text/event-stream` of live telemetry (SSE)   |

## What it demonstrates

- **Web development** — responsive dashboard in both vanilla JS/HTML/CSS and React/TypeScript.
- **API + server-side** — a hand-rolled REST + SSE service in Python, with input validation.
- **Cloud** — AWS IoT Core (MQTT over TLS) integration and an IaC deploy (SAM / Serverless / Lambda / DynamoDB).
- **Full SDLC** — design, implementation, **unit tests**, and documentation.

---

## 中文简介

本目录是自平衡小车的**配套全栈应用**：把小车的实时遥测推送到网页仪表盘，并能在网页上
在线调 PID 参数，走的是 CC3200 固件已有的 **AWS IoT** 链路。内置仿真器，**无需硬件、
无需云凭证**即可运行、演示与测试。

- `api/` 纯标准库 Python 服务（REST + SSE，零依赖）
- `web/` 免构建网页仪表盘（HTML/CSS/JS）；`web-react/` 同款 React + TypeScript 版本
- `deploy/` AWS SAM / Serverless Framework 的无服务器部署配置

运行：`cd companion-app && python -m api.server`，浏览器打开 <http://127.0.0.1:8000>。
测试：`python -m unittest api.tests.test_api -v`。
