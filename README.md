# 图片压缩 · YKM 重构版

`refactor/ykm` 从原版 C++ 项目分出，原有源码和构建入口仍保留在仓库根目录。新版位于 `ykm/apps/compress-image`；`ykm/` 同时包含该版本所需的 YKM Windows 宿主源码和构建工具，无需另取 SDK。

新版支持拖放或浏览导入 PNG、JPEG、静态 WebP，默认输出 PNG。可用滑杆或手动输入设置质量与等比缩放，结果直接写入输出目录；默认替换同名结果，但不覆盖本批次原图。详见[应用说明](ykm/apps/compress-image/README.md)。

## Windows x64 构建

需要 Node.js/npm、xmake、Visual Studio C++ 工具链和 Windows SDK。首次构建需要联网下载依赖。在 Git Bash 中执行：

```sh
cd ykm
npm ci
npm ci --prefix apps/compress-image
npm run typecheck
npm run build
```

运行开发版用 `npm run dev`。打包时确保使用 Windows 自带的 `tar.exe`（Git Bash 的 GNU tar 可能生成扩展名为 `.zip` 的 TAR 文件）：

```sh
export PATH="$(cygpath -u "$WINDIR")/System32:$PATH"
npm run package
```

发布 ZIP 在 `ykm/apps/compress-image/.ykm/package/` 下。仓库不提交生成的 EXE、ZIP 或本地依赖。
