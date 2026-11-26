import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import fs from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";
import JSON5 from "json5";
import { build } from "esbuild";

const defaultPermissions = ["fs.asset", "fs.data", "fs.cache", "fs.app"];
const allowedPermissions = new Set([...defaultPermissions, "desktop.filedrop"]);

const sdkRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function cString(value) {
  return JSON.stringify(value).replaceAll("\\u2028", "\\u2028").replaceAll("\\u2029", "\\u2029");
}

function cBytes(buffer) {
  const lines = [];
  for (let index = 0; index < buffer.length; index += 16) {
    const slice = buffer.subarray(index, index + 16);
    const values = [];
    for (const byte of slice) values.push(`0x${byte.toString(16).padStart(2, "0")}`);
    lines.push(values.join(","));
  }
  return lines.join(",\n  ");
}

function fail(message) {
  console.error(`ykm-app: ${message}`);
  process.exit(1);
}

function safeName(name) {
  if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(name)) {
    fail(`invalid session name '${name}'`);
  }
  return name;
}

async function exists(file) {
  try {
    await fs.access(file);
    return true;
  } catch {
    return false;
  }
}

function luaString(value) {
  return JSON.stringify(value.replaceAll("\\", "/"));
}

function executableName(app) {
  if (typeof app.executable === "string") {
    if (!/^[A-Za-z][A-Za-z0-9._-]{0,63}$/.test(app.executable)) fail(`invalid app.executable '${app.executable}'`);
    return app.executable;
  }
  const fallback = String(app.id ?? "").split(".").pop() ?? "";
  if (!/^[A-Za-z][A-Za-z0-9._-]{0,63}$/.test(fallback)) fail("app.executable is required");
  return fallback;
}

function fileVersion(version) {
  const parts = String(version ?? "0.0.0").split(/[^\d]+/).map((item) => Number(item) || 0);
  while (parts.length < 4) parts.push(0);
  return parts.slice(0, 4);
}

function rcString(value) {
  return String(value ?? "").replaceAll("\\", "\\\\").replaceAll("\"", "\"\"");
}

function run(command, args, cwd) {
  const result = spawnSync(command, args, { cwd, stdio: "inherit", shell: false });
  if (result.error) fail(`${command}: ${result.error.message}`);
  if (result.status !== 0) fail(`${command} ${args.join(" ")} failed`);
}

function resolvePermissions(manifest) {
  if (manifest.permissions == null) return [...defaultPermissions];
  if (!Array.isArray(manifest.permissions)) fail("permissions must be an array");
  const permissions = [];
  for (const item of manifest.permissions) {
    if (typeof item !== "string" || !allowedPermissions.has(item)) fail(`invalid permission '${item}'`);
    if (!permissions.includes(item)) permissions.push(item);
  }
  return permissions;
}

function createIco() {
  const width = 32;
  const xorSize = width * width * 4;
  const andRow = Math.ceil(width / 32) * 4;
  const andSize = andRow * width;
  const header = 6 + 16;
  const dib = 40 + xorSize + andSize;
  const buf = Buffer.alloc(header + dib);
  buf.writeUInt16LE(0, 0);
  buf.writeUInt16LE(1, 2);
  buf.writeUInt16LE(1, 4);
  buf.writeUInt8(width, 6);
  buf.writeUInt8(width, 7);
  buf.writeUInt16LE(1, 10);
  buf.writeUInt16LE(32, 12);
  buf.writeUInt32LE(dib, 14);
  buf.writeUInt32LE(header, 18);
  buf.writeUInt32LE(40, header);
  buf.writeInt32LE(width, header + 4);
  buf.writeInt32LE(width * 2, header + 8);
  buf.writeUInt16LE(1, header + 12);
  buf.writeUInt16LE(32, header + 14);
  buf.writeUInt32LE(xorSize + andSize, header + 20);
  const px = header + 40;
  for (let y = 0; y < width; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const i = px + ((width - 1 - y) * width + x) * 4;
      const inner = x >= 4 && x < 28 && y >= 4 && y < 28;
      const bar = inner && x >= 10 && x < 16 && y >= 8 && y < 24;
      if (bar) {
        buf[i] = 255;
        buf[i + 1] = 255;
        buf[i + 2] = 255;
        buf[i + 3] = 255;
      } else if (inner) {
        buf[i] = 210;
        buf[i + 1] = 120;
        buf[i + 2] = 40;
        buf[i + 3] = 255;
      }
    }
  }
  return buf;
}

async function ensureIcon(root, manifest) {
  const relative = typeof manifest.app.icon === "string" ? manifest.app.icon : "assets/app.ico";
  const absolute = path.resolve(root, relative);
  if (!await exists(absolute)) {
    await fs.mkdir(path.dirname(absolute), { recursive: true });
    await fs.writeFile(absolute, createIco());
  }
  return absolute;
}

async function findBuiltExe(buildir, mode, exeName) {
  const candidates = [
    path.join(buildir, "windows", "x64", mode, `${exeName}.exe`),
    path.join(buildir, `${exeName}.exe`),
  ];
  for (const candidate of candidates) if (await exists(candidate)) return candidate;
  fail(`missing ${exeName}.exe in ${buildir}`);
}

async function sha256File(file) {
  const hash = createHash("sha256");
  hash.update(await fs.readFile(file));
  return hash.digest("hex");
}

async function loadNativePackages(root, requested) {
  const packages = [];
  const modules = new Set();
  for (const request of requested) {
    if (typeof request !== "string" || !/^[A-Za-z][A-Za-z0-9_.-]{0,127}$/.test(request)) {
      fail(`invalid native static library '${request}'`);
    }
    const candidates = [
      path.join(root, "native", request),
      path.join(sdkRoot, "native", request),
    ];
    const directory = await (async () => {
      for (const candidate of candidates) if (await exists(path.join(candidate, "ykm-jsapi.json"))) return candidate;
      return null;
    })();
    if (!directory) fail(`cannot find native package '${request}' (expected native/${request}/ykm-jsapi.json)`);
    let descriptor;
    try {
      descriptor = JSON.parse(await fs.readFile(path.join(directory, "ykm-jsapi.json"), "utf8"));
    } catch (error) {
      fail(`cannot read native package '${request}': ${error.message}`);
    }
    if (descriptor.schemaVersion !== 1) fail(`native package '${request}' schemaVersion must be 1`);
    if (descriptor.name !== request) fail(`native package '${request}' has mismatched name '${descriptor.name}'`);
    if (descriptor.abiVersion !== 2) fail(`native package '${request}' requires ABI ${descriptor.abiVersion}; runtime ABI is 2`);
    if (typeof descriptor.module !== "string" || !/^[A-Za-z][A-Za-z0-9_./-]*$/.test(descriptor.module)) {
      fail(`native package '${request}' has invalid module name`);
    }
    if (modules.has(descriptor.module)) fail(`duplicate native module '${descriptor.module}'`);
    modules.add(descriptor.module);
    if (typeof descriptor.entry !== "string" || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(descriptor.entry)) {
      fail(`native package '${request}' has invalid C entry symbol`);
    }
    const target = descriptor.targets?.["windows-x64-msvc"] ?? descriptor.targets?.["windows-x64-mingw"];
    if (!target || !["source", "library"].includes(target.kind)) {
      fail(`native package '${request}' does not support windows-x64-msvc`);
    }
    if (target.kind === "source" && (!Array.isArray(target.sources) || target.sources.length === 0)) {
      fail(`native package '${request}' source target requires sources`);
    }
    if (target.kind === "library" && typeof target.library !== "string") {
      fail(`native package '${request}' library target requires library`);
    }
    if (descriptor.types != null && typeof descriptor.types !== "string") {
      fail(`native package '${request}' types must be a path`);
    }
    if (descriptor.types && !await exists(path.resolve(directory, descriptor.types))) {
      fail(`native package '${request}' missing types file '${descriptor.types}'`);
    }
    for (const relative of [...(target.sources ?? []), ...(target.includeDirs ?? []), ...(target.library ? [target.library] : [])]) {
      const absolute = path.resolve(directory, relative);
      if (!absolute.startsWith(path.resolve(directory) + path.sep) && absolute !== path.resolve(directory)) {
        fail(`native package '${request}' path escapes package: ${relative}`);
      }
      if (!relative.includes("*") && !await exists(absolute)) fail(`native package '${request}' missing ${relative}`);
    }
    packages.push({ descriptor, directory, target });
  }
  return packages;
}

async function buildProject(projectArg) {
  const root = path.resolve(projectArg ?? ".");
  const manifestPath = path.join(root, "ykm.json5");
  let manifest;
  try {
    manifest = JSON5.parse(await fs.readFile(manifestPath, "utf8"));
  } catch (error) {
    fail(`cannot read ${manifestPath}: ${error.message}`);
  }

  if (manifest.schemaVersion !== 1) fail("schemaVersion must be 1");
  if (!manifest.app?.id || !manifest.app?.name) fail("app.id and app.name are required");
  if (!Array.isArray(manifest.sessions) || manifest.sessions.length === 0) fail("sessions must not be empty");

  const names = new Set();
  for (const session of manifest.sessions) {
    safeName(session.name);
    if (names.has(session.name)) fail(`duplicate session '${session.name}'`);
    names.add(session.name);
    if (typeof session.entry !== "string") fail(`session '${session.name}' requires entry`);
    if (!["main", "ui", "cli", "worker"].includes(session.role)) {
      fail(`session '${session.name}' has invalid role '${session.role}'`);
    }
  }
  const mainSessions = manifest.sessions.filter((item) => item.role === "main");
  if (mainSessions.length !== 1) fail("exactly one session must have role 'main'");

  const output = path.resolve(root, manifest.build?.outDir ?? "dist");
  await fs.rm(output, { recursive: true, force: true });
  await fs.mkdir(path.join(output, "sessions"), { recursive: true });

  const permissions = resolvePermissions(manifest);
  const nativeRequests = manifest.native?.staticLibraries ?? [];
  if (!Array.isArray(nativeRequests)) fail("native.staticLibraries must be an array");
  const nativePackages = await loadNativePackages(root, nativeRequests);
  const nativeModules = [
    "fs",
    "fs/promises",
    "path",
    "path/win32",
    "path/posix",
    "node:fs",
    "node:fs/promises",
    "node:path",
    "ykm:webp",
    ...nativePackages.map((item) => item.descriptor.module),
  ];
  const entries = Object.fromEntries(manifest.sessions.map((session) => [
    `sessions/${session.name}`,
    `ykm-session:${session.name}`,
  ]));
  const sessionsByName = new Map(manifest.sessions.map((session) => [session.name, session]));
  await build({
    entryPoints: entries,
    outdir: output,
    outbase: ".",
    bundle: true,
    splitting: true,
    platform: "neutral",
    format: "esm",
    target: "es2020",
    external: nativeModules,
    entryNames: "[dir]/[name]",
    chunkNames: "modules/[name]-[hash]",
    sourcemap: manifest.build?.sourcemap ?? true,
    minify: manifest.build?.minify ?? false,
    banner: { js: "/* ykm:module */" },
    plugins: [{
      name: "ykm-session-entry",
      setup(esbuild) {
        esbuild.onResolve({ filter: /^ykm-session:/ }, (args) => ({
          path: args.path.slice("ykm-session:".length),
          namespace: "ykm-session",
        }));
        esbuild.onLoad({ filter: /.*/, namespace: "ykm-session" }, (args) => {
          const session = sessionsByName.get(args.path);
          const entry = path.resolve(root, session.entry).replaceAll("\\", "/");
          return {
            contents: `import * as session from ${JSON.stringify(entry)};\nfor (const key of ["render", "onMessage", "main", "start"]) if (typeof session[key] === "function") globalThis[key] = session[key];`,
            loader: "js",
            resolveDir: root,
          };
        });
      },
    }],
    logLevel: "silent",
  });

  const normalized = {
    schemaVersion: 1,
    app: manifest.app,
    runtime: manifest.runtime ?? {},
    sessions: manifest.sessions.map(({ name, role }) => ({
      name,
      role,
      script: `sessions/${name}.js`,
      autostart: role === "main",
    })),
    native: {
      dynamicLibraries: manifest.native?.dynamicLibraries ?? [],
      staticLibraries: nativePackages.map(({ descriptor }) => ({
        name: descriptor.name,
        version: descriptor.version,
        module: descriptor.module,
        abiVersion: descriptor.abiVersion,
      })),
    },
    permissions,
  };
  await fs.writeFile(path.join(output, "ykm.manifest.json"), JSON.stringify(normalized, null, 2));
  await fs.writeFile(
    path.join(output, "ykm.sessions"),
    normalized.sessions.map((item) => [item.name, item.role, item.autostart ? "1" : "0", item.script].join("\t")).join("\n") + "\n",
  );
  await fs.writeFile(path.join(output, "ykm.permissions"), `${permissions.join("\n")}${permissions.length ? "\n" : ""}`);
  const generated = path.join(root, ".ykm", "generated");
  await fs.mkdir(generated, { recursive: true });
  const registryDeclarations = nativePackages.map(({ descriptor }) =>
    `extern const YkmJsApi* ${descriptor.entry}(void);`).join("\n");
  const registryCalls = nativePackages.map(({ descriptor }) =>
    `    (void)ykm_jsapi_register(${descriptor.entry}());`).join("\n");
  await fs.writeFile(path.join(generated, "ykm_jsapi_register_packed.c"),
    `#include <ykm/jsapi.h>\n\n${registryDeclarations}\n\nvoid ykm_jsapi_register_packed(void) {\n${registryCalls}\n}\n`);
  const nativeTypeReferences = nativePackages
    .filter(({ descriptor }) => descriptor.types)
    .map(({ descriptor, directory }) => `/// <reference path=${JSON.stringify(path.resolve(directory, descriptor.types).replaceAll("\\", "/"))} />`);
  await fs.writeFile(path.join(generated, "ykm-native.d.ts"), nativeTypeReferences.join("\n") + "\n");

  const nativeLua = [];
  const nativeTargetNames = [];
  for (const { descriptor, directory, target } of nativePackages) {
    const targetName = `ykm-native-${descriptor.name}`;
    if (target.kind === "source") {
      nativeTargetNames.push(targetName);
      nativeLua.push(`target(${luaString(targetName)})`);
      nativeLua.push(`    set_kind("static")`);
      nativeLua.push(`    add_files(${target.sources.map((item) => luaString(path.resolve(directory, item))).join(", ")})`);
      nativeLua.push(`    add_includedirs(${[path.resolve(sdkRoot, "include"), ...((target.includeDirs ?? []).map((item) => path.resolve(directory, item)))].map(luaString).join(", ")})`);
      nativeLua.push(`    add_packages("quickjs-ng")`);
      nativeLua.push(`target_end()`);
    } else {
      const library = path.resolve(directory, target.library);
      const extension = path.extname(library);
      const linkName = path.basename(library, extension).replace(/^lib/, "");
      nativeLua.push(`target("ykm")`);
      nativeLua.push(`    add_linkdirs(${luaString(path.dirname(library))})`);
      nativeLua.push(`    add_links(${luaString(linkName)})`);
      nativeLua.push(`target_end()`);
    }
  }
  const exeName = executableName(manifest.app);
  const version = fileVersion(manifest.app.version);
  const icon = await ensureIcon(root, manifest);
  const rcPath = path.join(generated, "ykm_app.rc");
  await fs.writeFile(rcPath, [
    `1 ICON ${JSON.stringify(icon.replaceAll("\\", "/"))}`,
    "1 VERSIONINFO",
    `FILEVERSION ${version.join(",")}`,
    `PRODUCTVERSION ${version.join(",")}`,
    "FILEFLAGSMASK 0x3fL",
    "#ifdef _DEBUG",
    "FILEFLAGS 0x1L",
    "#else",
    "FILEFLAGS 0x0L",
    "#endif",
    "FILEOS 0x40004L",
    "FILETYPE 0x1L",
    "FILESUBTYPE 0x0L",
    "BEGIN",
    "  BLOCK \"StringFileInfo\"",
    "  BEGIN",
    "    BLOCK \"040904b0\"",
    "    BEGIN",
    `      VALUE \"CompanyName\", \"YKM\"`,
    `      VALUE \"FileDescription\", \"${rcString(manifest.app.name)}\"`,
    `      VALUE \"FileVersion\", \"${rcString(manifest.app.version ?? "0.0.0")}\"`,
    `      VALUE \"InternalName\", \"${rcString(exeName)}\"`,
    `      VALUE \"OriginalFilename\", \"${rcString(`${exeName}.exe`)}\"`,
    `      VALUE \"ProductName\", \"${rcString(manifest.app.name)}\"`,
    `      VALUE \"ProductVersion\", \"${rcString(manifest.app.version ?? "0.0.0")}\"`,
    "    END",
    "  END",
    "  BLOCK \"VarFileInfo\"",
    "  BEGIN",
    "    VALUE \"Translation\", 0x409, 1200",
    "  END",
    "END",
    "",
  ].filter((line) => line !== null).join("\n"));
  nativeLua.push(`target("ykm")`);
  if (nativeTargetNames.length) nativeLua.push(`    add_deps(${nativeTargetNames.map(luaString).join(", ")})`);
  const systemLinks = [...new Set(nativePackages.flatMap(({ target }) => target.links ?? []))];
  if (systemLinks.length) nativeLua.push(`    add_links(${systemLinks.map(luaString).join(", ")})`);
  nativeLua.push(`    set_basename(${luaString(exeName)})`);
  nativeLua.push(`    add_files(${luaString(rcPath)})`);
  nativeLua.push(`target_end()`);
  await fs.writeFile(path.join(generated, "ykm_native.lua"), nativeLua.join("\n") + "\n");
  const outputFiles = [];
  async function collect(directory) {
    for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
      const absolute = path.join(directory, entry.name);
      if (entry.isDirectory()) await collect(absolute);
      else if (entry.name.endsWith(".js")) outputFiles.push(absolute);
    }
  }
  await collect(output);
  outputFiles.sort();
  const moduleRows = [];
  const moduleArrays = [];
  for (let index = 0; index < outputFiles.length; index += 1) {
    const file = outputFiles[index];
    const virtualName = `app:/${path.relative(output, file).replaceAll("\\", "/")}`;
    const bytes = await fs.readFile(file);
    moduleArrays.push(`static const unsigned char ykm_module_${index}[] = {\n  ${cBytes(bytes)}\n};`);
    moduleRows.push(`  {${cString(virtualName)}, ykm_module_${index}, ${bytes.length}u}`);
  }
  const sessionRows = normalized.sessions.map((session) =>
    `  {${cString(session.name)}, ${cString(session.role)}, ${cString(`app:/${session.script}`)}, ${session.autostart ? 1 : 0}u}`);
  const assetFiles = [];
  const assetRoot = path.join(root, "assets");
  async function collectAssets(directory) {
    for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
      const absolute = path.join(directory, entry.name);
      if (entry.isDirectory()) await collectAssets(absolute);
      else if (entry.isFile()) assetFiles.push(absolute);
    }
  }
  try {
    await collectAssets(assetRoot);
    await fs.cp(assetRoot, path.join(output, "assets"), { recursive: true });
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
  }
  assetFiles.sort();
  const assetRows = [];
  const assetArrays = [];
  for (let index = 0; index < assetFiles.length; index += 1) {
    const file = assetFiles[index];
    const name = path.relative(assetRoot, file).replaceAll("\\", "/");
    const bytes = await fs.readFile(file);
    assetArrays.push(`static const unsigned char ykm_asset_${index}[] = {\n  ${cBytes(bytes)}\n};`);
    assetRows.push(`  {${cString(name)}, ykm_asset_${index}, ${bytes.length}u}`);
  }
  const assetTable = assetRows.length
    ? `${assetArrays.join("\n")}\nstatic const YkmEmbeddedAsset ykm_assets[] = {\n${assetRows.join(",\n")}\n};`
    : "";
  const permissionTable = permissions.length
    ? `static const char* const ykm_permissions[] = {\n${permissions.map((item) => `  ${cString(item)}`).join(",\n")}\n};`
    : "";
  const repository = typeof manifest.app.repository === "string"
    ? manifest.app.repository
    : manifest.app.repository?.url ?? "";
  const embeddedSource = `#include <ykm/embedded_app.h>\n\n${moduleArrays.join("\n")}\n${assetTable}\n${permissionTable}\n\nstatic const YkmEmbeddedModule ykm_modules[] = {\n${moduleRows.join(",\n")}\n};\n\nstatic const YkmEmbeddedSession ykm_sessions[] = {\n${sessionRows.join(",\n")}\n};\n\nstatic const YkmEmbeddedApp ykm_app = {\n  sizeof(YkmEmbeddedApp),\n  ${cString(manifest.app.id)},\n  ${cString(manifest.app.name)},\n  ykm_modules,\n  ${moduleRows.length}u,\n  ykm_sessions,\n  ${sessionRows.length}u,\n  ${assetRows.length ? "ykm_assets" : "0"},\n  ${assetRows.length}u,\n  ${permissions.length ? "ykm_permissions" : "0"},\n  ${permissions.length}u,\n  ${cString(manifest.app.version ?? "")},\n  ${cString(repository)}\n};\n\nconst YkmEmbeddedApp* ykm_get_embedded_app(void) { return &ykm_app; }\n`;
  await fs.writeFile(path.join(generated, "ykm_embedded_app.c"), embeddedSource);
  console.log(`Built ${normalized.sessions.length} sessions into ${output}`);
  return { root, manifest, exeName, permissions };
}

async function packageProject(projectArg, mode) {
  if (!["debug", "release"].includes(mode)) fail("mode must be debug or release");
  const { root, manifest, exeName, permissions } = await buildProject(projectArg);
  const buildir = path.join(root, ".ykm", "build", `windows-x64-msvc-${mode}`);
  const ykmApp = root.replaceAll("\\", "/");
  run("xmake", ["f", "-P", sdkRoot, "-p", "windows", "-a", "x64", "-m", mode, "-y", `--ykm_app=${ykmApp}`, `--buildir=${buildir.replaceAll("\\", "/")}`], sdkRoot);
  run("xmake", ["-P", sdkRoot], sdkRoot);
  const sourceExe = await findBuiltExe(buildir, mode, exeName);
  const outDir = path.join(root, ".ykm", "package", `windows-x64-msvc-${mode}`);
  await fs.rm(outDir, { recursive: true, force: true });
  await fs.mkdir(outDir, { recursive: true });
  const staged = [`${exeName}.exe`];
  await fs.copyFile(sourceExe, path.join(outDir, `${exeName}.exe`));
  const webpNotice = "libwebp-LICENSE.txt";
  await fs.copyFile(path.join(sdkRoot, "native", webpNotice), path.join(outDir, webpNotice));
  staged.push(webpNotice);
  const pdb = sourceExe.replace(/\.exe$/i, ".pdb");
  if (mode === "debug" && await exists(pdb)) {
    await fs.copyFile(pdb, path.join(outDir, `${exeName}.pdb`));
    staged.push(`${exeName}.pdb`);
  }
  if (process.env.YKM_SIGN_CERT) {
    const sign = ["sign", "/fd", "SHA256", "/f", process.env.YKM_SIGN_CERT, path.join(outDir, `${exeName}.exe`)];
    if (process.env.YKM_SIGN_PASSWORD) sign.splice(3, 0, "/p", process.env.YKM_SIGN_PASSWORD);
    run("signtool", sign, outDir);
  }
  const hashes = {};
  for (const name of staged) hashes[name] = await sha256File(path.join(outDir, name));
  await fs.writeFile(
    path.join(outDir, "SHA256SUMS.txt"),
    Object.entries(hashes).map(([name, hash]) => `${hash}  ${name}`).join("\n") + "\n",
  );
  staged.push("SHA256SUMS.txt");
  const version = manifest.app.version ?? "0.0.0";
  const zipName = `${exeName}-${version}-windows-x64-msvc-${mode}.zip`;
  const zipPath = path.join(path.dirname(outDir), zipName);
  await fs.rm(zipPath, { force: true });
  run("tar", ["-a", "-cf", zipName, ...staged], outDir);
  await fs.rename(path.join(outDir, zipName), zipPath);
  const zipHash = await sha256File(zipPath);
  await fs.writeFile(path.join(outDir, "ykm.package.json"), `${JSON.stringify({
    schemaVersion: 1,
    app: {
      id: manifest.app.id,
      name: manifest.app.name,
      version,
      executable: exeName,
    },
    target: "windows-x64-msvc",
    mode,
    permissions,
    files: staged,
    hashes,
    zip: { name: zipName, sha256: zipHash },
    signed: Boolean(process.env.YKM_SIGN_CERT),
  }, null, 2)}\n`);
  console.log(`Packed ${exeName} (${mode}) into ${outDir}`);
  console.log(zipPath);
}

async function createProject(projectArg) {
  const root = path.resolve(projectArg ?? ".");
  if (await exists(path.join(root, "ykm.json5"))) fail(`${root} already has ykm.json5`);
  const folder = path.basename(root);
  const exeName = /^[A-Za-z][A-Za-z0-9._-]{0,63}$/.test(folder) ? folder : "ykm-app";
  await fs.mkdir(path.join(root, "src"), { recursive: true });
  await fs.mkdir(path.join(root, "assets"), { recursive: true });
  await fs.writeFile(path.join(root, "assets", "app.ico"), createIco());
  await fs.writeFile(path.join(root, "ykm.json5"), `{
  schemaVersion: 1,
  app: {
    id: ${JSON.stringify(`dev.ykm.${exeName}`)},
    name: ${JSON.stringify(exeName)},
    version: "0.1.0",
    executable: ${JSON.stringify(exeName)},
    icon: "assets/app.ico",
  },
  runtime: { mode: "gui" },
  build: { language: "typescript", outDir: "dist", sourcemap: true, minify: false },
  sessions: [
    { name: "main", role: "main", entry: "src/main.ts" },
    { name: "ui", role: "ui", entry: "src/ui.ts" },
    { name: "cli", role: "cli", entry: "src/cli.ts" },
  ],
  native: { dynamicLibraries: [], staticLibraries: [] },
  permissions: ["fs.asset", "fs.data", "fs.cache"],
}
`);
  const typesRoot = path.relative(root, path.join(sdkRoot, "types", "ykm")).replaceAll("\\", "/");
  await fs.writeFile(path.join(root, "tsconfig.json"), `${JSON.stringify({
    compilerOptions: {
      target: "ES2020",
      module: "ESNext",
      moduleResolution: "Bundler",
      strict: true,
      noEmit: true,
      types: [path.isAbsolute(typesRoot) || typesRoot.startsWith(".") ? typesRoot : `./${typesRoot}`],
    },
    include: ["src/**/*.ts"],
  }, null, 2)}\n`);
  await fs.writeFile(path.join(root, "src", "main.ts"), `export function start(): void {
  ykm.sessions.start("ui");
}

start();
`);
  await fs.writeFile(path.join(root, "src", "ui.ts"), `const root = new ykm.ui.Window({ key: "main", title: ${JSON.stringify(exeName)}, width: 480, height: 240, padding: 12 });
root.append(new ykm.ui.Text({ key: "hello", text: ${JSON.stringify(exeName)} }));

export function render(): YkmControl {
  return root;
}
`);
  await fs.writeFile(path.join(root, "src", "cli.ts"), `export function main(args: string[]): number {
  ykm.print({ name: ${JSON.stringify(exeName)}, args });
  return 0;
}
`);
  console.log(`Created ${root}`);
}

async function devProject(projectArg) {
  const { root, manifest } = await buildProject(projectArg);
  const dist = path.resolve(root, manifest.build?.outDir ?? "dist");
  const hostBuild = path.join(sdkRoot, "build");
  run("xmake", ["f", "-P", sdkRoot, "-p", "windows", "-a", "x64", "-m", "debug", "-y", "--ykm_app=", `--buildir=${hostBuild.replaceAll("\\", "/")}`], sdkRoot);
  run("xmake", ["-P", sdkRoot], sdkRoot);
  const host = path.join(hostBuild, "windows", "x64", "debug", "ykm.exe");
  if (!await exists(host)) fail(`missing host ${host}`);
  run(host, [dist], sdkRoot);
}

const usage = "usage: node tools/ykm-app.mjs <create|dev|build|package> <project-directory> [--mode debug|release]";
const argv = process.argv.slice(2);
const command = argv[0];
const flags = {};
const positional = [];
for (let index = 1; index < argv.length; index += 1) {
  if (argv[index] === "--mode") flags.mode = argv[++index];
  else positional.push(argv[index]);
}
if (command === "create") {
  if (positional.length !== 1) fail(usage);
  await createProject(positional[0]);
} else if (command === "dev") {
  if (positional.length !== 1) fail(usage);
  await devProject(positional[0]);
} else if (command === "build") {
  if (positional.length !== 1) fail(usage);
  await buildProject(positional[0]);
} else if (command === "package") {
  if (positional.length !== 1) fail(usage);
  await packageProject(positional[0], flags.mode ?? "release");
} else {
  fail(usage);
}
