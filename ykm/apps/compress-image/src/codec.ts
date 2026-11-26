import "./quickjs";
import jpeg from "jpeg-js";
import { decode as decodePng, encode as encodePng } from "fast-png";
import { decode as decodeWebp, encode as encodeWebp } from "ykm:webp";

export type Format = "png" | "jpg" | "webp";
const maxPixels = 8_000_000;
export const maxBytes = 24 * 1024 * 1024;

export function transcode(bytes: Uint8Array, source: Format, target: Format, quality: number, scale: number): Uint8Array {
  if (bytes.length > maxBytes) throw new Error("文件超过 24 MiB");
  if (source === "jpg") {
    const image = jpeg.decode(bytes, {
      useTArray: true, tolerantDecoding: false, maxResolutionInMP: maxPixels / 1e6, maxMemoryUsageInMB: 128,
    });
    return encodeRgba(resize(image.width, image.height, image.data, 4, scale), target, quality);
  }
  if (source === "webp") {
    let image: ReturnType<typeof decodeWebp>;
    try {
      image = decodeWebp(bytes);
    } catch (error) {
      if (error instanceof Error && error.message === "Animated WebP is not supported") {
        throw new Error("暂不支持动画 WebP");
      }
      throw error;
    }
    if (target === "jpg") {
      for (let i = 0; i < image.data.length; i += 4) {
        const a = image.data[i + 3] / 255;
        for (let c = 0; c < 3; c++) image.data[i + c] = Math.round(image.data[i + c] * a + 255 * (1 - a));
        image.data[i + 3] = 255;
      }
    }
    return encodeRgba(resize(image.width, image.height, image.data, 4, scale), target, quality);
  }
  const header = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes.length < 33 || header.getUint32(0) !== 0x89504e47 || header.getUint32(4) !== 0x0d0a1a0a ||
      header.getUint32(8) !== 13 || header.getUint32(12) !== 0x49484452) throw new Error("无效的 PNG 文件头");
  const width = header.getUint32(16);
  const height = header.getUint32(20);
  if (!width || !height || width * height > maxPixels) throw new Error("PNG 像素数超限（800 万）");
  scaledSize(width, height, scale);
  if (bytes[24] < 8 && bytes[28] !== 0) throw new Error("暂不支持隔行低位深 PNG");

  // Keep only chunks needed for the pixels; metadata is not copied to output.
  const clean = new Uint8Array(bytes.length);
  clean.set(bytes.subarray(0, 8));
  let end = 8;
  let ended = false;
  for (let offset = 8; offset <= bytes.length - 12;) {
    const size = header.getUint32(offset);
    const type = header.getUint32(offset + 4);
    const next = offset + size + 12;
    if (next > bytes.length) throw new Error("PNG 数据不完整");
    if (type === 0x6163544c) throw new Error("暂不支持动画 PNG");
    if ([0x49484452, 0x504c5445, 0x74524e53, 0x49444154, 0x49454e44].includes(type)) {
      if (type === 0x49484452 && offset !== 8) throw new Error("PNG 文件头重复");
      clean.set(bytes.subarray(offset, next), end);
      end += next - offset;
    } else if ((type & 0x20000000) === 0) throw new Error("不支持的 PNG 数据块");
    if (type === 0x49454e44) { ended = true; break; }
    offset = next;
  }
  if (!ended) throw new Error("缺少 PNG 结束块");
  const png = decodePng(clean.subarray(0, end), { checkCrc: true });
  const hasAlpha = png.channels === 2 || png.channels === 4 || !!png.transparency || !!png.palette?.some(entry => entry.length === 4);
  const stride = target !== "png" || hasAlpha ? 4 : 3;
  const depth = target === "png" && png.depth === 16 ? 16 : 8;
  const top = (1 << depth) - 1;
  const sourceTop = png.palette ? 255 : (1 << png.depth) - 1;
  const pixels = depth === 16 ? new Uint16Array(width * height * stride) : new Uint8Array(width * height * stride);
  const packedRowBytes = Math.ceil(width * png.depth / 8);
  for (let i = 0; i < width * height; i++) {
    const p = i * png.channels;
    const bit = (i % width) * png.depth;
    const first = png.depth < 8 ?
      (png.data[Math.floor(i / width) * packedRowBytes + (bit >> 3)] >> (8 - png.depth - (bit & 7))) & ((1 << png.depth) - 1) : png.data[p];
    const palette = png.palette?.[first];
    if (png.palette && !palette) throw new Error("PNG 调色板索引无效");
    const gray = png.channels < 3;
    const r = palette?.[0] ?? first;
    const g = palette?.[1] ?? (gray ? r : png.data[p + 1]);
    const b = palette?.[2] ?? (gray ? r : png.data[p + 2]);
    const transparent = png.transparency;
    const alpha = palette ? (palette[3] ?? 255) : png.channels === 2 || png.channels === 4 ? png.data[p + png.channels - 1] :
      transparent && (gray ? r === transparent[0] : r === transparent[0] && g === transparent[1] && b === transparent[2]) ? 0 : sourceTop;
    const a = alpha / sourceTop;
    const base = i * stride;
    const color = (v: number) => Math.round(top * (target === "jpg" ? (v / sourceTop) * a + 1 - a : v / sourceTop));
    pixels[base] = color(r);
    pixels[base + 1] = color(g);
    pixels[base + 2] = color(b);
    if (stride === 4) pixels[base + 3] = target === "jpg" ? 255 : Math.round(top * a);
  }
  const scaled = resize(width, height, pixels, stride, scale);
  return target === "png" ? encodePng({ ...scaled, channels: stride, depth }, { zlib: { level: 9 } }) :
    encodeRgba({ ...scaled, data: scaled.data as Uint8Array }, target, quality);
}

function scaledSize(width: number, height: number, percent: number) {
  const w = Math.max(1, Math.round(width * percent / 100));
  const h = Math.max(1, Math.round(height * percent / 100));
  if (w * h > maxPixels) throw new Error("缩放后超过 800 万像素，请调低缩放比例");
  return { w, h };
}

function resize<T extends Uint8Array | Uint16Array>(width: number, height: number, data: T, channels: number,
  percent: number): { width: number; height: number; data: T } {
  const { w, h } = scaledSize(width, height, percent);
  if (w === width && h === height) return { width, height, data };
  const output = (data instanceof Uint16Array ? new Uint16Array(w * h * channels) : new Uint8Array(w * h * channels)) as T;
  const dx = width / w, dy = height / h;
  const alpha = channels === 4;
  // Use the pixel footprint when shrinking; use center-aligned linear interpolation when growing.
  const start = (i: number, step: number, size: number) => step >= 1 ? i * step :
    Math.max(0, Math.min(size - 1, (i + 0.5) * step - 0.5));
  for (let y = 0; y < h; y++) {
    const y0 = start(y, dy, height), y1 = Math.min(height, y0 + Math.max(1, dy));
    for (let x = 0; x < w; x++) {
      const x0 = start(x, dx, width), x1 = Math.min(width, x0 + Math.max(1, dx));
      let r = 0, g = 0, b = 0, weight = 0;
      for (let sy = Math.floor(y0); sy < y1; sy++) {
        const wy = Math.min(y1, sy + 1) - Math.max(y0, sy);
        for (let sx = Math.floor(x0); sx < x1; sx++) {
          const i = (sy * width + sx) * channels;
          const k = wy * (Math.min(x1, sx + 1) - Math.max(x0, sx)) * (alpha ? data[i + 3] : 1);
          weight += k;
          r += k * data[i];
          g += k * data[i + 1];
          b += k * data[i + 2];
        }
      }
      const i = (y * w + x) * channels;
      if (weight) {
        output[i] = Math.round(r / weight);
        output[i + 1] = Math.round(g / weight);
        output[i + 2] = Math.round(b / weight);
      }
      if (alpha) output[i + 3] = Math.round(weight / ((x1 - x0) * (y1 - y0)));
    }
  }
  return { width: w, height: h, data: output };
}

function encodeRgba(image: { width: number; height: number; data: Uint8Array }, target: Format, quality: number): Uint8Array {
  if (target === "png") return encodePng({ ...image, channels: 4 }, { zlib: { level: 9 } });
  if (target === "webp") return encodeWebp(image.data, image.width, image.height, quality);
  return encodeJpeg(image, quality);
}

function encodeJpeg(image: { width: number; height: number; data: Uint8Array }, quality: number): Uint8Array {
  // jpeg-js's CommonJS encoder only uses Buffer.from for the final byte array.
  const globals = globalThis as typeof globalThis & { Buffer?: { from(values: number[]): Uint8Array } };
  const shim = !globals.Buffer;
  if (shim) globals.Buffer = { from: values => new Uint8Array(values) };
  try {
    return jpeg.encode({ width: image.width, height: image.height, data: image.data }, quality).data;
  } finally {
    if (shim) delete globals.Buffer;
  }
}
