declare module "fs" {
  const fs: YkmFs;
  export = fs;
}

declare module "fs/promises" {
  const fs: YkmFsPromises;
  export = fs;
}

declare module "path" {
  const path: YkmPath;
  export = path;
}
