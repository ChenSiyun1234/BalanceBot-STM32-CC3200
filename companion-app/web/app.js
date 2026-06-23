/*
 * BalanceBot dashboard — buildless (vanilla JS, no toolchain).
 * Talks to the Python API on the same origin: SSE for live telemetry,
 * REST for reading and applying PID gains.
 */
"use strict";

const MAX_POINTS = 160;
const $ = (id) => document.getElementById(id);

const chart = (typeof Chart !== "undefined") ? new Chart($("chart"), {
  type: "line",
  data: {
    labels: [],
    datasets: [{
      label: "Pitch (deg)",
      data: [],
      borderColor: "#3b82f6",
      backgroundColor: "rgba(59,130,246,0.12)",
      borderWidth: 2,
      pointRadius: 0,
      tension: 0.25,
      fill: true,
    }],
  },
  options: {
    animation: false,
    responsive: true,
    maintainAspectRatio: false,
    scales: {
      y: { suggestedMin: -30, suggestedMax: 30, title: { display: true, text: "deg" } },
      x: { display: false },
    },
    plugins: { legend: { display: false } },
  },
}) : null;

function setConnection(ok) {
  const el = $("conn");
  el.textContent = ok ? "streaming" : "disconnected";
  el.className = "pill " + (ok ? "ok" : "off");
}

function renderTelemetry(t) {
  $("pitch").textContent = `${t.pitch_deg.toFixed(1)}°`;
  $("pid").textContent = t.pid_output.toFixed(1);
  $("heading").textContent = `${Math.round(t.heading_deg)}°`;
  $("source").textContent = t.source;
  $("state").textContent = t.upright ? "upright" : "fallen";
  $("state-card").classList.toggle("bad", !t.upright);
  $("state-card").classList.toggle("ok", t.upright);

  if (chart) {
    const ds = chart.data.datasets[0].data;
    ds.push(t.pitch_deg);
    chart.data.labels.push("");
    if (ds.length > MAX_POINTS) {
      ds.shift();
      chart.data.labels.shift();
    }
    chart.update("none");
  }
}

async function loadGains() {
  const res = await fetch("/api/pid");
  const gains = await res.json();
  for (const [key, value] of Object.entries(gains)) {
    const input = document.querySelector(`input[name="${key}"]`);
    if (input) input.value = value;
  }
}

async function applyGains(event) {
  event.preventDefault();
  $("form-error").textContent = "";
  const payload = {};
  for (const input of document.querySelectorAll(".pid-form input")) {
    payload[input.name] = Number(input.value);
  }
  const res = await fetch("/api/pid", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ error: res.statusText }));
    $("form-error").textContent = err.error || "request failed";
    return;
  }
  await loadGains();
}

function connectStream() {
  const es = new EventSource("/api/stream");
  es.onopen = () => setConnection(true);
  es.onerror = () => setConnection(false); // EventSource auto-reconnects
  es.onmessage = (ev) => {
    try {
      renderTelemetry(JSON.parse(ev.data));
      setConnection(true);
    } catch (_) { /* ignore malformed frame */ }
  };
}

$("pid-form").addEventListener("submit", applyGains);
$("reload").addEventListener("click", loadGains);
loadGains();
connectStream();
