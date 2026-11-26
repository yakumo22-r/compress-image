// Codec dependencies construct these at import; pixel-only paths do not use them.
for (const name of ["TextEncoder", "TextDecoder"] as const) {
  if (typeof globalThis[name] === "undefined") {
    Object.defineProperty(globalThis, name, {
      configurable: true,
      value: class {
        encode(): never { throw new Error("PNG 文本编码不可用"); }
        decode(): never { throw new Error("PNG 文本解码不可用"); }
      },
    });
  }
}
