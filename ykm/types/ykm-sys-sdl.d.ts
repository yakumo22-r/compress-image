export {};

declare global {
  interface YkmImageImport {
    /** App-private import directory, or empty when nothing was imported. */
    readonly directory: string;
    readonly sources: readonly string[];
    readonly imported: number;
    readonly skipped: number;
  }
  interface YkmFileDropEvent extends Event, YkmImageImport {}
  interface WindowEventMap { filedrop: YkmFileDropEvent; }
  function addEventListener(type: "filedrop", listener: (event: YkmFileDropEvent) => void): void;

  interface YkmSystemApi {
    readonly dialog: {
      pickDirectory(): string | null;
      /** Windows only; requires desktop.filedrop and fs.data. */
      pickImageDirectory(): YkmImageImport | null;
      pickImage(): YkmImageImport | null;
      pickImageOutput(): string | null;
      imageDataDirectory(): string;
      /** Derive output from the current native import, or reset for data:/input. */
      autoImageOutput(inputBatch: string): string;
      /** Publish directly to the authorized output directory. Returns that directory. */
      publishImages(batch: string, inputBatch: string, replaceExisting: boolean): string;
      openFile(): { path: string; content: string } | null;
      saveFile(content: string, path?: string | null): string | null;
    };
    readonly tools: {
      package(
        project: string,
        options?: "debug" | "release" | { mode?: "debug" | "release" },
      ): Promise<{ ok: boolean; log: string }>;
      create(project: string): Promise<{ ok: boolean; log: string }>;
      openFolder(path: string): void;
    };
  }
}
