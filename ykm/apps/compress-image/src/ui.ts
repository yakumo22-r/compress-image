import fs from "fs";

const { Window, Column, Row, Panel, Text, Input, Radio, Slider, Checkbox, Progress, Button } = ykm.ui;
const dataPath = ykm.dialog.imageDataDirectory();
const defaultInput = "data:/input";
const defaultOutputPath = `${dataPath}\\output`;
let directory = defaultInput;
const inputPath = ykm.state(`${dataPath}\\input`);
const outputPath = ykm.state(defaultOutputPath);
const format = ykm.state("png");
const quality = numericSetting("quality", "输出质量", 80, 1, "仅对 JPEG 和 WebP 生效");
const scale = numericSetting("scale", "分辨率缩放", 100, 0, "100% 保持原尺寸，0% 输出 1×1 像素", "%");
const numbered = ykm.state(false);
const replaceExisting = ykm.state(true);
const progress = ykm.state(0);
const status = ykm.state("等待开始");
const details = ykm.state("");
let nextId = 0;
let active = 0;
let replaceForRun = true;
let watchdog = 0;

function numericSetting(key: string, label: string, initial: number, min: number, tooltip: string, unit = "") {
  const value = ykm.state(initial);
  const draft = ykm.state(String(initial));
  const parse = (text: string) => /^[+-]?\d+$/.test(text.trim()) ? Number(text) : NaN;
  function update(next: number) {
    if (Number.isFinite(next)) value.set(Math.min(100, Math.max(min, Math.round(next))));
    draft.set(String(value.value));
  }
  const commit = () => update(parse(draft.value));
  const row = new Row({ key: `${key}-row`, gap: 8 }).append(
    new Text({ key: `${key}-label`, text: label, width: 100 }),
    new Slider({ key, label: `##${key}`, width: 180, value, min, max: 100, tooltip, onChange: update }),
    new Input({ key: `${key}-input`, label: `##${key}-input`, width: 60, maxLength: 12, value: draft, tooltip,
      onChange: (text: string) => {
        draft.set(text);
        const next = parse(text);
        if (next >= min && next <= 100) value.set(next);
      }, onSubmit: commit }),
    ...(unit ? [new Text({ key: `${key}-unit`, text: unit })] : []),
  );
  return { row, value, commit };
}

const root = new Window({ key: "image-compress", title: "图片压缩 · yakumo22", width: 480, height: 640,
  minWidth: 480, minHeight: 640, padding: 16, overflow: "hidden", backgroundImage: "asset:/bg.png", controlStyle: {
    text: "#fff", frame: "#336699", frameHovered: "#4680b3", frameActive: "#285582",
    button: "#336699", buttonHovered: "#4680b3", buttonActive: "#285582",
    border: "#e6ccb3", accent: "#e6ccb3", frameRadius: 8, grabRadius: 8, borderWidth: 1,
    framePaddingY: 4,
  } });
const form = new Column({ key: "form", gap: 2 });
const center = <T extends YkmControl>(control: T): T => {
  control.addRelation(root, ykm.Relation.CenterCenter);
  return control;
};
const drop = center(new Panel({ key: "drop-area", width: 360, height: 84, padding: 12, overflow: "hidden",
  background: { fill: "#0000", stroke: "#fff", strokeWidth: 3, radius: 10 },
}).append(new Column({ key: "drop-copy", gap: 6 }).append(
  new Text({ key: "drop-title", text: "拖拽图片 / 文件夹到此处", textAlign: "center" }),
  new Text({ key: "drop-hint", text: "PNG / JPEG / WebP · 原图不变", textAlign: "center" }),
)));
const settings = new Column({ key: "settings", gap: 3 }).append(
  new Text({ key: "input-label", text: "输入路径", textAlign: "center" }),
  new Row({ key: "input-row", gap: 5 }).append(
    new Input({ key: "input", label: "##input-path", value: inputPath, tooltip: inputPath, readOnly: true, width: 250 }),
    new Button({ key: "pick-file", label: "图片…", width: 65, height: 26, onClick: () => browse("image") }),
    new Button({ key: "pick-input", label: "目录…##input-dir", width: 65, height: 26, onClick: () => browse("input") }),
  ),
  new Text({ key: "output-label", text: "输出路径", textAlign: "center" }),
  new Row({ key: "output-row", gap: 5 }).append(
    new Input({ key: "output", label: "##output-path", value: outputPath, tooltip: outputPath, readOnly: true, width: 320 }),
    new Button({ key: "pick-output", label: "目录…##output-dir", width: 65, height: 26, onClick: () => browse("output") }),
  ),
  center(new Button({ key: "auto-output", label: "自动设置输出路径", width: 240, height: 26, onClick: autoOutput })),
  new Row({ key: "format-row", gap: 12 }).append(
    new Text({ key: "format-label", text: "输出格式" }),
    new Radio({ key: "format", value: format, direction: "row",
      options: [{ value: "png", label: "PNG" }, { value: "jpg", label: "JPEG" }, { value: "webp", label: "WebP" }],
      onChange: (value: string) => format.set(value) }),
  ),
  quality.row,
  scale.row,
  new Row({ key: "naming-row", gap: 12 }).append(
    new Checkbox({ key: "numbered", label: "按 001、002… 编号输出", value: numbered, onChange: (value: boolean) => numbered.set(value) }),
    new Checkbox({ key: "replace", label: "重名替换", value: replaceExisting,
      tooltip: "关闭后，同名文件加 -2、-3 等后缀", onChange: (value: boolean) => replaceExisting.set(value) }),
  ),
);
form.append(
  new Text({ key: "title", text: "图片压缩 · yakumo22 · v0.1.0", textAlign: "center" }),
  drop,
  settings,
  center(new Button({ key: "run", label: "开始处理", width: 150, height: 40, onClick: run })),
  center(new Progress({ key: "progress", value: progress, min: 0, max: 1, width: 310, height: 22 })),
  new Text({ key: "status", text: status, textAlign: "center" }),
  new Column({ key: "result-scroll", height: 48, overflow: "auto" }).append(
    new Text({ key: "details", text: details, wrap: true }),
  ),
);
root.append(form);

addEventListener("filedrop", event => { if (event.isTrusted) receiveImport(event); });

function autoOutput(): void {
  if (active) return;
  try {
    outputPath.set(ykm.dialog.autoImageOutput(directory));
    status.set("已自动设置输出路径");
  } catch (error) {
    status.set("无法自动设置输出路径，请手动选择目录");
    details.set(String(error));
  }
}

function browse(kind: "image" | "input" | "output"): void {
  if (active) return;
  try {
    if (kind === "output") {
      const path = ykm.dialog.pickImageOutput();
      if (path) outputPath.set(path);
    } else {
      const result = kind === "image" ? ykm.dialog.pickImage() : ykm.dialog.pickImageDirectory();
      if (result) receiveImport(result);
    }
  } catch (error) { status.set(String(error)); }
}

function receiveImport(result: YkmImageImport): void {
  if (!result.directory) { if (!active) status.set("未找到可导入的 PNG、JPEG 或 WebP，保留当前输入"); return; }
  if (!/^data:\/drop-\d+-\d+$/.test(result.directory)) return;
  if (active) {
    void fs.rm(result.directory, { recursive: true, force: true }).catch(ykm.error);
    return;
  }
  if (directory !== defaultInput) void fs.rm(directory, { recursive: true, force: true }).catch(ykm.error);
  directory = result.directory;
  inputPath.set(result.sources.length > 1 ? `${result.sources[0]}（另 ${result.sources.length - 1} 项）` : result.sources[0]);
  progress.set(0);
  details.set("");
  status.set(`已导入 ${result.imported} 张图片${result.skipped ? "（部分文件未导入）" : ""}`);
}

function run(): void {
  if (active) return;
  const id = active = ++nextId;
  quality.commit();
  scale.commit();
  const task = { id, directory, format: format.value, quality: quality.value.value, scale: scale.value.value, numbered: numbered.value };
  replaceForRun = replaceExisting.value;
  progress.set(0);
  details.set("");
  status.set("正在启动 Worker…");
  const deadline = Date.now() + 5000;
  try {
    if (!ykm.sessions.isRunning("worker")) ykm.sessions.start("worker");
    sendWhenReady();
  } catch (error) { finish(String(error)); }

  function sendWhenReady(): void {
    if (active !== id) return;
    try {
      if (ykm.sessions.isRunning("worker")) {
        ykm.send("worker", task);
        status.set("正在处理图片…");
        watchdog = setInterval(() => {
          if (!ykm.sessions.isRunning("worker")) finish("Worker 意外退出，请重试。");
        }, 500);
      } else if (Date.now() >= deadline) finish("Worker 启动超时，请查看 F12 日志。");
      else setTimeout(sendWhenReady, 50);
    } catch (error) { finish(String(error)); }
  }
}

function finish(message: string): void {
  if (watchdog) clearInterval(watchdog);
  watchdog = 0;
  active = 0;
  status.set(message);
}

export function onMessage(payload: YkmCloneValue, sender: string): void {
  if (sender !== "worker" || !active || !payload || typeof payload !== "object" || Array.isArray(payload) || payload.id !== active) return;
  if (payload.type === "progress" && typeof payload.completed === "number" && typeof payload.total === "number") {
    progress.set(payload.completed / payload.total);
    status.set(`正在处理：${payload.completed} / ${payload.total}`);
  } else if (payload.type === "error") {
    finish(String(payload.message));
  } else if (payload.type === "done") {
    const kib = (bytes: YkmCloneValue) => (Number(bytes) / 1024).toFixed(1);
    const size = Number(payload.success) ? ` · ${kib(payload.original)} → ${kib(payload.written)} KiB（成功文件）` : "";
    const errors = Array.isArray(payload.errors) ? payload.errors.join("\n") : "";
    const batch = String(payload.folder);
    const message = `完成：成功 ${payload.success} 张，失败 ${payload.failed} 张${size}`;
    try {
      const path = Number(payload.success) ? ykm.dialog.publishImages(batch, directory, replaceForRun) : "";
      if (/^\d+-[a-z0-9]+$/.test(batch)) void fs.rm(`data:/output/${batch}`, { recursive: true, force: true }).catch(ykm.error);
      finish(message);
      details.set([path && `输出：${path}`, errors].filter(Boolean).join("\n"));
    } catch (error) {
      finish("压缩完成，导出失败");
      details.set(`结果保留在：${defaultOutputPath}\\${batch}\n${String(error)}`);
    }
  }
}

export function render(): YkmControl { return root; }
