from flask import Flask, request, jsonify, render_template_string, Response
from datetime import datetime, timezone, timedelta
import os
import psycopg2

app = Flask(__name__)

# Latest data from ESP32 (in-memory only)
latest = {
    "biodigester_temp": None,
    "water_temp": None,
    "heater": None,
    "motor": None,
    "updated_at": None,
}

UTC = timezone.utc

# --------------- Database helpers ---------------

DATABASE_URL = os.environ.get("DATABASE_URL")


def get_conn():
    """Return a new psycopg2 connection (caller must close)."""
    return psycopg2.connect(DATABASE_URL)


def init_db():
    """Create the readings table if it doesn't exist."""
    if not DATABASE_URL:
        return
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                CREATE TABLE IF NOT EXISTS readings (
                    id      SERIAL PRIMARY KEY,
                    timestamp       TIMESTAMP NOT NULL,
                    biodigester_temp REAL,
                    heater  BOOLEAN
                );
            """)
        conn.commit()
    finally:
        conn.close()


init_db()


# --------------- Routes ---------------

# ESP32 posts data here
@app.route("/api/data", methods=["POST"])
def receive_data():
    data = request.get_json()
    if not data:
        return jsonify({"error": "no data"}), 400
    latest["biodigester_temp"] = data.get("biodigester_temp")
    latest["water_temp"] = data.get("water_temp")
    latest["heater"] = data.get("heater")
    latest["motor"] = data.get("motor")
    latest["updated_at"] = datetime.now(UTC).strftime("%Y-%m-%d %H:%M:%S")

    # Persist to PostgreSQL
    if DATABASE_URL:
        conn = get_conn()
        try:
            with conn.cursor() as cur:
                cur.execute(
                    """INSERT INTO readings (timestamp, biodigester_temp, heater)
                       VALUES (NOW() AT TIME ZONE 'America/Costa_Rica', %s, %s)""",
                    (data.get("biodigester_temp"), data.get("heater")),
                )
            conn.commit()
        finally:
            conn.close()

    return jsonify({"status": "ok"})


# JSON endpoint for API consumers
@app.route("/api/data", methods=["GET"])
def get_data():
    return jsonify(latest)


# History JSON (for graph & table)
@app.route("/api/history")
def history_json():
    if not DATABASE_URL:
        return jsonify([])
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT timestamp, biodigester_temp, heater FROM readings ORDER BY timestamp"
            )
            rows = cur.fetchall()
    finally:
        conn.close()
    result = []
    for ts, temp, heater in rows:
        result.append({
            "timestamp": ts.strftime("%Y-%m-%d %H:%M"),
            "biodigester_temp": temp,
            "heater": heater,
        })
    return jsonify(result)


# History CSV download
@app.route("/api/history/csv")
def history_csv():
    if not DATABASE_URL:
        return Response("timestamp,biodigester_temp,heater\n",
                        mimetype="text/csv",
                        headers={"Content-Disposition": "attachment; filename=readings.csv"})
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT timestamp, biodigester_temp, heater FROM readings ORDER BY timestamp"
            )
            rows = cur.fetchall()
    finally:
        conn.close()

    lines = ["timestamp,biodigester_temp,heater"]
    for ts, temp, heater in rows:
        lines.append(f"{ts.strftime('%Y-%m-%d %H:%M')},{temp},{heater}")
    csv_text = "\n".join(lines) + "\n"
    return Response(csv_text,
                    mimetype="text/csv",
                    headers={"Content-Disposition": "attachment; filename=readings.csv"})


# Dashboard
@app.route("/")
def dashboard():
    return render_template_string(PAGE, data=latest)


PAGE = """
<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Biodigester Monitor</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
    body {
        font-family: "Times New Roman", Times, serif;
        background: #c0c0c0;
        color: #000;
        margin: 0;
        padding: 20px;
    }
    .page {
        max-width: 580px;
        margin: 0 auto;
    }
    .titlebar {
        background: #000080;
        color: #fff;
        font-weight: bold;
        font-size: 14px;
        padding: 3px 6px;
        margin-bottom: 8px;
        display: flex;
        justify-content: space-between;
        align-items: center;
    }
    .titlebar-buttons span {
        display: inline-block;
        width: 16px;
        height: 14px;
        background: #c0c0c0;
        border: 2px outset #fff;
        text-align: center;
        font-size: 10px;
        line-height: 10px;
        margin-left: 2px;
        cursor: default;
    }
    .panel {
        border: 2px outset #fff;
        background: #c0c0c0;
        padding: 16px;
        margin-bottom: 8px;
    }
    .panel-inset {
        border: 2px inset #888;
        background: #fff;
        padding: 12px;
        margin: 8px 0;
    }
    h1 {
        font-size: 22px;
        text-align: center;
        margin: 0 0 4px 0;
    }
    h2 {
        font-size: 16px;
        margin: 0 0 8px 0;
    }
    hr {
        border: none;
        border-top: 2px groove #c0c0c0;
        margin: 10px 0;
    }
    .readings {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 8px;
        margin: 8px 0;
    }
    .reading-box {
        border: 2px inset #888;
        background: #fff;
        padding: 10px;
        text-align: center;
    }
    .reading-label {
        font-size: 12px;
        color: #666;
        margin-bottom: 4px;
    }
    .reading-value {
        font-family: "Courier New", Courier, monospace;
        font-size: 28px;
        font-weight: bold;
    }
    .reading-unit {
        font-size: 12px;
        color: #666;
    }
    .status-line {
        text-align: center;
        margin: 6px 0;
    }
    .status-badge {
        display: inline-block;
        font-size: 16px;
        font-weight: bold;
        padding: 3px 14px;
        border: 2px outset #fff;
        min-width: 60px;
    }
    .status-on {
        background: #00aa00;
        color: #fff;
        animation: blink90s 1.5s step-end infinite;
    }
    .status-off {
        background: #cc0000;
        color: #fff;
    }
    .status-unknown {
        background: #808080;
        color: #fff;
    }
    @keyframes blink90s {
        0%, 100% { opacity: 1; }
        50% { opacity: 0.6; }
    }
    .updated {
        font-size: 12px;
        color: #666;
        text-align: center;
        margin-top: 8px;
    }
    .connection {
        text-align: center;
        font-size: 12px;
        margin-top: 4px;
    }
    .live { color: #00aa00; font-weight: bold; }
    .error { color: #cc0000; font-weight: bold; }
    .footer {
        text-align: center;
        font-size: 11px;
        color: #666;
        margin-top: 6px;
    }
    .counter {
        text-align: center;
        font-size: 11px;
        color: #808080;
        margin-top: 10px;
    }
    /* History section */
    .chart-container {
        border: 2px inset #888;
        background: #fff;
        padding: 8px;
        margin: 8px 0;
    }
    .csv-table-wrap {
        border: 2px inset #888;
        background: #fff;
        max-height: 260px;
        overflow-y: auto;
        margin: 8px 0;
    }
    .csv-table {
        width: 100%;
        border-collapse: collapse;
        font-size: 12px;
    }
    .csv-table th {
        background: #c0c0c0;
        border: 1px solid #888;
        padding: 3px 6px;
        position: sticky;
        top: 0;
        text-align: left;
    }
    .csv-table td {
        border: 1px solid #ccc;
        padding: 3px 6px;
    }
    .btn-download {
        font-family: "Times New Roman", Times, serif;
        font-size: 14px;
        padding: 4px 16px;
        border: 2px outset #fff;
        background: #c0c0c0;
        cursor: pointer;
        margin-top: 8px;
    }
    .btn-download:active {
        border-style: inset;
    }
</style>
</head>
<body>
<div class="page">
    <div class="titlebar">
        <span>UCR-IDS</span>
        <div class="titlebar-buttons">
            <span>_</span><span>&square;</span><span>&times;</span>
        </div>
    </div>
    <div class="panel">
        <h1>Monitor de Biogestor v2</h1>
        <hr>
        <div class="readings">
            <div class="reading-box">
                <div class="reading-label">Temp de Biogestor</div>
                <div class="reading-value" id="bio">--</div>
                <div class="reading-unit">&deg;C</div>
            </div>
            <div class="reading-box">
                <div class="reading-label">Temp del tanque</div>
                <div class="reading-value" id="water">--</div>
                <div class="reading-unit">&deg;C</div>
            </div>
        </div>
        <div class="panel-inset">
            <div class="readings" style="margin:0">
                <div>
                    <div class="reading-label">Calentador</div>
                    <div class="status-line">
                        <span class="status-badge status-unknown" id="heater">--</span>
                    </div>
                </div>
                <div>
                    <div class="reading-label">Bomba/ motor</div>
                    <div class="status-line">
                        <span class="status-badge status-unknown" id="motor">--</span>
                    </div>
                </div>
            </div>
        </div>
        <hr>
        <div class="updated">Ultima actualizacion: <span id="time">--</span></div>
        <div class="connection" id="status"></div>
    </div>

    <!-- History panel -->
    <div class="panel">
        <h2>Historial de Temperatura</h2>
        <hr>
        <div class="chart-container">
            <canvas id="tempChart" height="200"></canvas>
        </div>
        <hr>
        <h2>Tabla de Datos</h2>
        <div class="csv-table-wrap">
            <table class="csv-table">
                <thead>
                    <tr><th>Hora</th><th>Temp Purin (&deg;C)</th><th>Circulacion</th></tr>
                </thead>
                <tbody id="csvBody"></tbody>
            </table>
        </div>
        <button class="btn-download" onclick="window.location.href='/api/history/csv'">Descargar CSV</button>
    </div>

    <div class="footer">Automatizacion por Bakuho Goto - UCR-IDS. 2026<br><a href="https://github.com/juuwaah/UCR-Biodigestor" style="color:#666">github.com/juuwaah/UCR-Biodigestor</a></div>
</div>

<script>
async function refresh() {
    try {
        const res = await fetch("/api/data");
        const d = await res.json();
        // Check if data is stale (no update in 60s)
        let stale = true;
        if (d.updated_at) {
            const utcDate = new Date(d.updated_at + " UTC");
            const ageSec = (Date.now() - utcDate.getTime()) / 1000;
            stale = ageSec > 60;
        }

        if (stale) {
            document.getElementById("bio").textContent = "--";
            document.getElementById("water").textContent = "--";
            const heaterEl = document.getElementById("heater");
            heaterEl.textContent = "--";
            heaterEl.className = "status-badge status-unknown";
            const motorEl = document.getElementById("motor");
            motorEl.textContent = "--";
            motorEl.className = "status-badge status-unknown";
            document.getElementById("time").textContent = "--";
            document.getElementById("status").innerHTML = '<span class="error">&#9679; DISPOSITIVO DESCONECTADO</span>';
            return;
        }

        document.getElementById("bio").textContent = d.biodigester_temp != null ? d.biodigester_temp.toFixed(1) : "--";
        document.getElementById("water").textContent = d.water_temp != null ? d.water_temp.toFixed(1) : "--";

        const heaterEl = document.getElementById("heater");
        if (d.heater != null) {
            heaterEl.textContent = d.heater ? "ON" : "OFF";
            heaterEl.className = "status-badge " + (d.heater ? "status-on" : "status-off");
        } else {
            heaterEl.textContent = "--";
            heaterEl.className = "status-badge status-unknown";
        }

        const motorEl = document.getElementById("motor");
        if (d.motor != null) {
            motorEl.textContent = d.motor ? "ON" : "OFF";
            motorEl.className = "status-badge " + (d.motor ? "status-on" : "status-off");
        } else {
            motorEl.textContent = "--";
            motorEl.className = "status-badge status-unknown";
        }

        // Convert to Costa Rica time (UTC-6)
        const utcDate2 = new Date(d.updated_at + " UTC");
        const crDate = new Date(utcDate2.getTime() - 6 * 60 * 60 * 1000);
        const h = String(crDate.getUTCHours()).padStart(2, "0");
        const m = String(crDate.getUTCMinutes()).padStart(2, "0");
        const s = String(crDate.getUTCSeconds()).padStart(2, "0");
        document.getElementById("time").textContent = h + ":" + m + ":" + s + " (Costa Rica)";
        document.getElementById("status").innerHTML = '<span class="live">&#9679; EN VIVO</span>';
    } catch (e) {
        document.getElementById("status").innerHTML = '<span class="error">&#9679; ERROR DE CONEXION</span>';
    }
}
refresh();
setInterval(refresh, 3000);

// ---------- History: graph + table ----------
let tempChart = null;

async function loadHistory() {
    try {
        const res = await fetch("/api/history");
        const rows = await res.json();
        if (!rows.length) return;

        const labels = rows.map(r => r.timestamp.split(" ")[1]);  // HH:MM
        const temps  = rows.map(r => r.biodigester_temp);

        // Chart
        const ctx = document.getElementById("tempChart").getContext("2d");
        if (tempChart) tempChart.destroy();
        tempChart = new Chart(ctx, {
            type: "line",
            data: {
                labels: labels,
                datasets: [{
                    label: "Temp Purin (°C)",
                    data: temps,
                    borderColor: "#000080",
                    backgroundColor: "rgba(0,0,128,0.1)",
                    borderWidth: 2,
                    pointRadius: 2,
                    fill: true,
                    tension: 0.3
                }]
            },
            options: {
                responsive: true,
                plugins: { legend: { display: false } },
                scales: {
                    x: { title: { display: true, text: "Hora" } },
                    y: { title: { display: true, text: "°C" } }
                }
            }
        });

        // Table
        const tbody = document.getElementById("csvBody");
        tbody.innerHTML = "";
        rows.forEach(r => {
            const tr = document.createElement("tr");
            const time = r.timestamp.split(" ")[1] || r.timestamp;
            const temp = r.biodigester_temp != null ? r.biodigester_temp.toFixed(1) : "--";
            const circ = r.heater != null ? (r.heater ? "ON" : "OFF") : "--";
            tr.innerHTML = "<td>" + time + "</td><td>" + temp + "</td><td>" + circ + "</td>";
            tbody.appendChild(tr);
        });
    } catch (e) {
        console.error("Failed to load history:", e);
    }
}
loadHistory();
</script>
</body>
</html>
"""

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
