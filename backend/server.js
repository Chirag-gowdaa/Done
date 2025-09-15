import express from "express";
import { spawn, exec } from "child_process";
import fs from "fs";
import cors from "cors";
import path from "path";

const app = express();
app.use(express.json());
app.use(cors());

/* ========== Global state ========== */
let currentWipe = null;
let currentFactory = null;

/* ========== Static frontend serving ========== */
const frontendPath = "/opt/wiper/frontend";
if (fs.existsSync(frontendPath)) {
  app.use(express.static(frontendPath));

  // Catch-all → send React index.html for non-API routes
  app.get("*", (req, res, next) => {
    if (req.path.startsWith("/api/")) return next();
    res.sendFile(path.join(frontendPath, "index.html"));
  });
}

/* ========== Global error handler ========== */
const safeHandler = (fn) => (req, res, next) =>
  Promise.resolve(fn(req, res, next)).catch(next);

app.use((err, req, res, next) => {
  console.error("💥 Uncaught error:", err);
  res.status(500).json({ error: "Internal server error" });
});

/* ========== Disk Listing ========== */
app.get(
  "/api/disks",
  safeHandler(async (req, res) => {
    exec("lsblk -o NAME,SIZE,TYPE,MOUNTPOINT -J", (err, stdout) => {
      if (err) return res.status(500).json({ error: "lsblk failed" });
      try {
        const data = JSON.parse(stdout);
        const disks = data.blockdevices.filter(
          (d) => d.type === "disk" && !d.name.startsWith("loop") && !d.name.startsWith("sr")
        );
        res.json({ disks });
      } catch {
        res.status(500).json({ error: "parse error" });
      }
    });
  })
);

/* ========== Disk Wipe ========== */
app.post("/api/wipe", safeHandler(async (req, res) => {
  const { device, method, sudoPassword } = req.body;
  if (!device || !method || !sudoPassword)
    return res.status(400).json({ error: "Missing fields" });

  currentWipe = { device, method, sudoPassword };
  fs.writeFileSync("/opt/wiper/backend/current_wipe.json", JSON.stringify(currentWipe, null, 2));
  res.json({ message: "Wipe queued" });
}));

app.get("/api/wipe-progress", (req, res) => {
  if (!currentWipe) return res.status(400).end("No wipe running");

  const { device, method, sudoPassword } = currentWipe;

  res.setHeader("Content-Type", "text/event-stream");
  res.setHeader("Cache-Control", "no-cache");
  res.setHeader("Connection", "keep-alive");
  res.flushHeaders();

  const wipeMethod = method === "random" ? "2" : "1";
  const wiperPath = path.resolve("/opt/wiper/backend/wiper");

  const child = spawn("sudo", ["-S", wiperPath, device, wipeMethod], {
    stdio: ["pipe", "pipe", "pipe"]
  });

  child.stdin.write(sudoPassword + "\n");
  child.stdin.end();

  child.stdout.on("data", (chunk) => {
    chunk.toString().split(/\r?\n/).forEach(line => {
      const m = line.match(/PROGRESS:(\d+)/);
      if (m) {
        res.write(`data: ${m[1]}\n\n`);
        fs.writeFileSync("/opt/wiper/backend/wipe_log.json", JSON.stringify({ progress: m[1] }, null, 2));
      }
    });
  });

  child.stderr.on("data", d => console.error("Wiper stderr:", d.toString()));

  child.on("exit", () => {
    try {
      const cert = fs.existsSync("/opt/wiper/backend/wipe_log.json")
        ? JSON.parse(fs.readFileSync("/opt/wiper/backend/wipe_log.json"))
        : { status: "UNKNOWN" };
      res.write(`data: 100\n\n`);
      res.write(`event: done\ndata: ${JSON.stringify(cert)}\n\n`);
    } catch {
      res.write(`event: done\ndata: ${JSON.stringify({ status: "FAILED" })}\n\n`);
    }
    res.end();
    currentWipe = null;
  });
});

/* ========== Factory Reset ========== */
app.post("/api/factory-reset", safeHandler(async (req, res) => {
  const { sudoPassword } = req.body;
  if (!sudoPassword) return res.status(400).json({ error: "Missing sudoPassword" });

  currentFactory = { sudoPassword };
  fs.writeFileSync("/opt/wiper/backend/current_factory.json", JSON.stringify(currentFactory, null, 2));
  res.json({ message: "Factory reset queued" });
}));

app.get("/api/factory-progress", (req, res) => {
  if (!currentFactory) return res.status(400).end("No factory reset running");

  const { sudoPassword } = currentFactory;
  res.setHeader("Content-Type", "text/event-stream");
  res.setHeader("Cache-Control", "no-cache");
  res.setHeader("Connection", "keep-alive");
  res.flushHeaders();

  const factoryPath = path.resolve("/opt/wiper/backend/factoryreset");

  const child = spawn("sudo", ["-S", factoryPath], { stdio: ["pipe", "pipe", "pipe"] });
  child.stdin.write(sudoPassword + "\n");
  child.stdin.write("y\n");
  child.stdin.end();

  child.stdout.on("data", (chunk) => {
    chunk.toString().split(/\r?\n/).forEach(line => {
      const m = line.match(/PROGRESS:(\d+)/);
      if (m) {
        res.write(`data: ${m[1]}\n\n`);
        fs.writeFileSync("/opt/wiper/backend/factory_log.json", JSON.stringify({ progress: m[1] }, null, 2));
      }
    });
  });

  child.stderr.on("data", d => console.error("Factory stderr:", d.toString()));

  child.on("exit", () => {
    try {
      const cert = fs.existsSync("/opt/wiper/backend/factory_log.json")
        ? JSON.parse(fs.readFileSync("/opt/wiper/backend/factory_log.json"))
        : { status: "UNKNOWN" };
      res.write(`data: 100\n\n`);
      res.write(`event: done\ndata: ${JSON.stringify(cert)}\n\n`);
    } catch {
      res.write(`event: done\ndata: ${JSON.stringify({ status: "FAILED" })}\n\n`);
    }
    res.end();
    currentFactory = null;
  });
});

/* ========== Test endpoint ========== */
app.get("/api/test", (req, res) => res.send("Backend alive!"));

// ===== Start server =====
const PORT = 5000;
app.listen(PORT, () => console.log(`✅ Backend running at http://localhost:${PORT}`));
