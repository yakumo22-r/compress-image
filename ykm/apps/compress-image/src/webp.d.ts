declare module "ykm:webp" {
  export function decode(bytes: Uint8Array): { width: number; height: number; data: Uint8Array };
  export function encode(rgba: Uint8Array, width: number, height: number, quality: number): Uint8Array;
}
