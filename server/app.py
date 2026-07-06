from flask import Flask, request, jsonify, render_template, Response
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
    return render_template("index.html")

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
