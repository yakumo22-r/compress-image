import fs from "fs";
import { maxBytes, transcode, type Format } from "./codec";

let busy = false;
const outputRoot = "data:/output";

function formatOf(file: string): Format | null {
  if (/\.png$/i.test(file)) return "png";
  if (/\.webp$/i.test(file)) return "webp";
  return /\.jpe?g$/i.test(file) ? "jpg" : null;
}

export function onMessage(payload: YkmCloneValue, sender: string): void {
  if (sender !== "ui" || busy || !payload || typeof payload !== "object" || Array.isArray(payload)) return;
  const { id, directory, format, quality, scale, numbered } = payload;
  if (typeof id !== "number" || typeof directory !== "string" ||
      (directory !== "data:/input" && !/^data:\/drop-\d+-\d+$/.test(directory)) ||
      (format !== "png" && format !== "jpg" && format !== "webp") ||
      typeof quality !== "number" || !Number.isFinite(quality) || typeof numbered !== "boolean" ||
      typeof scale !== "number" || !Number.isInteger(scale) || scale < 0 || scale > 100) return;
  busy = true;
  const send = (update: Record<string, YkmCloneValue>) => ykm.send(sender, { id, ...update });
  void run(directory, format, Math.max(1, Math.min(100, Math.round(quality))), scale, numbered, send)
    .catch(error => send({ type: "error", message: String(error) })).finally(() => { busy = false; });
}

async function run(inputRoot: string, format: Format, quality: number, scale: number, numbered: boolean,
  send: (update: Record<string, YkmCloneValue>) => void): Promise<void> {
  const names = (await fs.readdir(inputRoot)).filter(formatOf).sort();
  if (!names.length) throw new Error("输入目录没有 PNG、JPEG 或 WebP，请选择或拖入图片");
  if (names.length > 500) throw new Error("单次最多处理 500 个文件");
  await fs.mkdir(outputRoot, { recursive: true });
  const folder = `${Date.now()}-${Math.random().toString(36).slice(2, 10)}`;
  const output = `${outputRoot}/${folder}`;
  if (fs.existsSync(output)) throw new Error("输出目录已存在，请重试");
  await fs.mkdir(output);
  let completed = 0, success = 0, original = 0, written = 0;
  const used = new Set<string>();
  const errors: string[] = [];
  for (const name of names) {
    try {
      const input = `${inputRoot}/${name}`;
      const stat = await fs.stat(input);
      if (!stat.isFile()) throw new Error("不是文件");
      if (stat.size > maxBytes) throw new Error("文件超过 24 MiB");
      const bytes = await fs.readFile(input);
      if (!(bytes instanceof Uint8Array)) throw new Error("无法读取图片字节");
      const result = transcode(bytes, formatOf(name)!, format, quality, scale);
      const stem = numbered ? String(completed + 1).padStart(3, "0") : name.replace(/\.[^.]+$/, "") || "image";
      let base = stem;
      for (let n = 2; used.has(base.toLowerCase()); n++) base = `${stem}-${n}`;
      await fs.writeFile(`${output}/${base}.${format}`, result);
      used.add(base.toLowerCase());
      success++;
      original += bytes.length;
      written += result.length;
    } catch (error) {
      if (errors.length < 8) errors.push(`${name}: ${String(error).slice(0, 180)}`);
    }
    completed++;
    if (completed === names.length || completed % Math.ceil(names.length / 50) === 0) {
      send({ type: "progress", completed, total: names.length });
    }
  }
  send({ type: "done", folder, success, failed: completed - success, original, written, errors });
}
