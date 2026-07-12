<script setup lang="ts">
/**
 * Light WebGL particle field over the lyric cover (fullscreen-friendly).
 * No Three.js — raw WebGL2/WebGL1 points. Respects reduced-motion.
 */
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { isReducedMotion } from '../../api/motion';

const props = defineProps<{
  active: boolean;
  isPlaying?: boolean;
}>();

const canvasRef = ref<HTMLCanvasElement | null>(null);

const COUNT = 96;
let gl: WebGLRenderingContext | null = null;
let program: WebGLProgram | null = null;
let buf: WebGLBuffer | null = null;
let aPos = -1;
let uTime: WebGLUniformLocation | null = null;
let uPlaying: WebGLUniformLocation | null = null;
let raf = 0;
let timeOrigin = 0;
let alive = true;

const VS = `
attribute vec3 a_pos;
uniform float u_time;
uniform float u_playing;
varying float v_a;
void main() {
  float t = u_time * (0.35 + u_playing * 0.55);
  vec3 p = a_pos;
  p.x += sin(t + a_pos.z * 6.0) * 0.04 * (0.5 + u_playing);
  p.y += cos(t * 1.1 + a_pos.x * 5.0) * 0.05 * (0.5 + u_playing);
  gl_Position = vec4(p.xy, 0.0, 1.0);
  gl_PointSize = mix(1.5, 3.2, u_playing) * (1.0 + 0.4 * sin(t + a_pos.z * 10.0));
  v_a = 0.25 + 0.55 * abs(sin(t + a_pos.z * 8.0));
}
`;

const FS = `
precision mediump float;
varying float v_a;
void main() {
  vec2 c = gl_PointCoord - vec2(0.5);
  float d = dot(c, c);
  if (d > 0.25) discard;
  float soft = smoothstep(0.25, 0.0, d);
  gl_FragColor = vec4(0.55, 0.95, 0.78, v_a * soft * 0.85);
}
`;

function compile(type: number, src: string): WebGLShader | null {
  if (!gl) return null;
  const s = gl.createShader(type);
  if (!s) return null;
  gl.shaderSource(s, src);
  gl.compileShader(s);
  if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
    gl.deleteShader(s);
    return null;
  }
  return s;
}

function initGl(): boolean {
  const canvas = canvasRef.value;
  if (!canvas) return false;
  gl = canvas.getContext('webgl', { alpha: true, premultipliedAlpha: true, antialias: true })
    || canvas.getContext('experimental-webgl', { alpha: true }) as WebGLRenderingContext | null;
  if (!gl) return false;

  const vs = compile(gl.VERTEX_SHADER, VS);
  const fs = compile(gl.FRAGMENT_SHADER, FS);
  if (!vs || !fs) return false;
  program = gl.createProgram();
  if (!program) return false;
  gl.attachShader(program, vs);
  gl.attachShader(program, fs);
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) return false;

  aPos = gl.getAttribLocation(program, 'a_pos');
  uTime = gl.getUniformLocation(program, 'u_time');
  uPlaying = gl.getUniformLocation(program, 'u_playing');

  const data = new Float32Array(COUNT * 3);
  for (let i = 0; i < COUNT; i++) {
    data[i * 3] = (Math.random() * 2 - 1) * 0.92;
    data[i * 3 + 1] = (Math.random() * 2 - 1) * 0.92;
    data[i * 3 + 2] = Math.random();
  }
  buf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);

  gl.enable(gl.BLEND);
  gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
  gl.clearColor(0, 0, 0, 0);
  return true;
}

function resize(): void {
  const canvas = canvasRef.value;
  if (!canvas || !gl) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const w = Math.max(1, Math.floor(canvas.clientWidth * dpr));
  const h = Math.max(1, Math.floor(canvas.clientHeight * dpr));
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
    gl.viewport(0, 0, w, h);
  }
}

function frame(ts: number): void {
  raf = 0;
  if (!alive || !props.active || !gl || !program || isReducedMotion()) return;
  resize();
  const t = (ts - timeOrigin) / 1000;
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.useProgram(program);
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.enableVertexAttribArray(aPos);
  gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);
  gl.uniform1f(uTime, t);
  gl.uniform1f(uPlaying, props.isPlaying ? 1 : 0.35);
  gl.drawArrays(gl.POINTS, 0, COUNT);
  raf = requestAnimationFrame(frame);
}

function startLoop(): void {
  if (!alive || !props.active || isReducedMotion()) return;
  if (!gl && !initGl()) return;
  timeOrigin = performance.now();
  if (!raf) raf = requestAnimationFrame(frame);
}

function stop(): void {
  if (raf) {
    cancelAnimationFrame(raf);
    raf = 0;
  }
}

onMounted(() => {
  alive = true;
  if (props.active && !isReducedMotion()) startLoop();
});

onBeforeUnmount(() => {
  alive = false;
  stop();
  if (gl && buf) gl.deleteBuffer(buf);
  if (gl && program) gl.deleteProgram(program);
  gl = null;
  program = null;
  buf = null;
});

watch(
  () => [props.active, props.isPlaying] as const,
  ([active]) => {
    if (active && !isReducedMotion()) startLoop();
    else stop();
  },
);
</script>

<template>
  <canvas
    ref="canvasRef"
    class="cover-webgl-particles"
    data-test="cover-webgl-particles"
    :data-active="active"
    aria-hidden="true"
  />
</template>

<style scoped>
.cover-webgl-particles {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
  border-radius: inherit;
  z-index: 2;
  mix-blend-mode: screen;
  opacity: 0.9;
}
</style>
