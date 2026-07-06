async function refresh() {
    try {
        const res = await fetch("/api/data");
        const d = await res.json();

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

        // UTC → Costa Rica (UTC-6)
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

        const ctx = document.getElementById("tempChart").getContext("2d");

        if (!rows.length) {
            if (tempChart) { tempChart.destroy(); tempChart = null; }
            ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
            ctx.fillStyle = "#888";
            ctx.font = "14px Times New Roman";
            ctx.textAlign = "center";
            ctx.fillText("Sin datos aún", ctx.canvas.width / 2, ctx.canvas.height / 2);
            document.getElementById("csvBody").innerHTML = "";
            return;
        }

        const labels = rows.map(r => r.timestamp.split(" ")[1]);
        const temps  = rows.map(r => r.biodigester_temp);

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
        const wrap = document.querySelector(".csv-table-wrap");
        wrap.scrollTop = wrap.scrollHeight;
    } catch (e) {
        console.error("Failed to load history:", e);
    }
}
loadHistory();
setInterval(loadHistory, 30000);
