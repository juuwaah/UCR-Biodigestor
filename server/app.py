from flask import Flask, request, jsonify, render_template_string
from datetime import datetime, timezone, timedelta

app = Flask(__name__)

# Latest data from ESP32 (in-memory only)
latest = {
    "biodigester_temp": None,
    "water_temp": None,
    "heater": None,
    "motor": None,
    "updated_at": None,
}

JST = timezone(timedelta(hours=9))

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
    latest["updated_at"] = datetime.now(JST).strftime("%Y-%m-%d %H:%M:%S")
    return jsonify({"status": "ok"})


# JSON endpoint for API consumers
@app.route("/api/data", methods=["GET"])
def get_data():
    return jsonify(latest)


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
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { font-family: 'Helvetica Neue', sans-serif; background: #0f1117; color: #e0e0e0; min-height: 100vh; display: flex; flex-direction: column; align-items: center; justify-content: center; }
  h1 { font-size: 1.4rem; margin-bottom: 24px; color: #8899aa; font-weight: 400; }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; max-width: 420px; width: 90%; }
  .card { background: #1a1d27; border-radius: 12px; padding: 20px; text-align: center; }
  .label { font-size: 0.75rem; color: #667; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px; }
  .value { font-size: 2.2rem; font-weight: 700; }
  .unit { font-size: 0.9rem; color: #667; }
  .on { color: #f06040; }
  .off { color: #40b080; }
  .temp { color: #50aaff; }
  .updated { margin-top: 24px; font-size: 0.7rem; color: #445; }
  #status { margin-top: 8px; font-size: 0.7rem; }
  .live { color: #40b080; }
  .stale { color: #f06040; }
</style>
</head>
<body>
  <h1>Biodigester Monitor</h1>
  <div class="grid">
    <div class="card">
      <div class="label">Biodigester</div>
      <div class="value temp" id="bio">--</div>
      <div class="unit">&deg;C</div>
    </div>
    <div class="card">
      <div class="label">Water Tank</div>
      <div class="value temp" id="water">--</div>
      <div class="unit">&deg;C</div>
    </div>
    <div class="card">
      <div class="label">Heater</div>
      <div class="value" id="heater">--</div>
    </div>
    <div class="card">
      <div class="label">Motor</div>
      <div class="value" id="motor">--</div>
    </div>
  </div>
  <div class="updated">Last update: <span id="time">--</span></div>
  <div id="status"></div>

<script>
async function refresh() {
  try {
    const res = await fetch("/api/data");
    const d = await res.json();
    document.getElementById("bio").textContent = d.biodigester_temp != null ? d.biodigester_temp.toFixed(1) : "--";
    document.getElementById("water").textContent = d.water_temp != null ? d.water_temp.toFixed(1) : "--";

    const heaterEl = document.getElementById("heater");
    heaterEl.textContent = d.heater != null ? (d.heater ? "ON" : "OFF") : "--";
    heaterEl.className = "value " + (d.heater ? "on" : "off");

    const motorEl = document.getElementById("motor");
    motorEl.textContent = d.motor != null ? (d.motor ? "ON" : "OFF") : "--";
    motorEl.className = "value " + (d.motor ? "on" : "off");

    document.getElementById("time").textContent = d.updated_at || "--";
    document.getElementById("status").innerHTML = '<span class="live">LIVE</span>';
  } catch (e) {
    document.getElementById("status").innerHTML = '<span class="stale">CONNECTION ERROR</span>';
  }
}
refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
"""

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
