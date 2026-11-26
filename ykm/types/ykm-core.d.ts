export {};

declare global {
  type YkmCloneValue = null | boolean | number | string | YkmCloneValue[] | { [key: string]: YkmCloneValue };

  interface YkmSessions {
    start(name: string): boolean;
    /** Request a worker to stop; does not wait for its thread to exit. */
    stop(name: string): boolean;
    isRunning(name: string): boolean;
    list(): string[];
  }

  interface YkmCoreApi {
    readonly sessionId: string;
    readonly args: readonly string[];
    readonly sessions: YkmSessions;
    readonly logging: {
      enable(path: string): string;
      disable(): void;
      status(): { enabled: boolean; path: string | null };
    };
    send(targetSessionId: string, payload: YkmCloneValue): void;
    print(...values: unknown[]): void;
    error(...values: unknown[]): void;
  }
}
