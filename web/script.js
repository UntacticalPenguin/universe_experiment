import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const viewer = document.getElementById('viewer');
const crosshair = document.getElementById('crosshair');
const toggleCrosshairBtn = document.getElementById('toggle-crosshair');
const togglePlayBtn = document.getElementById('toggle-play');
const sunLabel = document.getElementById('sun-label');
const sunLine = document.getElementById('sun-line');
const statStars = document.getElementById('stat-stars');
const statFrame = document.getElementById('stat-frame');
const statYears = document.getElementById('stat-years');
const statError = document.getElementById('stat-error');

let playing = true;
let showCrosshair = true;

let frameFloatArrays = [];
let starMeta = [];
let sunIndex = -1;
let currentFrame = 0;
let currentFrameFloat = 0;
let frameCount = 0;
let yearsPerFrame = 0;

let positions = null;
let colors = null;
let sizes = null;
let points = null;

function setError(message) {
  console.error(message);
  if (statError) {
    statError.textContent = `Error: ${message}`;
  }
}

const renderer = new THREE.WebGLRenderer({
  antialias: true,
  powerPreference: 'high-performance'
});
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setClearColor(0x020202, 1);
viewer.appendChild(renderer.domElement);
renderer.domElement.addEventListener('contextmenu', (event) => event.preventDefault());

const scene = new THREE.Scene();

const camera = new THREE.PerspectiveCamera(
  60,
  window.innerWidth / window.innerHeight,
  0.1,
  50000
);
camera.position.set(0, 35, 130);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.06;
controls.screenSpacePanning = true;
controls.mouseButtons.LEFT = THREE.MOUSE.ROTATE;
controls.mouseButtons.RIGHT = THREE.MOUSE.PAN;
controls.mouseButtons.MIDDLE = THREE.MOUSE.DOLLY;
controls.target.set(0, 0, 0);

const geometry = new THREE.BufferGeometry();

function magnitudeToSize(mag) {
  return Math.max(0.9, Math.min(7.0, 5.2 - 0.42 * mag));
}

function colorForStar(star) {
  if (star.isSun) return [1.0, 0.92, 0.2];
  return [1.0, 1.0, 1.0];
}

const material = new THREE.ShaderMaterial({
  transparent: true,
  depthWrite: false,
  vertexColors: true,
  uniforms: {
    uPixelRatio: { value: renderer.getPixelRatio() }
  },
  vertexShader: `
    attribute float aSize;
    varying vec3 vColor;
    uniform float uPixelRatio;

    void main() {
      vColor = color;
      vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
      gl_Position = projectionMatrix * mvPosition;

      float dist = max(1.0, -mvPosition.z);
      gl_PointSize = aSize * uPixelRatio * (180.0 / dist);
      gl_PointSize = clamp(gl_PointSize, 1.0, 18.0);
    }
  `,
  fragmentShader: `
    varying vec3 vColor;

    void main() {
      vec2 c = gl_PointCoord - vec2(0.5);
      float d = dot(c, c);
      // Schärfere Kante, kleinerer Fade-Radius
      if (d > 0.09) discard;
      float alpha = smoothstep(0.09, 0.0, d);
      gl_FragColor = vec4(vColor, alpha);
    }
  `
});

function fitCamera() {
  if (!frameFloatArrays.length || !frameFloatArrays[0].length) {
    setError('scene.json contains no frame position data');
    return;
  }

  const box = new THREE.Box3();
  const frame = frameFloatArrays[0];
  const temp = new THREE.Vector3();

  for (let i = 0; i < frame.length; i += 3) {
    temp.set(frame[i], frame[i + 1], frame[i + 2]);
    box.expandByPoint(temp);
  }

  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3());
  const maxDim = Math.max(size.x, size.y, size.z, 1);

  controls.target.copy(center);
  camera.position.copy(
    center.clone().add(new THREE.Vector3(maxDim * 0.8, maxDim * 0.5, maxDim * 1.3))
  );
  controls.update();
}

function updateFrame(frameFloat) {
  if (frameCount < 2) return;

  const maxBaseIndex = frameCount - 1;
  const wrapped = ((frameFloat % maxBaseIndex) + maxBaseIndex) % maxBaseIndex;
  const i0 = Math.floor(wrapped);
  const t = wrapped - i0;
  const i1 = Math.min(i0 + 1, frameCount - 1);

  const a = frameFloatArrays[i0];
  const b = frameFloatArrays[i1];

  for (let i = 0; i < positions.length; i++) {
    positions[i] = a[i] + (b[i] - a[i]) * t;
  }

  geometry.attributes.position.needsUpdate = true;
  currentFrame = i0;
  statFrame.textContent = `Frame: ${i0 + 1}/${frameCount}`;
}

function updateSunLabel() {
  if (sunIndex < 0 || !positions) {
    sunLabel.style.display = 'none';
    return;
  }

  const x = positions[sunIndex * 3 + 0];
  const y = positions[sunIndex * 3 + 1];
  const z = positions[sunIndex * 3 + 2];

  const projected = new THREE.Vector3(x, y, z).project(camera);

  const visible =
    projected.z > -1 &&
    projected.z < 1 &&
    projected.x >= -1.2 &&
    projected.x <= 1.2 &&
    projected.y >= -1.2 &&
    projected.y <= 1.2;

  if (!visible) {
    sunLabel.style.display = 'none';
    return;
  }

  const sx = (projected.x * 0.5 + 0.5) * window.innerWidth;
  const sy = (-projected.y * 0.5 + 0.5) * window.innerHeight;

  sunLabel.style.display = 'block';
  sunLabel.style.left = `${sx}px`;
  sunLabel.style.top = `${sy}px`;

  // Dynamische Verbindung zur Box
  const sunBox = document.getElementById('sun-box');
  const labelRect = sunLabel.getBoundingClientRect();
  const boxRect = sunBox.getBoundingClientRect();

  // Startpunkt: Mittelpunkt der Sonne
  const startX = labelRect.left + labelRect.width / 2;
  const startY = labelRect.top + labelRect.height / 2;
  // Endpunkt: Mittelpunkt der Box
  const endX = boxRect.left + boxRect.width / 2;
  const endY = boxRect.top + boxRect.height / 2;

  // Berechne Winkel und Länge
  const dx = endX - startX;
  const dy = endY - startY;
  const angle = Math.atan2(dy, dx) * 180 / Math.PI;
  const length = Math.sqrt(dx * dx + dy * dy);

  sunLine.style.width = `${length}px`;
  sunLine.style.transform = `rotate(${angle}deg)`;
  sunLine.style.left = '0px';
  sunLine.style.top = '0px';
}

function animate() {
  requestAnimationFrame(animate);

  if (playing && frameCount > 1) {
    currentFrameFloat += 0.12;
    updateFrame(currentFrameFloat);
  }

  controls.update();
  updateSunLabel();
  renderer.render(scene, camera);
}

async function loadSceneData() {
  try {
    const response = await fetch('scene.json', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status} while loading scene.json`);
    }

    const data = await response.json();

    if (!data || !Array.isArray(data.stars) || !Array.isArray(data.frames)) {
      throw new Error('scene.json has an unexpected structure');
    }

    starMeta = data.stars;
    frameCount = Number(data.frameCount ?? data.frames.length ?? 0);
    yearsPerFrame = Number(data.yearsPerFrame ?? 0);

    if (frameCount !== data.frames.length) {
      frameCount = data.frames.length;
    }

    frameFloatArrays = data.frames.map((frame, idx) => {
      const arr = new Float32Array(frame);
      if (arr.length !== starMeta.length * 3) {
        throw new Error(
          `frame ${idx} has ${arr.length} values, expected ${starMeta.length * 3}`
        );
      }
      return arr;
    });

    if (!frameFloatArrays.length) {
      throw new Error('scene.json contains zero frames');
    }

    positions = new Float32Array(frameFloatArrays[0].length);
    colors = new Float32Array(starMeta.length * 3);
    sizes = new Float32Array(starMeta.length);

    for (let i = 0; i < starMeta.length; i++) {
      const [r, g, b] = colorForStar(starMeta[i]);
      colors[i * 3 + 0] = r;
      colors[i * 3 + 1] = g;
      colors[i * 3 + 2] = b;
      sizes[i] = magnitudeToSize(starMeta[i].mag ?? 10);

      if (starMeta[i].isSun) {
        sunIndex = i;
      }
    }

    positions.set(frameFloatArrays[0]);

    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    geometry.setAttribute('aSize', new THREE.BufferAttribute(sizes, 1));

    points = new THREE.Points(geometry, material);
    scene.add(points);

    fitCamera();

    statStars.textContent = `Stars: ${starMeta.length}`;
    statYears.textContent = `Years/frame: ${yearsPerFrame.toLocaleString()}`;
    statError.textContent = '';

    animate();
  } catch (error) {
    setError(error instanceof Error ? error.message : String(error));
  }
}

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
  material.uniforms.uPixelRatio.value = renderer.getPixelRatio();
});

toggleCrosshairBtn.addEventListener('click', () => {
  showCrosshair = !showCrosshair;
  crosshair.style.display = showCrosshair ? 'block' : 'none';
  toggleCrosshairBtn.textContent = `Crosshair: ${showCrosshair ? 'On' : 'Off'}`;
});

togglePlayBtn.addEventListener('click', () => {
  playing = !playing;
  togglePlayBtn.textContent = playing ? 'Pause' : 'Play';
});

loadSceneData();