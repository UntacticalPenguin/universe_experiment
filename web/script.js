const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");

let stars = [];
let lastTime = 0;

// Camera
let zoom = 1.0;
let verticalScale = 0.6;
let offsetX = 0;
let offsetY = 0;

// Dragging
let isDragging = false;
let lastMouseX = 0;
let lastMouseY = 0;

// Simulation speed
const yearsPerSecond = 10000.0;

function resizeCanvas() {
  canvas.width = window.innerWidth;
  canvas.height = window.innerHeight;
}

window.addEventListener("resize", () => {
  resizeCanvas();
  fitView();
});

resizeCanvas();

function radiusFromMagnitude(mag) {
  let r = 4.5 - 0.45 * mag;
  if (r < 0.5) r = 0.5;
  if (r > 6.0) r = 6.0;
  return r;
}

// Fixed projection:
// zoom affects the entire projected result uniformly.
function project(x, y, z) {
  return {
    x: (x - y) * zoom,
    y: ((x + y) * 0.25 - z * verticalScale) * zoom
  };
}

function fitView() {
  if (stars.length === 0) return;

  let minX = Infinity;
  let maxX = -Infinity;
  let minY = Infinity;
  let maxY = -Infinity;

  // Measure projected bounds at zoom = 1
  for (const star of stars) {
    const px = (star.px - star.py);
    const py = (star.px + star.py) * 0.25 - star.pz * verticalScale;

    if (px < minX) minX = px;
    if (px > maxX) maxX = px;
    if (py < minY) minY = py;
    if (py > maxY) maxY = py;
  }

  const contentWidth = Math.max(1, maxX - minX);
  const contentHeight = Math.max(1, maxY - minY);

  const margin = 0.9;
  const zoomX = (canvas.width * margin) / contentWidth;
  const zoomY = (canvas.height * margin) / contentHeight;

  zoom = Math.min(zoomX, zoomY);

  const centerProjectedX = (minX + maxX) * 0.5 * zoom;
  const centerProjectedY = (minY + maxY) * 0.5 * zoom;

  offsetX = -centerProjectedX;
  offsetY = -centerProjectedY;
}

canvas.addEventListener("wheel", (event) => {
  event.preventDefault();

  const factor = event.deltaY < 0 ? 1.1 : 0.9;
  zoom *= factor;

  if (zoom < 0.0001) zoom = 0.0001;
  if (zoom > 10000) zoom = 10000;
}, { passive: false });

canvas.addEventListener("mousedown", (event) => {
  isDragging = true;
  lastMouseX = event.clientX;
  lastMouseY = event.clientY;
});

window.addEventListener("mouseup", () => {
  isDragging = false;
});

window.addEventListener("mousemove", (event) => {
  if (!isDragging) return;

  const dx = event.clientX - lastMouseX;
  const dy = event.clientY - lastMouseY;

  offsetX += dx;
  offsetY += dy;

  lastMouseX = event.clientX;
  lastMouseY = event.clientY;
});

async function loadStars() {
  const response = await fetch("stars.json");
  stars = await response.json();

  console.log("Loaded stars:", stars.length);
  fitView();
  requestAnimationFrame(render);
}

function render(timestamp) {
  if (!lastTime) lastTime = timestamp;

  const dtSeconds = (timestamp - lastTime) / 1000.0;
  lastTime = timestamp;

  const dtYears = dtSeconds * yearsPerSecond;

  ctx.fillStyle = "black";
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  const centerX = canvas.width * 0.5;
  const centerY = canvas.height * 0.5;

  let drawnCount = 0;

  for (const star of stars) {
    // Animate using only dtYears
    star.px += star.vx * dtYears;
    star.py += star.vy * dtYears;
    star.pz += star.vz * dtYears;

    const p = project(star.px, star.py, star.pz);

    const screenX = centerX + offsetX + p.x;
    const screenY = centerY + offsetY + p.y;

    if (
      screenX < -20 ||
      screenX > canvas.width + 20 ||
      screenY < -20 ||
      screenY > canvas.height + 20
    ) {
      continue;
    }

    const r = radiusFromMagnitude(star.mag);

    ctx.beginPath();
    ctx.arc(screenX, screenY, r, 0, Math.PI * 2);
    ctx.fillStyle = "white";
    if (star.name == "Sol"){
        ctx.fillStyle = "yellow"
    }
    ctx.globalAlpha = 0.9;
    ctx.fill();

    drawnCount++;
  }

  ctx.globalAlpha = 1.0;
  ctx.fillStyle = "lime";
  ctx.font = "14px sans-serif";
  ctx.fillText(`Stars: ${stars.length}`, 12, 22);
  ctx.fillText(`Drawn: ${drawnCount}`, 12, 42);
  ctx.fillText(`Zoom: ${zoom.toFixed(4)}`, 12, 62);
  ctx.fillText(`Years/sec: ${yearsPerSecond}`, 12, 82);

  requestAnimationFrame(render);
}

loadStars();