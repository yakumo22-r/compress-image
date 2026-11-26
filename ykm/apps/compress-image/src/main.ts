import fs from "fs";

for (const path of ["data:/input", "data:/output"]) {
  try { fs.mkdirSync(path, { recursive: true }); }
  catch (error) { ykm.error("无法创建默认目录", error); }
}
ykm.sessions.start("ui");
