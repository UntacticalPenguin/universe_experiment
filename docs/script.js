import * as THREE from 'three';
import { TrackballControls } from 'three/addons/controls/TrackballControls.js';

const viewer = document.getElementById('viewer');
const crosshair = document.getElementById('crosshair');
const labelLayer = document.getElementById('label-layer');
const labelList = document.getElementById('label-list');
const labelSearchInput = document.getElementById('label-search');
const labelCountEl = document.getElementById('label-count');
const toggleAllLabelsBtn = document.getElementById('toggle-all-labels');
const toggleCrosshairBtn = document.getElementById('toggle-crosshair');
const toggleLockSunBtn = document.getElementById('toggle-lock-sun');
const toggleStarScalingBtn = document.getElementById('toggle-star-scaling');
const toggleCinematicOrbitBtn = document.getElementById('toggle-cinematic-orbit');
const togglePlayBtn = document.getElementById('toggle-play');
const orbitVectorGrid = document.getElementById('orbit-vector-grid');
const orbitVectorButtons = Array.from(document.querySelectorAll('.orbit-vector-button'));
const orbitVectorSummary = document.getElementById('orbit-vector-summary');
const orbitVectorResetBtn = document.getElementById('orbit-vector-reset');
const orbitCompassArrow = document.getElementById('orbit-compass-arrow');
const orbitSpeedInput = document.getElementById('orbit-speed');
const orbitSpeedLabel = document.getElementById('orbit-speed-label');
const statStars = document.getElementById('stat-stars');
const statGas = document.getElementById('stat-gas');
const statFrame = document.getElementById('stat-frame');
const statYears = document.getElementById('stat-years');
const statVisible = document.getElementById('stat-visible');
const statError = document.getElementById('stat-error');
const statCamera = document.getElementById('stat-camera');

const MAX_SCENE_BYTES = 250 * 1024 * 1024;
const MOTION_EXAGGERATION = 40.0;
const FRAME_ADVANCE_PER_SECOND = 5.4;
const LABEL_VISIBILITY_MARGIN = 1.15;
const ORBIT_AXIS_MAP = {
  x: new THREE.Vector3(1, 0, 0),
  y: new THREE.Vector3(0, 1, 0),
  z: new THREE.Vector3(0, 0, 1)
};

// Add your own labels here. Use either starId, starName, or a fixed position.
const EXTRA_LABEL_DEFINITIONS = [
  {
    key: 'galactic-center',
    label: 'Galactic Center',
    color: '#72d8ff',
    position: { x: 0, y: 0, z: 0 },
    offset: { x: 72, y: 18 },
    enabled: true
  }
];

let playing = true;
let showCrosshair = true;
let lockOnSun = false;
let starScaling = true;
let animationEnabled = true;
let cinematicOrbitEnabled = false;
let cinematicOrbitSpeed = Number(orbitSpeedInput.value) / 100;
let orbitVector = { x: 0, y: 1, z: 1 };
let orbitActivationOrder = ['y', 'z'];
let interfaceHidden = false;
let labelsHiddenGlobally = false;
let labelFilterQuery = '';

let frameFloatArrays = [];
let starMeta = [];
let sunIndex = -1;
let currentFrameFloat = 0;
let frameCount = 0;
let yearsPerFrame = 0;

let masterPositions = null;
let baseFramePositions = null;
let masterColors = null;
let masterSizes = null;
let visiblePositions = null;
let visibleColors = null;
let visibleSizes = null;
let visibleMap = [];
let points = null;
let crosshairObject = null;
let animationFaulted = false;
let continuousLoopRunning = false;
let singleRenderQueued = false;
let lastLoopTimestamp = 0;
let interactionActive = false;

const labelStates = new Map();
const tempVec3 = new THREE.Vector3();
const tempVec3B = new THREE.Vector3();
const frustum = new THREE.Frustum();
const projScreenMatrix = new THREE.Matrix4();

function formatBytes(bytes) {
  if (!Number.isFinite(bytes) || bytes <= 0) {
    return '0 B';
  }

  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  const exponent = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1);
  const value = bytes / Math.pow(1024, exponent);
  return `${value.toFixed(value >= 10 || exponent === 0 ? 0 : 1)} ${units[exponent]}`;
}

function logMemory(stage) {
  const mem = performance && performance.memory;
  if (!mem) {
    console.log(`[memory] ${stage}: unavailable in this browser`);
    return;
  }

  console.log(
    `[memory] ${stage}: used ${formatBytes(mem.usedJSHeapSize)}, total ${formatBytes(mem.totalJSHeapSize)}, limit ${formatBytes(mem.jsHeapSizeLimit)}`
  );
}

function setError(message) {
  console.error(message);
  statError.textContent = `Error: ${message}`;
}

function clearError() {
  statError.textContent = '';
}

function bpRpToColor(ci, isSun) {
  if (isSun) {
    return [1.0, 0.88, 0.22];
  }

  const t = Math.max(-0.3, Math.min(2.4, Number(ci ?? 0.8)));
  if (t < 0.2) return [0.72, 0.82, 1.0];
  if (t < 0.5) return [0.82, 0.88, 1.0];
  if (t < 0.9) return [0.95, 0.96, 1.0];
  if (t < 1.3) return [1.0, 0.94, 0.82];
  if (t < 1.8) return [1.0, 0.83, 0.62];
  return [1.0, 0.73, 0.55];
}

function magnitudeToSize(mag, isSun) {
  if (isSun) {
    return 14.0;
  }
  const m = Number(mag ?? 12.0);
  const raw = 7.4 - 0.34 * m;
  const capped = m < 8.0 ? Math.min(raw, 1.4) : raw;
  return Math.max(1.2, Math.min(7.0, capped));
}

function axisLabel(key) {
  return key === 'x' ? 'X' : key === 'y' ? 'Y' : 'Z';
}

function describeOrbitVector() {
  const active = [];
  for (const axis of ['x', 'y', 'z']) {
    const value = orbitVector[axis];
    if (value > 0) {
      active.push(`+${axisLabel(axis)}`);
    } else if (value < 0) {
      active.push(`-${axisLabel(axis)}`);
    }
  }
  return active.length > 0 ? active.join(', ') : 'none';
}

function setOrbitAxis(axis, sign) {
  const current = orbitVector[axis];
  if (current === sign) {
    orbitVector[axis] = 0;
    orbitActivationOrder = orbitActivationOrder.filter((entry) => entry !== axis);
    return;
  }

  if (current === 0 && orbitActivationOrder.length >= 2) {
    const removed = orbitActivationOrder.shift();
    if (removed) {
      orbitVector[removed] = 0;
    }
  } else {
    orbitActivationOrder = orbitActivationOrder.filter((entry) => entry !== axis);
  }

  orbitVector[axis] = sign;
  orbitActivationOrder.push(axis);
}

function toggleInterfaceVisibility(hidden) {
  interfaceHidden = hidden;
  document.body.classList.toggle('ui-hidden', interfaceHidden);
}

function starNameToLabelKey(star) {
  const idPart = Number.isFinite(Number(star.id)) ? Number(star.id) : normalizeLabelTargetName(star.name || 'unnamed');
  return `star-${idPart}`;
}

function rgbArrayToHex(rgb) {
  const r = Math.max(0, Math.min(255, Math.round(rgb[0] * 255)));
  const g = Math.max(0, Math.min(255, Math.round(rgb[1] * 255)));
  const b = Math.max(0, Math.min(255, Math.round(rgb[2] * 255)));
  return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;
}

function hashStringFNV1a(text) {
  let hash = 0x811c9dc5;
  for (let i = 0; i < text.length; i += 1) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193);
  }
  return hash >>> 0;
}

function hslToRgb(h, s, l) {
  const hue = ((h % 360) + 360) % 360;
  const sat = Math.max(0, Math.min(1, s));
  const light = Math.max(0, Math.min(1, l));

  const c = (1 - Math.abs(2 * light - 1)) * sat;
  const x = c * (1 - Math.abs(((hue / 60) % 2) - 1));
  const m = light - c / 2;

  let r = 0;
  let g = 0;
  let b = 0;

  if (hue < 60) {
    r = c; g = x; b = 0;
  } else if (hue < 120) {
    r = x; g = c; b = 0;
  } else if (hue < 180) {
    r = 0; g = c; b = x;
  } else if (hue < 240) {
    r = 0; g = x; b = c;
  } else if (hue < 300) {
    r = x; g = 0; b = c;
  } else {
    r = c; g = 0; b = x;
  }

  return [r + m, g + m, b + m];
}

function seededBrightColorForStar(star, index) {
  const name = String(star.name ?? '').trim();
  const px = Number(baseFramePositions?.[index * 3 + 0] ?? 0);
  const py = Number(baseFramePositions?.[index * 3 + 1] ?? 0);
  const pz = Number(baseFramePositions?.[index * 3 + 2] ?? 0);

  const seedText = `${Number(star.id) || 0}|${name}|${px.toFixed(2)}|${py.toFixed(2)}|${pz.toFixed(2)}`;
  const hash = hashStringFNV1a(seedText);
  // golden-angle offset (~137.508°) keeps neighbouring entries far apart on the colour wheel
  const hue = ((hash % 360) + index * 137.508) % 360;
  const saturation = 0.78 + ((hash >>> 9) % 18) / 100;
  const lightness = 0.60 + ((hash >>> 17) % 14) / 100;

  return rgbArrayToHex(hslToRgb(hue, saturation, lightness));
}

function buildStarNameLabelDefinitions() {
  const definitions = [];
  for (let i = 0; i < starMeta.length; i += 1) {
    const star = starMeta[i];
    const name = String(star.name ?? '').trim();
    if (!name) {
      continue;
    }
    if (star.isSun) {
      continue;
    }

    const color = seededBrightColorForStar(star, i);
    definitions.push({
      key: starNameToLabelKey(star),
      label: name,
      color,
      starId: Number(star.id),
      offset: { x: 58, y: -18 },
      enabled: false
    });
  }
  return definitions;
}

function normalizeLabelTargetName(value) {
  return String(value ?? '').trim().toLowerCase();
}

function hexToRgbComponents(color) {
  const normalized = String(color ?? '#ffffff').trim();
  const expanded = normalized.length === 4
    ? `#${normalized[1]}${normalized[1]}${normalized[2]}${normalized[2]}${normalized[3]}${normalized[3]}`
    : normalized;
  const match = /^#([0-9a-f]{6})$/i.exec(expanded);
  if (!match) {
    return { r: 255, g: 255, b: 255 };
  }

  const value = Number.parseInt(match[1], 16);
  return {
    r: (value >> 16) & 255,
    g: (value >> 8) & 255,
    b: value & 255
  };
}

function createLabelDom(definition) {
  const root = document.createElement('div');
  root.className = 'scene-label';
  root.style.display = 'none';
  root.style.setProperty('--label-color', definition.color);

  const anchor = document.createElement('div');
  anchor.className = 'scene-label-anchor';
  const joint = document.createElement('div');
  joint.className = 'scene-label-joint';
  const line = document.createElement('div');
  line.className = 'scene-label-line';
  const box = document.createElement('div');
  box.className = 'scene-label-box';
  box.textContent = definition.label;

  root.append(anchor, line, joint, box);
  labelLayer.appendChild(root);
  return { root, anchor, line, joint, box };
}

function ensureLabelState(definition, options = {}) {
  const existing = labelStates.get(definition.key);
  if (existing) {
    return existing;
  }

  const dom = createLabelDom(definition);
  const state = {
    definition,
    dom,
    enabled: Boolean(options.enabled),
    targetType: 'none',
    starIndex: -1,
    listItem: null,
    toggleButton: null
  };
  labelStates.set(definition.key, state);
  return state;
}

function buildExtraLabelState(definition) {
  return ensureLabelState(definition, { enabled: Boolean(definition.enabled) });
}

function getLabelOffset(definition) {
  const offset = definition.offset ?? {};
  return {
    x: Number(offset.x ?? 56),
    y: Number(offset.y ?? -18)
  };
}

function resolveLabelStateTarget(state) {
  const { definition } = state;

  if (Number.isFinite(definition.starId)) {
    state.starIndex = starMeta.findIndex((star) => Number(star.id) === Number(definition.starId));
    state.targetType = state.starIndex >= 0 ? 'star' : 'none';
    return;
  }

  if (definition.starName) {
    const expected = normalizeLabelTargetName(definition.starName);
    state.starIndex = starMeta.findIndex((star) => normalizeLabelTargetName(star.name) === expected);
    state.targetType = state.starIndex >= 0 ? 'star' : 'none';
    return;
  }

  if (definition.position && Number.isFinite(definition.position.x) && Number.isFinite(definition.position.y) && Number.isFinite(definition.position.z)) {
    state.targetType = 'position';
    state.starIndex = -1;
    return;
  }

  state.targetType = 'none';
  state.starIndex = -1;
}

function updateLabelListUI() {
  const query = normalizeLabelTargetName(labelFilterQuery);
  let matchCount = 0;
  let totalCount = 0;

  for (const state of labelStates.values()) {
    if (!state.listItem || !state.toggleButton) {
      continue;
    }

    totalCount += 1;

    const resolved = state.targetType !== 'none';
    state.listItem.classList.toggle('is-unresolved', !resolved);
    state.toggleButton.disabled = !resolved;
    state.toggleButton.textContent = state.enabled ? 'Hide' : 'Show';
    state.toggleButton.classList.toggle('is-active', state.enabled);

    const meta = state.listItem.querySelector('.label-meta');
    if (meta) {
      if (state.definition.position) {
        meta.textContent = resolved ? 'Fixed position' : 'Invalid fixed position';
      } else if (Number.isFinite(state.definition.starId)) {
        meta.textContent = resolved ? `Resolved by id ${state.definition.starId}` : `Missing id ${state.definition.starId}`;
      } else {
        meta.textContent = resolved ? `Resolved by name ${state.definition.starName}` : `Missing name ${state.definition.starName}`;
      }
    }

    const labelText = normalizeLabelTargetName(state.definition.label);
    const metaText = normalizeLabelTargetName(meta?.textContent ?? '');
    const isMatch = !query || labelText.includes(query) || metaText.includes(query);
    state.listItem.style.display = isMatch ? '' : 'none';
    if (isMatch) matchCount += 1;
  }

  if (labelCountEl) {
    labelCountEl.textContent = query && matchCount < totalCount
      ? `${matchCount} / ${totalCount}`
      : `${totalCount} labels`;
  }
}

function buildLabelList() {
  for (const [key, state] of labelStates.entries()) {
    state.dom.root.remove();
    labelStates.delete(key);
  }

  labelList.textContent = '';

  const allListDefinitions = [...EXTRA_LABEL_DEFINITIONS, ...buildStarNameLabelDefinitions()];
  for (const definition of allListDefinitions) {
    const state = buildExtraLabelState(definition);
    resolveLabelStateTarget(state);

    const item = document.createElement('div');
    item.className = 'label-item';

    const swatch = document.createElement('div');
    swatch.className = 'label-swatch';
    swatch.style.setProperty('--swatch-color', definition.color);

    const copy = document.createElement('div');
    copy.className = 'label-copy';

    const name = document.createElement('div');
    name.className = 'label-name';
    name.textContent = definition.label;

    const meta = document.createElement('div');
    meta.className = 'label-meta';

    const toggle = document.createElement('button');
    toggle.className = 'label-toggle';
    toggle.addEventListener('click', () => {
      if (state.targetType === 'none') {
        return;
      }
      state.enabled = !state.enabled;
      updateLabelListUI();
      requestRender();
    });

    copy.append(name, meta);
    item.append(swatch, copy, toggle);
    labelList.appendChild(item);

    state.listItem = item;
    state.toggleButton = toggle;
  }

  updateLabelListUI();
}

function getLabelWorldPosition(state, output) {
  if (state.targetType === 'position') {
    output.set(state.definition.position.x, state.definition.position.y, state.definition.position.z);
    return true;
  }

  const targetIndex = state.starIndex;
  if (targetIndex < 0 || !masterPositions) {
    return false;
  }

  output.set(
    masterPositions[targetIndex * 3 + 0],
    masterPositions[targetIndex * 3 + 1],
    masterPositions[targetIndex * 3 + 2]
  );
  return true;
}

function isLabelTargetVisible(state) {
  if (state.targetType === 'position') {
    return true;
  }

  const targetIndex = state.starIndex;
  return targetIndex >= 0 && visibleMap.includes(targetIndex);
}

function updateLabelVisual(state) {
  const { root, line, joint, box } = state.dom;
  const shouldShow = state.enabled;

  if (labelsHiddenGlobally || !shouldShow || state.targetType === 'none' || !getLabelWorldPosition(state, tempVec3) || !isLabelTargetVisible(state)) {
    root.style.display = 'none';
    return;
  }

  tempVec3.project(camera);
  const inView =
    tempVec3.z > -1 &&
    tempVec3.z < 1 &&
    tempVec3.x > -LABEL_VISIBILITY_MARGIN &&
    tempVec3.x < LABEL_VISIBILITY_MARGIN &&
    tempVec3.y > -LABEL_VISIBILITY_MARGIN &&
    tempVec3.y < LABEL_VISIBILITY_MARGIN;

  if (!inView) {
    root.style.display = 'none';
    return;
  }

  const screenX = (tempVec3.x * 0.5 + 0.5) * window.innerWidth;
  const screenY = (-tempVec3.y * 0.5 + 0.5) * window.innerHeight;
  const offset = getLabelOffset(state.definition);

  root.style.display = 'block';
  root.style.left = `${screenX}px`;
  root.style.top = `${screenY}px`;
  box.textContent = state.definition.label;
  box.style.left = `${offset.x}px`;
  box.style.top = `${offset.y}px`;

  const boxCenterX = offset.x + box.offsetWidth * 0.5;
  const boxCenterY = offset.y + box.offsetHeight * 0.5;

  const boxDiag = (box.offsetWidth + box.offsetHeight) * 0.5;
  const jointDiameter = Math.max(4, Math.min(16, boxDiag / 3));
  const gapPx = 4;
  const boxCenterLength = Math.max(0.001, Math.sqrt(boxCenterX * boxCenterX + boxCenterY * boxCenterY));
  const unitX = boxCenterX / boxCenterLength;
  const unitY = boxCenterY / boxCenterLength;
  const jointCenterX = boxCenterX - unitX * (jointDiameter * 0.5 + gapPx);
  const jointCenterY = boxCenterY - unitY * (jointDiameter * 0.5 + gapPx);

  joint.style.width = `${jointDiameter}px`;
  joint.style.height = `${jointDiameter}px`;
  joint.style.left = `${jointCenterX - jointDiameter * 0.5}px`;
  joint.style.top = `${jointCenterY - jointDiameter * 0.5}px`;

  const length = Math.sqrt(jointCenterX * jointCenterX + jointCenterY * jointCenterY);
  const angle = Math.atan2(jointCenterY, jointCenterX) * 180 / Math.PI;
  line.style.width = `${length}px`;
  line.style.transform = `rotate(${angle}deg)`;
}

function updateAllLabels() {
  for (const state of labelStates.values()) {
    updateLabelVisual(state);
  }
}

function updateButtonStates() {
  toggleCrosshairBtn.textContent = `Crosshair: ${showCrosshair ? 'On' : 'Off'}`;
  toggleCrosshairBtn.classList.toggle('active', showCrosshair);

  toggleLockSunBtn.textContent = `Lock Sun: ${lockOnSun ? 'On' : 'Off'}`;
  toggleLockSunBtn.classList.toggle('active', lockOnSun);

  toggleStarScalingBtn.textContent = `Star Scaling: ${starScaling ? 'On' : 'Off'}`;
  toggleStarScalingBtn.classList.toggle('active', !starScaling);

  toggleCinematicOrbitBtn.textContent = `Cinematic Orbit: ${cinematicOrbitEnabled ? 'On' : 'Off'}`;
  toggleCinematicOrbitBtn.classList.toggle('active', cinematicOrbitEnabled);

  if (animationEnabled) {
    togglePlayBtn.disabled = false;
    togglePlayBtn.textContent = playing ? 'Pause' : 'Play';
    togglePlayBtn.classList.toggle('active', !playing);
  } else {
    togglePlayBtn.disabled = true;
    togglePlayBtn.textContent = 'Static Frame';
    togglePlayBtn.classList.add('active');
  }

  let dominantAxis = 'y';
  let dominantSign = 1;
  for (const axis of ['x', 'y', 'z']) {
    if (orbitVector[axis] !== 0) {
      dominantAxis = axis;
      dominantSign = orbitVector[axis] >= 0 ? 1 : -1;
      break;
    }
  }
  const axisBaseAngle = dominantAxis === 'x' ? 300 : dominantAxis === 'y' ? 0 : 90;
  const orbitAngle = axisBaseAngle + (dominantSign < 0 ? 180 : 0);
  const brightness = 0.4 + cinematicOrbitSpeed * 0.6;
  orbitCompassArrow.style.transform = `rotate(${orbitAngle}deg) scaleX(${0.65 + cinematicOrbitSpeed * 0.55})`;
  orbitCompassArrow.style.opacity = `${brightness}`;
  orbitSpeedLabel.textContent = `Speed: ${cinematicOrbitSpeed.toFixed(2)}x`;
  orbitVectorSummary.textContent = `Active: ${describeOrbitVector()}`;
  for (const button of orbitVectorButtons) {
    const axis = button.dataset.axis;
    const sign = Number(button.dataset.sign);
    button.classList.toggle('is-active', orbitVector[axis] === sign);
  }
  statCamera.textContent = cinematicOrbitEnabled
    ? 'Mouse: orbit/pan/zoom · Orbit pod: active'
    : 'Mouse: left orbit · right pan · wheel zoom';

  if (toggleAllLabelsBtn) {
    toggleAllLabelsBtn.textContent = labelsHiddenGlobally ? 'Show All Labels' : 'Hide All Labels';
    toggleAllLabelsBtn.classList.toggle('is-active', labelsHiddenGlobally);
  }
}

const renderer = new THREE.WebGLRenderer({
  antialias: true,
  powerPreference: 'high-performance',
  alpha: false
});
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.5));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setClearColor(0x070b16, 1.0);
viewer.appendChild(renderer.domElement);
renderer.domElement.addEventListener('contextmenu', (event) => event.preventDefault());

const scene = new THREE.Scene();
scene.fog = new THREE.FogExp2(0x070b16, 0.0009);

const camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.1, 200000);
camera.position.set(0, 40, 140);

const controls = new TrackballControls(camera, renderer.domElement);
controls.noRotate = false;
controls.noZoom = false;
controls.noPan = false;
controls.staticMoving = true;
controls.dynamicDampingFactor = 0.08;
controls.rotateSpeed = 3.2;
controls.zoomSpeed = 2.8;
controls.panSpeed = 1.6;
controls.target.set(0, 0, 0);

const geometry = new THREE.BufferGeometry();
const material = new THREE.ShaderMaterial({
  transparent: true,
  depthWrite: false,
  vertexColors: true,
  blending: THREE.AdditiveBlending,
  uniforms: {
    uPixelRatio: { value: renderer.getPixelRatio() },
    uStarScaling: { value: 1.0 }
  },
  vertexShader: `
    attribute float aSize;
    varying vec3 vColor;
    varying float vSize;
    uniform float uPixelRatio;
    uniform float uStarScaling;

    void main() {
      vColor = color;
      vSize = aSize;
      vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
      gl_Position = projectionMatrix * mvPosition;

      float dist = max(1.0, -mvPosition.z);
      float scaleFactor = mix(1.0, 190.0 / dist, uStarScaling);
      gl_PointSize = clamp(aSize * uPixelRatio * scaleFactor, 1.0, 20.0);
    }
  `,
  fragmentShader: `
    varying vec3 vColor;
    varying float vSize;

    void main() {
      vec2 c = gl_PointCoord - vec2(0.5);
      float d = dot(c, c);
      if (d > 0.25) discard;
      // Blend between soft glow (small/faint stars) and hard crisp disc
      // (bright/large stars that come from gaia_new.csv).
      float sharpness = smoothstep(1.2, 1.4, vSize);
      float softAlpha  = smoothstep(0.25, 0.0, d);
      float sharpAlpha = smoothstep(0.25, 0.18, d) * 0.97;
      float alpha = mix(softAlpha, sharpAlpha, sharpness);
      gl_FragColor = vec4(vColor, alpha);
    }
  `
});


function createCrosshair3D() {
  const crosshairMaterial = new THREE.LineBasicMaterial({ color: 0x8bb7ff, transparent: true, opacity: 0.85 });
  const crosshairPoints = [
    new THREE.Vector3(-5, 0, 0), new THREE.Vector3(5, 0, 0),
    new THREE.Vector3(0, -5, 0), new THREE.Vector3(0, 5, 0),
    new THREE.Vector3(0, 0, -5), new THREE.Vector3(0, 0, 5)
  ];
  const crosshairGeometry = new THREE.BufferGeometry().setFromPoints(crosshairPoints);
  crosshairObject = new THREE.LineSegments(crosshairGeometry, crosshairMaterial);
  scene.add(crosshairObject);
}

function fitCamera() {
  if (!frameFloatArrays.length || !frameFloatArrays[0].length) {
    setError('scene.json contains no frame position data');
    return;
  }

  const frame = frameFloatArrays[0];
  const box = new THREE.Box3();

  for (let i = 0; i < frame.length; i += 3) {
    tempVec3.set(frame[i], frame[i + 1], frame[i + 2]);
    box.expandByPoint(tempVec3);
  }

  const size = box.getSize(new THREE.Vector3());
  const maxDim = Math.max(size.x, size.y, size.z, 1);

  const dataCenter = box.getCenter(new THREE.Vector3());
  controls.target.copy(dataCenter);
  camera.position.copy(dataCenter.clone().add(new THREE.Vector3(maxDim * 0.72, maxDim * 0.32, maxDim * 1.08)));
  controls.update();
}

function updateFrame(frameFloat) {
  if (!masterPositions || frameCount === 0) {
    return;
  }

  const anchor = baseFramePositions || frameFloatArrays[0];
  if (frameCount === 1) {
    masterPositions.set(anchor);
    statFrame.textContent = 'Frame: 1/1';
    return;
  }

  const maxBaseIndex = frameCount - 1;
  const wrapped = ((frameFloat % maxBaseIndex) + maxBaseIndex) % maxBaseIndex;
  const i0 = Math.floor(wrapped);
  const t = wrapped - i0;
  const i1 = Math.min(i0 + 1, frameCount - 1);

  const a = frameFloatArrays[i0];
  const b = frameFloatArrays[i1];
  for (let i = 0; i < masterPositions.length; i += 1) {
    const interpolated = a[i] + (b[i] - a[i]) * t;
    masterPositions[i] = anchor[i] + (interpolated - anchor[i]) * MOTION_EXAGGERATION;
  }

  statFrame.textContent = `Frame: ${i0 + 1}/${frameCount}`;
}

function rebuildVisibleBuffers() {
  if (!masterPositions || !points) {
    return;
  }

  camera.updateMatrixWorld();
  projScreenMatrix.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
  frustum.setFromProjectionMatrix(projScreenMatrix);

  const cameraPos = camera.position;
  let visibleCount = 0;

  for (let i = 0; i < starMeta.length; i += 1) {
    const x = masterPositions[i * 3 + 0];
    const y = masterPositions[i * 3 + 1];
    const z = masterPositions[i * 3 + 2];

    tempVec3.set(x, y, z);
    const distSq = cameraPos.distanceToSquared(tempVec3);
    const star = starMeta[i];
    const keepByDistance = starScaling ? (star.isSun || distSq < 50000 * 50000) : true;
    const keepByBrightness = starScaling ? (star.isSun || Number(star.mag ?? 99) < 11.5 || distSq < 1500 * 1500) : true;
    const keepByFrustum = starScaling ? (star.isSun || frustum.containsPoint(tempVec3)) : true;

    if (!(keepByDistance && keepByBrightness && keepByFrustum)) {
      continue;
    }

    visiblePositions[visibleCount * 3 + 0] = x;
    visiblePositions[visibleCount * 3 + 1] = y;
    visiblePositions[visibleCount * 3 + 2] = z;
    visibleColors[visibleCount * 3 + 0] = masterColors[i * 3 + 0];
    visibleColors[visibleCount * 3 + 1] = masterColors[i * 3 + 1];
    visibleColors[visibleCount * 3 + 2] = masterColors[i * 3 + 2];
    visibleSizes[visibleCount] = masterSizes[i];
    visibleMap[visibleCount] = i;
    visibleCount += 1;
  }

  geometry.setDrawRange(0, visibleCount);
  geometry.attributes.position.needsUpdate = true;
  geometry.attributes.color.needsUpdate = true;
  geometry.attributes.aSize.needsUpdate = true;
  statVisible.textContent = `Visible stars: ${visibleCount}/${starMeta.length}`;
}

function updateCrosshair3D() {
  if (!crosshairObject) {
    return;
  }

  crosshairObject.visible = showCrosshair;
  crosshairObject.position.copy(controls.target);

  const dist = camera.position.distanceTo(controls.target);
  const scale = Math.max(0.3, dist * 0.03);
  crosshairObject.scale.setScalar(scale);
}

function updateSunLock() {
  if (!lockOnSun || sunIndex < 0 || !masterPositions) {
    return;
  }

  tempVec3.set(
    masterPositions[sunIndex * 3 + 0],
    masterPositions[sunIndex * 3 + 1],
    masterPositions[sunIndex * 3 + 2]
  );
  controls.target.lerp(tempVec3, 0.18);
}

function applyCinematicOrbit(deltaSeconds) {
  if (!cinematicOrbitEnabled) {
    return;
  }

  tempVec3.copy(camera.position).sub(controls.target);
  if (tempVec3.lengthSq() < 1e-9) {
    return;
  }

  const step = cinematicOrbitSpeed * deltaSeconds;
  for (const axis of ['x', 'y', 'z']) {
    if (orbitVector[axis] !== 0) {
      tempVec3.applyAxisAngle(ORBIT_AXIS_MAP[axis], step * orbitVector[axis]);
    }
  }
  camera.position.copy(controls.target).add(tempVec3);
  camera.lookAt(controls.target);
}

function renderScene() {
  if (animationFaulted) {
    return;
  }

  try {
    controls.update();
    updateCrosshair3D();
    rebuildVisibleBuffers();
    updateAllLabels();
    renderer.render(scene, camera);
  } catch (error) {
    animationFaulted = true;
    console.error('Fatal render error:', error);
    setError(error instanceof Error ? error.message : String(error));
  }
}

function shouldRunContinuousLoop() {
  return !animationFaulted && ((animationEnabled && playing) || cinematicOrbitEnabled || interactionActive);
}

function requestRender() {
  if (continuousLoopRunning || singleRenderQueued || animationFaulted) {
    return;
  }

  singleRenderQueued = true;
  requestAnimationFrame(() => {
    singleRenderQueued = false;
    if (continuousLoopRunning || animationFaulted) {
      return;
    }
    renderScene();
  });
}

function animationTick(timestamp) {
  if (animationFaulted) {
    continuousLoopRunning = false;
    return;
  }

  const deltaSeconds = lastLoopTimestamp > 0 ? Math.min((timestamp - lastLoopTimestamp) / 1000, 0.1) : 1 / 60;
  lastLoopTimestamp = timestamp;

  if (animationEnabled && playing) {
    currentFrameFloat += FRAME_ADVANCE_PER_SECOND * deltaSeconds;
    updateFrame(currentFrameFloat);
  }

  updateSunLock();
  applyCinematicOrbit(deltaSeconds);

  renderScene();

  if (shouldRunContinuousLoop()) {
    requestAnimationFrame(animationTick);
    return;
  }

  continuousLoopRunning = false;
  lastLoopTimestamp = 0;
}

function ensureContinuousLoop() {
  if (continuousLoopRunning || !shouldRunContinuousLoop()) {
    return;
  }

  continuousLoopRunning = true;
  lastLoopTimestamp = 0;
  requestAnimationFrame(animationTick);
}

function refreshMotionMode() {
  animationEnabled = frameCount > 1;
  if (!animationEnabled) {
    playing = false;
  }

  updateButtonStates();

  if (shouldRunContinuousLoop()) {
    ensureContinuousLoop();
  } else {
    requestRender();
  }
}

function handleControlsChange() {
  if (!continuousLoopRunning) {
    requestRender();
  }
}

controls.addEventListener('change', handleControlsChange);
controls.addEventListener('start', () => {
  interactionActive = true;
  ensureContinuousLoop();
});
controls.addEventListener('end', () => {
  interactionActive = false;
  if (!shouldRunContinuousLoop()) {
    requestRender();
  }
});

function initializeSceneFromData(data) {
  starMeta = data.stars;
  frameCount = Number(data.frameCount ?? data.frames.length ?? 0);
  yearsPerFrame = Number(data.yearsPerFrame ?? 0);
  if (frameCount !== data.frames.length) {
    frameCount = data.frames.length;
  }

  console.log(`Processing ${data.frames.length} frames for ${starMeta.length} stars (${starMeta.length * 3} expected values per frame)...`);
  logMemory('before frame conversion');

  frameFloatArrays = data.frames.map((frame, index) => {
    const arr = new Float32Array(frame);
    const expectedLength = starMeta.length * 3;
    if (index < 3 || index === data.frames.length - 1) {
      console.log(`Frame ${index}: got ${arr.length} values, expected ${expectedLength}`);
    }
    if (arr.length !== expectedLength) {
      throw new Error(`frame ${index} has ${arr.length} values, expected ${expectedLength}`);
    }
    return arr;
  });
  logMemory('after frame conversion');

  if (frameFloatArrays.length === 0) {
    throw new Error('scene.json contains zero frames');
  }

  masterPositions = new Float32Array(frameFloatArrays[0].length);
  baseFramePositions = new Float32Array(frameFloatArrays[0]);
  masterColors = new Float32Array(starMeta.length * 3);
  masterSizes = new Float32Array(starMeta.length);
  visiblePositions = new Float32Array(frameFloatArrays[0].length);
  visibleColors = new Float32Array(starMeta.length * 3);
  visibleSizes = new Float32Array(starMeta.length);
  visibleMap = new Array(starMeta.length);
  masterPositions.set(baseFramePositions);

  sunIndex = -1;
  for (let i = 0; i < starMeta.length; i += 1) {
    const star = starMeta[i];
    const [r, g, b] = bpRpToColor(star.ci, star.isSun);
    masterColors[i * 3 + 0] = r * (2 / 3);
    masterColors[i * 3 + 1] = g * (2 / 3);
    masterColors[i * 3 + 2] = b * (2 / 3);
    masterSizes[i] = magnitudeToSize(star.mag, star.isSun);
    if (star.isSun) {
      sunIndex = i;
    }
  }

  geometry.setAttribute('position', new THREE.BufferAttribute(visiblePositions, 3));
  geometry.setAttribute('color', new THREE.BufferAttribute(visibleColors, 3));
  geometry.setAttribute('aSize', new THREE.BufferAttribute(visibleSizes, 1));

  if (!points) {
    points = new THREE.Points(geometry, material);
    points.frustumCulled = false;
    scene.add(points);
  }
  statGas.textContent = 'Gas clouds: 0';

  currentFrameFloat = 0;
  updateFrame(0);
  for (const definition of EXTRA_LABEL_DEFINITIONS) {
    resolveLabelStateTarget(buildExtraLabelState(definition));
  }
  buildLabelList();
  fitCamera();

  statStars.textContent = `Stars: ${starMeta.length}`;
  statFrame.textContent = `Frame: 1/${Math.max(frameCount, 1)}`;
  statYears.textContent = `Years/frame: ${yearsPerFrame.toLocaleString()}`;
  clearError();
  refreshMotionMode();
}

async function loadSceneData() {
  try {
    console.time('scene-load');
    console.log('Starting to load scene.json...');
    console.log('Current location:', window.location.href);

    const response = await fetch('scene.json', { cache: 'no-cache' });
    console.log('Fetch response status:', response.status);
    console.log('Fetch response ok:', response.ok);

    if (!response.ok) {
      throw new Error(`HTTP ${response.status} while loading scene.json`);
    }

    const contentLength = Number(response.headers.get('content-length') ?? 0);
    if (contentLength > 0) {
      console.log('scene.json content-length:', formatBytes(contentLength));
      if (contentLength > MAX_SCENE_BYTES) {
        console.warn(`scene.json is ${formatBytes(contentLength)} which is large. Parsing may be slow or run out of memory. Regenerate with fewer frames if needed.`);
      }
    } else {
      console.log('scene.json content-length header unavailable');
    }

    logMemory('before json parse');
    const data = await response.json();
    logMemory('after json parse');

    if (!data || !Array.isArray(data.stars) || !Array.isArray(data.frames)) {
      throw new Error('scene.json has an unexpected structure');
    }

    console.log(`Found ${data.stars.length} stars and ${data.frames.length} frames`);
    initializeSceneFromData(data);
    console.log('Scene loaded successfully!');
    console.timeEnd('scene-load');
  } catch (error) {
    console.timeEnd('scene-load');
    console.error('Error loading scene:', error);
    setError(error instanceof Error ? error.message : String(error));

    const fallbackStars = [
      { id: 1, name: 'Sun', mag: -1.5, ci: 0.6, isSun: true },
      { id: 2, name: 'Fallback A', mag: 1.0, ci: 0.8, isSun: false },
      { id: 3, name: 'Fallback B', mag: 2.0, ci: 0.7, isSun: false },
      { id: 4, name: 'Fallback C', mag: 3.0, ci: 0.5, isSun: false }
    ];
    const fallbackFrames = [];
    const fallbackFrameCount = 10;
    for (let frameIndex = 0; frameIndex < fallbackFrameCount; frameIndex += 1) {
      const positions = new Float32Array(fallbackStars.length * 3);
      for (let i = 0; i < fallbackStars.length; i += 1) {
        const angle = (frameIndex / fallbackFrameCount) * Math.PI * 2 + i * Math.PI / 2;
        const radius = 10 + i * 5;
        positions[i * 3 + 0] = Math.cos(angle) * radius;
        positions[i * 3 + 1] = Math.sin(angle) * radius * 0.3;
        positions[i * 3 + 2] = Math.sin(angle * 2) * radius * 0.1;
      }
      fallbackFrames.push(Array.from(positions));
    }

    initializeSceneFromData({
      stars: fallbackStars,
      gasPoints: [],
      gasDensityMax: 0,
      frames: fallbackFrames,
      frameCount: fallbackFrameCount,
      yearsPerFrame: 1000
    });
    statStars.textContent = `Stars: ${starMeta.length} (test data)`;
    statError.textContent = 'Using test data - scene.json not accessible';
  }
}

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
  material.uniforms.uPixelRatio.value = renderer.getPixelRatio();
  if (typeof controls.handleResize === 'function') {
    controls.handleResize();
  }
  requestRender();
});

toggleCrosshairBtn.addEventListener('click', () => {
  showCrosshair = !showCrosshair;
  crosshair.style.display = showCrosshair ? 'block' : 'none';
  updateButtonStates();
  requestRender();
});

toggleLockSunBtn.addEventListener('click', () => {
  lockOnSun = !lockOnSun;
  if (lockOnSun && sunIndex >= 0 && masterPositions) {
    tempVec3B.set(
      masterPositions[sunIndex * 3 + 0],
      masterPositions[sunIndex * 3 + 1],
      masterPositions[sunIndex * 3 + 2]
    );
    controls.target.copy(tempVec3B);
    controls.update();
  }
  updateButtonStates();
  requestRender();
});

if (toggleAllLabelsBtn) {
  toggleAllLabelsBtn.addEventListener('click', () => {
    labelsHiddenGlobally = !labelsHiddenGlobally;
    updateButtonStates();
    requestRender();
  });
}

if (labelSearchInput) {
  labelSearchInput.addEventListener('input', () => {
    labelFilterQuery = labelSearchInput.value ?? '';
    updateLabelListUI();
  });

  labelSearchInput.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      labelSearchInput.value = '';
      labelFilterQuery = '';
      updateLabelListUI();
      labelSearchInput.blur();
    }
  });
}

toggleStarScalingBtn.addEventListener('click', () => {
  starScaling = !starScaling;
  material.uniforms.uStarScaling.value = starScaling ? 1.0 : 0.0;
  updateButtonStates();
  requestRender();
});

toggleCinematicOrbitBtn.addEventListener('click', () => {
  cinematicOrbitEnabled = !cinematicOrbitEnabled;
  refreshMotionMode();
});

togglePlayBtn.addEventListener('click', () => {
  if (!animationEnabled) {
    return;
  }
  playing = !playing;
  refreshMotionMode();
});

orbitSpeedInput.addEventListener('input', () => {
  cinematicOrbitSpeed = Number(orbitSpeedInput.value) / 100;
  updateButtonStates();
  if (cinematicOrbitEnabled) {
    ensureContinuousLoop();
  }
});

orbitVectorGrid.addEventListener('click', (event) => {
  const target = event.target;
  if (!(target instanceof HTMLElement)) {
    return;
  }
  const axis = target.dataset.axis;
  const sign = Number(target.dataset.sign);
  if (!axis || !Number.isFinite(sign)) {
    return;
  }

  setOrbitAxis(axis, sign);
  updateButtonStates();
  if (cinematicOrbitEnabled) {
    ensureContinuousLoop();
  } else {
    requestRender();
  }
});

orbitVectorResetBtn.addEventListener('click', () => {
  orbitVector = { x: 0, y: 1, z: 1 };
  orbitActivationOrder = ['y', 'z'];
  updateButtonStates();
  if (cinematicOrbitEnabled) {
    ensureContinuousLoop();
  } else {
    requestRender();
  }
});

window.addEventListener('keydown', (event) => {
  if (event.repeat) {
    return;
  }
  if (event.key === 'h' || event.key === 'H') {
    toggleInterfaceVisibility(!interfaceHidden);
  }
});

document.querySelectorAll('.panel-collapse-btn').forEach((btn) => {
  btn.addEventListener('click', () => {
    const panel = document.getElementById(btn.dataset.target ?? '');
    if (!panel) {
      return;
    }
    const isCollapsed = panel.classList.toggle('is-collapsed');
    btn.textContent = isCollapsed ? '+' : '−';
    btn.title = isCollapsed ? 'Expand' : 'Collapse';
  });
});

crosshair.style.display = showCrosshair ? 'block' : 'none';
buildLabelList();
toggleInterfaceVisibility(false);
updateButtonStates();
createCrosshair3D();
loadSceneData();
