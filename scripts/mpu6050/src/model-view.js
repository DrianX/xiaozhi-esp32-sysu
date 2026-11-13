import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { MeshoptDecoder } from 'three/addons/libs/meshopt_decoder.module.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { SERIES_COLORS } from './palette.js';

const DEG2RAD = Math.PI / 180;
const STAGE_Y = -1.55;
const DEFAULT_CAMERA = new THREE.Vector3(4.4, 3.9, 5.7);
const DEFAULT_TARGET = new THREE.Vector3(0, -0.3, 0);
const MODEL_MAX_EXTENT = 2.1;
const AXIS_COLORS = { x: 0x5aa9ff, y: 0xff8b52, z: 0x2bd6a1 };
const REDUCED_MOTION = typeof window !== 'undefined'
  && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches;

function makeTextSprite(text, { color = '#d9e6f4', fontSize = 52, scale = [0.34, 0.17], chip = false } = {}) {
  const canvas = document.createElement('canvas');
  canvas.width = 256;
  canvas.height = 128;
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.font = `600 ${fontSize}px "Cascadia Mono", Consolas, monospace`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  if (chip) {
    const width = ctx.measureText(text).width;
    const halfW = width / 2 + 22;
    const halfH = fontSize * 0.66;
    const x = canvas.width / 2 - halfW;
    const y = canvas.height / 2 - halfH;
    const w = halfW * 2;
    const h = halfH * 2;
    const r = 18;
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + w - r, y);
    ctx.quadraticCurveTo(x + w, y, x + w, y + r);
    ctx.lineTo(x + w, y + h - r);
    ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    ctx.lineTo(x + r, y + h);
    ctx.quadraticCurveTo(x, y + h, x, y + h - r);
    ctx.lineTo(x, y + r);
    ctx.quadraticCurveTo(x, y, x + r, y);
    ctx.closePath();
    ctx.fillStyle = 'rgba(5, 10, 16, 0.76)';
    ctx.fill();
    ctx.strokeStyle = 'rgba(151, 178, 207, 0.28)';
    ctx.lineWidth = 2;
    ctx.stroke();
  }
  ctx.fillStyle = color;
  ctx.fillText(text, canvas.width / 2, canvas.height / 2 + 2);
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.minFilter = THREE.LinearFilter;
  texture.generateMipmaps = false;
  const material = new THREE.SpriteMaterial({
    map: texture,
    transparent: true,
    depthWrite: false,
    toneMapped: false,
  });
  const sprite = new THREE.Sprite(material);
  sprite.scale.set(scale[0], scale[1], 1);
  sprite.renderOrder = 3;
  return sprite;
}

function makeArrow(direction, color, length, radius = 0.014) {
  const group = new THREE.Group();
  const dir = direction.clone().normalize();
  const material = new THREE.MeshBasicMaterial({
    color,
    transparent: true,
    opacity: 1,
    toneMapped: false,
  });
  const shaftLength = Math.max(0.1, length - 0.17);
  const shaft = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, shaftLength, 8), material);
  shaft.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir);
  shaft.position.copy(dir).multiplyScalar(shaftLength / 2);
  const head = new THREE.Mesh(new THREE.ConeGeometry(radius * 4.6, 0.17, 12), material);
  head.quaternion.copy(shaft.quaternion);
  head.position.copy(dir).multiplyScalar(length - 0.085);
  group.add(shaft, head);
  return group;
}

export class AttitudeView {
  constructor(container) {
    this.container = container;
    this.frozen = false;
    this.hasAttitude = false;
    this.calibration = { invertRoll: false, invertPitch: false, invertYaw: false };
    this.lastAngles = { roll: 0, pitch: 0, yaw: 0 };
    this.displayYaw = 0;
    this.targetQuaternion = new THREE.Quaternion();
    this.cameraTween = null;
    this._disposed = false;

    this._initRenderer();
    this._initScene();
    this._buildStage();
    // Render a lightweight board immediately. The detailed GLB replaces it in
    // the background, so the viewer never blocks behind a loading overlay.
    this._attachPlaceholder();
    this._initControls();
    this._observeResize();
    this._animate = this._animate.bind(this);
    this.renderer.setAnimationLoop(this._animate);
  }

  _initRenderer() {
    this.renderer = new THREE.WebGLRenderer({
      antialias: true,
      alpha: true,
      powerPreference: 'high-performance',
    });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.08;
    this.renderer.setClearColor(0x000000, 0);
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.domElement.setAttribute('aria-hidden', 'true');
    this.container.appendChild(this.renderer.domElement);
  }

  _initScene() {
    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(35, 1, 0.1, 80);
    this.camera.position.copy(DEFAULT_CAMERA);

    const pmrem = new THREE.PMREMGenerator(this.renderer);
    const room = new RoomEnvironment();
    this.envTexture = pmrem.fromScene(room, 0.035).texture;
    this.scene.environment = this.envTexture;
    room.dispose?.();
    pmrem.dispose();

    this.hemiLight = new THREE.HemisphereLight(0xb9d7ff, 0x060a0e, 1.1);
    this.scene.add(this.hemiLight);

    this.keyLight = new THREE.DirectionalLight(0xf4f8ff, 2.8);
    this.keyLight.position.set(3.5, 5.5, 3.0);
    this.keyLight.castShadow = true;
    this.keyLight.shadow.mapSize.set(2048, 2048);
    this.keyLight.shadow.camera.near = 0.5;
    this.keyLight.shadow.camera.far = 20;
    this.keyLight.shadow.camera.left = -4;
    this.keyLight.shadow.camera.right = 4;
    this.keyLight.shadow.camera.top = 4;
    this.keyLight.shadow.camera.bottom = -4;
    this.keyLight.shadow.bias = -0.0004;
    this.keyLight.shadow.normalBias = 0.02;
    this.scene.add(this.keyLight);
    this.scene.add(this.keyLight.target);

    this.fillLight = new THREE.DirectionalLight(0x6fa9ff, 0.75);
    this.fillLight.position.set(-4, 2.0, -2.5);
    this.scene.add(this.fillLight);

    this.rimLight = new THREE.DirectionalLight(0x65f2d5, 0.5);
    this.rimLight.position.set(0, 2.5, -4);
    this.scene.add(this.rimLight);

    this.attitudeGroup = new THREE.Group();
    this.attitudeGroup.position.set(0, 0, 0);
    this.scene.add(this.attitudeGroup);

    this.modelPivot = new THREE.Group();
    this.attitudeGroup.add(this.modelPivot);

    this._buildAxisTriad();
  }

  _buildStage() {
    const platformMaterial = new THREE.MeshStandardMaterial({
      color: 0x081016,
      metalness: 0.32,
      roughness: 0.66,
      transparent: true,
      opacity: 0.97,
    });
    const platform = new THREE.Mesh(new THREE.CircleGeometry(2.82, 160), platformMaterial);
    platform.rotation.x = -Math.PI / 2;
    platform.position.y = STAGE_Y;
    platform.receiveShadow = true;
    this.scene.add(platform);

    // Concentric guide rings.
    for (const [radius, opacity] of [[2.68, 0.34], [2.02, 0.16], [1.16, 0.11]]) {
      const points = [];
      for (let i = 0; i < 192; i++) {
        const angle = (i / 192) * Math.PI * 2;
        points.push(new THREE.Vector3(Math.cos(angle) * radius, STAGE_Y + 0.004, Math.sin(angle) * radius));
      }
      const geometry = new THREE.BufferGeometry().setFromPoints(points);
      const ring = new THREE.LineLoop(geometry, new THREE.LineBasicMaterial({
        color: 0x3d5b72,
        transparent: true,
        opacity,
        toneMapped: false,
      }));
      this.scene.add(ring);
    }

    // Degree ticks.
    const tickPositions = [];
    for (let deg = 0; deg < 360; deg += 5) {
      const major = deg % 30 === 0;
      const angle = -deg * DEG2RAD;
      const inner = major ? 2.4 : 2.5;
      const outer = 2.68;
      tickPositions.push(
        Math.cos(angle) * inner, STAGE_Y + 0.007, Math.sin(angle) * inner,
        Math.cos(angle) * outer, STAGE_Y + 0.007, Math.sin(angle) * outer,
      );
    }
    const tickGeometry = new THREE.BufferGeometry();
    tickGeometry.setAttribute('position', new THREE.Float32BufferAttribute(tickPositions, 3));
    const ticks = new THREE.LineSegments(tickGeometry, new THREE.LineBasicMaterial({
      color: 0x50728b,
      transparent: true,
      opacity: 0.48,
      toneMapped: false,
    }));
    this.scene.add(ticks);

    // Cardinal heading labels. Positive yaw rotates +X toward -Z.
    const cardinals = [
      { text: '0°', pos: [2.92, 0] },
      { text: '90°', pos: [0, -2.92] },
      { text: '180°', pos: [-2.92, 0] },
      { text: '270°', pos: [0, 2.92] },
    ];
    for (const item of cardinals) {
      const sprite = makeTextSprite(item.text, {
        color: item.text === '0°' ? '#b9d8ff' : '#8497a9',
        fontSize: 46,
        scale: [0.38, 0.19],
      });
      sprite.position.set(item.pos[0], STAGE_Y + 0.12, item.pos[1]);
      this.scene.add(sprite);
    }

    // A faint plumb line from the board pivot to the compass plane.
    const plumbGeometry = new THREE.BufferGeometry().setFromPoints([
      new THREE.Vector3(0, 0, 0),
      new THREE.Vector3(0, STAGE_Y, 0),
    ]);
    const plumb = new THREE.Line(plumbGeometry, new THREE.LineBasicMaterial({
      color: 0x44687f,
      transparent: true,
      opacity: 0.16,
      toneMapped: false,
    }));
    this.scene.add(plumb);

    const centerRing = new THREE.LineLoop(
      new THREE.BufferGeometry().setFromPoints(
        Array.from({ length: 64 }, (_, i) => {
          const angle = (i / 64) * Math.PI * 2;
          return new THREE.Vector3(Math.cos(angle) * 0.11, STAGE_Y + 0.006, Math.sin(angle) * 0.11);
        }),
      ),
      new THREE.LineBasicMaterial({ color: 0x6c8ca3, transparent: true, opacity: 0.4, toneMapped: false }),
    );
    this.scene.add(centerRing);
  }

  _buildAxisTriad() {
    this.axisTriad = new THREE.Group();
    this.axisTriad.position.set(0, 0.27, 0);
    this.attitudeGroup.add(this.axisTriad);

    const axes = [
      { dir: new THREE.Vector3(1, 0, 0), color: AXIS_COLORS.x, label: 'X', length: 1.34 },
      { dir: new THREE.Vector3(0, 0, -1), color: AXIS_COLORS.y, label: 'Y', length: 1.34 },
      { dir: new THREE.Vector3(0, 1, 0), color: AXIS_COLORS.z, label: 'Z', length: 0.98 },
    ];

    for (const axis of axes) {
      const arrow = makeArrow(axis.dir, axis.color, axis.length, axis.dir.y ? 0.01 : 0.012);
      this.axisTriad.add(arrow);
      const label = makeTextSprite(axis.label, {
        color: '#eef6ff',
        fontSize: 54,
        scale: [0.24, 0.12],
        chip: true,
      });
      label.position.copy(axis.dir).multiplyScalar(axis.length + 0.19);
      this.axisTriad.add(label);
    }

    const origin = new THREE.Mesh(
      new THREE.SphereGeometry(0.026, 14, 10),
      new THREE.MeshBasicMaterial({ color: 0xdbeaff, toneMapped: false }),
    );
    this.axisTriad.add(origin);
  }

  _initControls() {
    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.075;
    this.controls.enablePan = false;
    this.controls.minDistance = 3.2;
    this.controls.maxDistance = 10.5;
    this.controls.minPolarAngle = 0.18;
    this.controls.maxPolarAngle = Math.PI / 2 - 0.035;
    this.controls.rotateSpeed = 0.7;
    this.controls.zoomSpeed = 0.8;
    this.controls.target.copy(DEFAULT_TARGET);
    this.controls.update();
    this._clock = new THREE.Clock();
  }

  _observeResize() {
    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(this.container);
    this.resize();
  }

  resize() {
    const width = Math.max(1, this.container.clientWidth);
    const height = Math.max(1, this.container.clientHeight);
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(width, height, true);
  }

  loadModel(url, { onProgress, onReady, onError } = {}) {
    const loader = new GLTFLoader();
    loader.setMeshoptDecoder(MeshoptDecoder);
    loader.load(
      url,
      gltf => {
        try {
          this._attachModel(gltf.scene);
          onReady?.({ fallback: false });
        } catch (error) {
          this._attachPlaceholder();
          onError?.(error);
          onReady?.({ fallback: true });
        }
      },
      event => {
        if (event?.lengthComputable && event.total > 0) {
          onProgress?.(Math.min(1, event.loaded / event.total));
        } else if (event?.loaded) {
          onProgress?.(Math.min(0.95, event.loaded / 1_600_000));
        }
      },
      error => {
        this._attachPlaceholder();
        onError?.(error);
        onReady?.({ fallback: true });
      },
    );
  }

  _attachModel(model) {
    let meshCount = 0;
    model.traverse(child => {
      if (!child.isMesh) return;
      const positions = child.geometry?.attributes?.position;
      if (!positions || positions.count < 3) return;
      meshCount++;
      // A single large mesh with a scaled/quantized parent can be culled
      // incorrectly when rotated. The scene is small, so keep it visible.
      child.frustumCulled = false;
      child.castShadow = true;
      child.receiveShadow = true;
      const materials = Array.isArray(child.material) ? child.material : [child.material];
      for (const material of materials) {
        if (!material) continue;
        material.envMapIntensity = 0.9;
        material.needsUpdate = true;
      }
    });

    if (!meshCount) throw new Error('GLB 模型没有可渲染网格');

    const box = new THREE.Box3().setFromObject(model);
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());
    if (![size.x, size.y, size.z].every(Number.isFinite)
        || size.x <= 0 || size.z <= 0) {
      throw new Error('GLB 模型包围盒无效');
    }

    const horizontalExtent = Math.max(size.x, size.z);
    const scale = MODEL_MAX_EXTENT / horizontalExtent;
    model.scale.setScalar(scale);
    model.position.set(-center.x * scale, -center.y * scale, -center.z * scale);
    model.updateMatrixWorld(true);

    // Only remove the lightweight preview after the detailed model has been
    // validated, so a broken GLB can never leave the viewer empty.
    this.modelPivot.clear();
    this.modelPivot.add(model);
    this.modelRoot = model;
    this.usingFallback = false;
  }

  _attachPlaceholder() {
    this.modelPivot.clear();
    const group = new THREE.Group();
    const pcbMat = new THREE.MeshStandardMaterial({ color: 0x12345b, roughness: 0.6, metalness: 0.12 });
    const darkMat = new THREE.MeshStandardMaterial({ color: 0x1a1f27, roughness: 0.4, metalness: 0.35 });
    const metalMat = new THREE.MeshStandardMaterial({ color: 0xaab4bd, roughness: 0.3, metalness: 0.85 });
    const goldMat = new THREE.MeshStandardMaterial({ color: 0xc69a45, roughness: 0.35, metalness: 0.8 });

    const board = new THREE.Mesh(new THREE.BoxGeometry(2.1, 0.12, 2.1), pcbMat);
    board.castShadow = true;
    board.receiveShadow = true;
    group.add(board);

    const module = new THREE.Mesh(new THREE.BoxGeometry(0.8, 0.18, 0.5), darkMat);
    module.position.set(-0.25, 0.15, -0.2);
    module.castShadow = true;
    group.add(module);

    const shield = new THREE.Mesh(new THREE.BoxGeometry(0.54, 0.035, 0.34), metalMat);
    shield.position.set(-0.25, 0.257, -0.2);
    shield.castShadow = true;
    group.add(shield);

    // Large connectors along the front edge echo the real board's layout.
    for (const x of [-0.73, -0.25, 0.25, 0.73]) {
      const connector = new THREE.Mesh(new THREE.BoxGeometry(0.38, 0.15, 0.23), darkMat);
      connector.position.set(x, 0.135, 0.58);
      connector.castShadow = true;
      group.add(connector);
    }

    const cameraCan = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.22, 0.34), metalMat);
    cameraCan.position.set(0.58, 0.17, -0.3);
    cameraCan.castShadow = true;
    group.add(cameraCan);

    const usb = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.16, 0.23), metalMat);
    usb.position.set(0, 0.1, -1.0);
    usb.castShadow = true;
    group.add(usb);

    for (let i = 0; i < 8; i++) {
      const pin = new THREE.Mesh(new THREE.BoxGeometry(0.065, 0.04, 0.18), metalMat);
      pin.position.set(-0.77 + i * 0.22, 0.075, 1.03);
      group.add(pin);
    }

    for (const [x, z] of [[-0.88, -0.88], [0.88, -0.88], [-0.88, 0.88], [0.88, 0.88]]) {
      const pad = new THREE.Mesh(new THREE.CylinderGeometry(0.075, 0.075, 0.02, 20), goldMat);
      pad.position.set(x, 0.07, z);
      group.add(pad);
    }

    const marker = new THREE.Mesh(
      new THREE.CylinderGeometry(0.035, 0.035, 0.015, 18),
      new THREE.MeshBasicMaterial({ color: SERIES_COLORS.z, toneMapped: false }),
    );
    marker.position.set(0.82, 0.067, 0.82);
    group.add(marker);

    group.traverse(child => {
      if (child.isMesh) child.frustumCulled = false;
    });

    this.modelPivot.add(group);
    this.modelRoot = group;
    this.usingFallback = true;
  }

  setCalibration(calibration) {
    this.calibration = {
      invertRoll: !!calibration.invertRoll,
      invertPitch: !!calibration.invertPitch,
      invertYaw: !!calibration.invertYaw,
    };
    if (this.hasAttitude) this.setAttitude(this.lastAngles, { forceTarget: true });
  }

  setFrozen(frozen) {
    this.frozen = !!frozen;
  }

  clearAttitude() {
    this.hasAttitude = false;
    this.lastAngles = { roll: 0, pitch: 0, yaw: 0 };
    this.displayYaw = 0;
    this.targetQuaternion.identity();
    this.attitudeGroup.quaternion.identity();
  }

  setAttitude(angles) {
    const roll = Number.isFinite(angles.roll) ? angles.roll : this.lastAngles.roll;
    const pitch = Number.isFinite(angles.pitch) ? angles.pitch : this.lastAngles.pitch;
    const yaw = Number.isFinite(angles.yaw) ? angles.yaw : this.lastAngles.yaw;
    this.lastAngles = { roll, pitch, yaw };

    const sign = this.calibration;
    const rollRad = (sign.invertRoll ? -roll : roll) * DEG2RAD;
    const pitchRad = (sign.invertPitch ? -pitch : pitch) * DEG2RAD;
    const yawRad = (sign.invertYaw ? -yaw : yaw) * DEG2RAD;

    const qYaw = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), yawRad);
    const qPitch = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 0, 1), pitchRad);
    const qRoll = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), rollRad);
    const target = new THREE.Quaternion().copy(qYaw).multiply(qPitch).multiply(qRoll);

    this.targetQuaternion.copy(target);
    const displayYaw = sign.invertYaw ? -yaw : yaw;
    const wrapped = ((displayYaw % 360) + 360) % 360;
    this.displayYaw = wrapped;

    if (!this.hasAttitude) {
      this.hasAttitude = true;
      this.attitudeGroup.quaternion.copy(target);
    }
  }

  getHeading() {
    return this.hasAttitude ? this.displayYaw : 0;
  }

  resetView() {
    this.cameraTween = {
      elapsed: 0,
      duration: REDUCED_MOTION ? 0.001 : 0.55,
      fromPos: this.camera.position.clone(),
      toPos: DEFAULT_CAMERA.clone(),
      fromTarget: this.controls.target.clone(),
      toTarget: DEFAULT_TARGET.clone(),
    };
  }

  _animate() {
    if (this._disposed) return;
    const dt = Math.min(this._clock?.getDelta?.() ?? 0.016, 0.08);

    if (this.hasAttitude && !this.frozen) {
      const alpha = REDUCED_MOTION ? 1 : 1 - Math.exp(-dt * 9.5);
      this.attitudeGroup.quaternion.slerp(this.targetQuaternion, alpha);
    }

    if (this.cameraTween) {
      this.cameraTween.elapsed += dt;
      const t = Math.min(1, this.cameraTween.elapsed / this.cameraTween.duration);
      const eased = 1 - Math.pow(1 - t, 3);
      this.camera.position.lerpVectors(this.cameraTween.fromPos, this.cameraTween.toPos, eased);
      this.controls.target.lerpVectors(this.cameraTween.fromTarget, this.cameraTween.toTarget, eased);
      if (t >= 1) this.cameraTween = null;
    }

    this.controls.update();
    this.renderer.render(this.scene, this.camera);
  }

  dispose() {
    this._disposed = true;
    this.renderer.setAnimationLoop(null);
    this.resizeObserver?.disconnect();
    this.controls?.dispose();
    this.scene?.traverse(object => {
      if (object.geometry) object.geometry.dispose();
      if (object.material) {
        const materials = Array.isArray(object.material) ? object.material : [object.material];
        for (const material of materials) {
          material.map?.dispose?.();
          material.dispose?.();
        }
      }
    });
    this.envTexture?.dispose?.();
    this.renderer?.dispose();
    this.renderer?.domElement?.remove();
  }
}
