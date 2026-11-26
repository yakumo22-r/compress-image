# 图片压缩 · YKM 示例

参考 [compress-image](https://github.com/yakumo22-r/compress-image) 的单图/批量转换、输出格式、质量和编号命名流程；用 TypeScript、YKM 控件和后台 Worker 实现。

## 运行

在 SDK 根目录执行：

```sh
npm ci
npm ci --prefix apps/compress-image
npx tsc -p apps/compress-image/tsconfig.json
node tools/ykm-app.mjs dev apps/compress-image
```

1. 输入、输出栏启动时显示实际 Windows 路径。点击「图片…」或输入栏旁的「目录…」浏览，也可把图片或文件夹拖进窗口。文件夹只扫描第一层。
2. 点击输出栏旁的「目录…」可另选输出目录。默认输出 PNG，也可选择 JPEG 或 WebP；质量滑块仅影响 JPEG 和 WebP。
   输出质量（1–100）和分辨率缩放（0–100%）都可拖动滑杆或手动输入整数。缩放对三种格式均生效：100% 保持原尺寸，0% 输出 1×1 像素，不支持放大。
3. 结果直接写入输出目录。默认开启「重名替换」；关闭后，同名结果会加 `-2`、`-3` 等后缀。本批次的原图不会被替换；若导出失败，处理结果仍留在应用数据目录的 `output` 暂存批次中。
4. 默认路径为应用数据目录下的 `input` 和 `output`。开发宿主使用 `%LOCALAPPDATA%\YKM\ykm-app\data`，打包版使用 `%LOCALAPPDATA%\YKM\dev.ykm.compress-image\data`。

仅构建可用 `node tools/ykm-app.mjs build apps/compress-image`；分发用 `node tools/ykm-app.mjs package apps/compress-image --mode release`。

## 首版范围

- 文件拖放使用 Windows 宿主的 `desktop.filedrop` 权限，将选中的图片复制到 `data:/` 下的独立导入目录，不需要任意路径读写权限。每次导入最多 256 MiB。
- 竖版布局使用原版 `assets/bg.png` 和图标。支持导入和导出静态 PNG、JPEG、WebP；动画 PNG、动画 WebP 和隔行低位深 PNG 会报错。
- 每批最多 500 个文件，每张最多 24 MiB、800 万像素。PNG 和 WebP 保留透明度，转 JPEG 时以白色填充透明区域。重新编码不保留原始元数据（包括 EXIF 方向和 ICC 配置），也不保证输出更小。
- `jpeg-js`、`fast-png` 由 npm 锁文件管理，libwebp 静态链接到宿主。运行打包后的应用无需 Node.js。
