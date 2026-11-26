export {};

declare global {
  interface YkmUiBackendApi {
    readonly fonts: YkmFonts;
    readonly ui: YkmUiApi;
    readonly Relation: typeof YkmRelationType;
    readonly Layer: typeof YkmWindowLayer;
    state<T>(value: T): YkmState<T>;
    computed<T>(compute: () => T, dependencies?: readonly YkmReactiveValue<unknown>[]): YkmComputed<T>;
  }
}
