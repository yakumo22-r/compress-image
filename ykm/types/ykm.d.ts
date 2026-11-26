/// <reference path="./ykm-modules.d.ts" />
/// <reference path="./ykm-core.d.ts" />
/// <reference path="./ykm-sys-sdl.d.ts" />
/// <reference path="./ykm-ui-imgui.d.ts" />

export {};

declare global {
  interface YkmFonts {
    add(options: { name: string; file: string; size?: number }): boolean;
    list(): string[];
  }

  interface YkmApi extends YkmCoreApi, YkmSystemApi, YkmUiBackendApi {}

  enum YkmRelationType {
    Width = 1,
    Height = 2,
    Size = 3,
    LeftLeft = 4,
    LeftCenter = 5,
    LeftRight = 6,
    CenterCenter = 7,
    RightLeft = 8,
    RightRight = 9,
    TopTop = 10,
    TopMiddle = 11,
    TopBottom = 12,
    MiddleMiddle = 13,
    BottomTop = 14,
    BottomBottom = 15,
  }

  enum YkmWindowLayer {
    BG = 0,
    Page = 1,
    Modal = 2,
    System = 3,
    Debug = 4,
  }

  type YkmBindable<T> = T | YkmReactiveValue<T>;

  interface YkmReactiveValue<T> {
    readonly value: T;
    subscribe(listener: (value: T, previous: T) => void): () => void;
  }

  class YkmState<T> implements YkmReactiveValue<T> {
    constructor(value: T);
    value: T;
    set(value: T): void;
    update(updater: (value: T) => T): void;
    subscribe(listener: (value: T, previous: T) => void): () => void;
  }

  class YkmComputed<T> implements YkmReactiveValue<T> {
    constructor(compute: () => T, dependencies?: readonly YkmReactiveValue<unknown>[]);
    readonly value: T;
    subscribe(listener: (value: T, previous: T) => void): () => void;
  }

  interface YkmControlStyle {
    text?: string;
    frame?: string;
    frameHovered?: string;
    frameActive?: string;
    button?: string;
    buttonHovered?: string;
    buttonActive?: string;
    border?: string;
    accent?: string;
    frameRadius?: number;
    grabRadius?: number;
    borderWidth?: number;
    framePaddingY?: number;
  }

  interface YkmControlProps {
    key?: string;
    x?: YkmBindable<number>;
    y?: YkmBindable<number>;
    width?: YkmBindable<number>;
    height?: YkmBindable<number>;
    gap?: YkmBindable<number>;
    padding?: YkmBindable<number>;
    wrap?: YkmBindable<boolean>;
    /** Minimum outer size in pixels, read when the native Windows window is created. */
    minWidth?: YkmBindable<number>;
    minHeight?: YkmBindable<number>;
    /** On native Window, Page and Panel, hidden clips overflow without scrolling. */
    overflow?: YkmBindable<"visible" | "auto" | "scroll" | "hidden">;
    visible?: YkmBindable<boolean>;
    disabled?: YkmBindable<boolean>;
    font?: YkmBindable<string>;
    tooltip?: YkmBindable<string>;
    readOnly?: YkmBindable<boolean>;
    password?: YkmBindable<boolean>;
    maxLength?: YkmBindable<number>;
    layer?: YkmWindowLayer;
    menu?: boolean | YkmMenuItem[];
    background?: YkmBindable<YkmBackground>;
    /** Windows native Window background, using an asset:/ image cropped to fill. */
    backgroundImage?: YkmBindable<string>;
    /** Optional white overlay over the window image, from 0 to 1. */
    backgroundVeil?: YkmBindable<number>;
    /** Native Window control palette and frame geometry, scoped to its contents. Colors are hex. */
    controlStyle?: YkmBindable<YkmControlStyle>;
    /** Center a single-line Text within its width. */
    textAlign?: YkmBindable<"center">;
    [name: string]: unknown;
  }

  interface YkmMenuItem {
    label: string;
    shortcut?: string;
    enabled?: boolean;
    items?: YkmMenuItem[];
    onClick?: () => void;
  }

  interface YkmRelationOptions {
    percent?: boolean;
    offset?: number;
  }

  type YkmControlChild = YkmControl | string | number | null | undefined | false | YkmControlChild[];

  class YkmControl {
    constructor(type: string, props?: YkmControlProps);
    readonly key: string;
    readonly type: string;
    readonly parent: YkmControl | null;
    readonly tree: YkmControlTree | null;
    readonly props: YkmControlProps;
    readonly children: YkmControl[];
    relations: Array<{ target: string; type: YkmRelationType; percent: boolean; offset: number }>;
    get<K extends string>(name: K): unknown;
    set(name: string, value: unknown): this;
    patch(props: YkmControlProps): this;
    setLayout(layout: YkmControlProps): this;
    setLayer(layer: YkmWindowLayer): this;
    setMenu(menu: boolean | YkmMenuItem[]): this;
    setBackground(background: YkmBindable<YkmBackground>): this;
    append(...children: YkmControlChild[]): this;
    prepend(...children: YkmControlChild[]): this;
    insert(index: number, child: YkmControlChild): this;
    replace(current: YkmControl | string, replacement: YkmControlChild): YkmControl | null;
    removeChild(childOrKey: YkmControl | string): YkmControl | null;
    removeAt(index: number): YkmControl | null;
    clear(): YkmControl[];
    childAt(index: number): YkmControl | null;
    findChild(key: string): YkmControl | null;
    addRelation(target: YkmControl | string, type: YkmRelationType, options?: YkmRelationOptions): this;
    removeRelation(target: YkmControl | string, type?: YkmRelationType): this;
    clearRelations(): this;
    remove(): this;
    dispose(): void;
  }

  class YkmControlTree {
    constructor(root?: YkmControl);
    readonly root: YkmControl | null;
    readonly revision: number;
    setRoot(root: YkmControl): this;
    find(key: string): YkmControl | null;
    has(key: string): boolean;
    transaction(callback: () => void): void;
    validate(): ReadonlyArray<{ code: string; message: string }>;
    dispose(): void;
  }

  class YkmTypedControl<P extends YkmControlProps = YkmControlProps> extends YkmControl {
    constructor(props?: P, children?: YkmControlChild | YkmControlChild[]);
  }

  class YkmInspect<T = unknown> extends YkmControl {
    constructor(type: string, props?: YkmControlProps);
    readonly path: string;
    readonly dirty: YkmState<boolean>;
    readonly errors: readonly string[];
    document: T;
    validate(): readonly string[];
    bind(state: YkmState<T>): this;
    load(source: string): this;
    openFile(): this;
    save(): this;
    saveAs(): this;
    readonly fileMenu: YkmMenuItem;
    installShortcuts(): void;
  }

  interface YkmInspectSchema {
    type?: "object" | "array" | "string" | "number" | "boolean" | "null";
    title?: string;
    description?: string;
    enum?: readonly (string | number | boolean)[];
    properties?: { [name: string]: YkmInspectSchema };
    items?: YkmInspectSchema;
    additionalProperties?: boolean | YkmInspectSchema;
    default?: YkmCloneValue;
    required?: readonly string[];
    min?: number;
    max?: number;
    minLength?: number;
    maxLength?: number;
    readOnly?: boolean;
  }

  interface YkmChoiceOption {
    value: string;
    label?: string;
  }

  interface YkmTreeNode {
    id: string;
    label?: string;
    children?: readonly YkmTreeNode[];
    expanded?: boolean;
    disabled?: boolean;
    menu?: YkmMenuItem[];
    onDoubleClick?: (id: string) => void;
    onActivate?: (id: string) => void;
  }

  interface YkmTreeProps extends YkmControlProps {
    nodes?: readonly YkmTreeNode[];
    selected?: YkmBindable<string>;
    onSelect?: (id: string) => void;
    onToggle?: (event: { id: string; expanded: boolean }) => void;
  }

  class YkmJInspect extends YkmInspect<YkmCloneValue> {
    constructor(props?: YkmControlProps & {
      value?: YkmCloneValue;
      schema?: YkmInspectSchema;
      onChange?: (value: YkmCloneValue) => void;
    });
  }

  class YkmXmlInspect extends YkmInspect<unknown> {}
  class YkmYamlInspect extends YkmInspect<unknown> {}
  class YkmTomlInspect extends YkmInspect<unknown> {}

  type YkmColor = `#${string}`;

  interface YkmShapePaint {
    fill?: YkmColor;
    stroke?: YkmColor;
    strokeWidth?: number;
    opacity?: number;
  }

  interface YkmRectCommand extends YkmShapePaint {
    kind?: "rect" | "roundedRect";
    x: number;
    y: number;
    width: number;
    height: number;
    radius?: number;
  }

  interface YkmCircleCommand extends YkmShapePaint {
    kind?: "circle";
    x: number;
    y: number;
    radius: number;
  }

  interface YkmEllipseCommand extends YkmShapePaint {
    kind?: "ellipse";
    x: number;
    y: number;
    radiusX: number;
    radiusY: number;
  }

  interface YkmLineCommand extends Omit<YkmShapePaint, "fill"> {
    kind?: "line";
    x1: number;
    y1: number;
    x2: number;
    y2: number;
  }

  type YkmVectorCommand =
    | (YkmRectCommand & { kind: "rect" | "roundedRect" })
    | (YkmCircleCommand & { kind: "circle" })
    | (YkmEllipseCommand & { kind: "ellipse" })
    | (YkmLineCommand & { kind: "line" });

  type YkmDrawing = YkmVectorDrawing | readonly YkmVectorCommand[];

  type YkmBackground = YkmColor | {
    fill?: YkmColor;
    stroke?: YkmColor;
    strokeWidth?: number;
    radius?: number;
    opacity?: number;
    drawing?: YkmDrawing;
  };

  interface YkmGraphicsProps extends YkmControlProps {
    drawing?: YkmBindable<YkmDrawing>;
  }

  class YkmVectorDrawing {
    commands: YkmVectorCommand[];
    rect(options: YkmRectCommand): this;
    roundedRect(options: YkmRectCommand): this;
    circle(options: YkmCircleCommand): this;
    ellipse(options: YkmEllipseCommand): this;
    line(options: YkmLineCommand): this;
    polyline(options: Record<string, unknown>): this;
    polygon(options: Record<string, unknown>): this;
    path(options: Record<string, unknown>): this;
    fill(paint: unknown): this;
    stroke(paint: unknown, width?: number): this;
  }

  interface YkmUiApi {
    readonly Control: typeof YkmControl;
    readonly ControlTree: typeof YkmControlTree;
    readonly VectorDrawing: typeof YkmVectorDrawing;
    readonly Root: typeof YkmTypedControl;
    readonly Window: typeof YkmTypedControl;
    readonly Page: typeof YkmTypedControl;
    readonly Modal: typeof YkmTypedControl;
    readonly Row: typeof YkmTypedControl;
    readonly Column: typeof YkmTypedControl;
    readonly Group: typeof YkmTypedControl;
    readonly Panel: typeof YkmTypedControl;
    readonly Graphics: typeof YkmTypedControl<YkmGraphicsProps>;
    readonly Text: typeof YkmTypedControl;
    readonly Button: typeof YkmTypedControl;
    readonly Checkbox: typeof YkmTypedControl;
    readonly Input: typeof YkmTypedControl;
    readonly Slider: typeof YkmTypedControl;
    readonly Separator: typeof YkmTypedControl;
    readonly Select: typeof YkmTypedControl;
    readonly Radio: typeof YkmTypedControl;
    readonly Tabs: typeof YkmTypedControl;
    readonly List: typeof YkmTypedControl;
    readonly Scroll: typeof YkmTypedControl;
    readonly Progress: typeof YkmTypedControl;
    readonly TextArea: typeof YkmTypedControl;
    readonly NumberInput: typeof YkmTypedControl;
    readonly Image: typeof YkmTypedControl;
    readonly Tree: typeof YkmTypedControl<YkmTreeProps>;
    readonly TypeBadge: typeof YkmTypedControl;
    readonly Disclosure: typeof YkmTypedControl;
    readonly IconButton: typeof YkmTypedControl;
    readonly TreeIndent: typeof YkmTypedControl;
    readonly Inspect: typeof YkmInspect;
    readonly JInspect: typeof YkmJInspect;
    readonly XmlInspect: typeof YkmXmlInspect;
    readonly YamlInspect: typeof YkmYamlInspect;
    readonly TomlInspect: typeof YkmTomlInspect;
  }

  interface YkmPathObject {
    root: string;
    dir: string;
    base: string;
    ext: string;
    name: string;
  }

  interface YkmPath {
    readonly sep: string;
    readonly delimiter: string;
    readonly win32: YkmPath;
    readonly posix: YkmPath;
    join(...parts: string[]): string;
    resolve(...parts: string[]): string;
    normalize(path: string): string;
    dirname(path: string): string;
    basename(path: string, ext?: string): string;
    extname(path: string): string;
    isAbsolute(path: string): boolean;
    relative(from: string, to: string): string;
    parse(path: string): YkmPathObject;
    format(pathObject: Partial<YkmPathObject>): string;
  }

  interface YkmFsStats {
    size: number;
    mtimeMs: number;
    isFile(): boolean;
    isDirectory(): boolean;
  }

  interface YkmFsPromises {
    readFile(path: string, encoding: "utf8" | "utf-8"): Promise<string>;
    readFile(path: string, encoding?: string | { encoding?: string }): Promise<string | Uint8Array>;
    writeFile(path: string, data: string | Uint8Array): Promise<void>;
    appendFile(path: string, data: string | Uint8Array): Promise<void>;
    mkdir(path: string, options?: { recursive?: boolean }): Promise<void>;
    readdir(path: string): Promise<string[]>;
    stat(path: string): Promise<YkmFsStats>;
    unlink(path: string): Promise<void>;
    rm(path: string, options?: { recursive?: boolean; force?: boolean }): Promise<void>;
    access(path: string): Promise<void>;
  }

  interface YkmFs extends YkmFsPromises {
    readonly promises: YkmFsPromises;
    readFileSync(path: string, encoding: "utf8" | "utf-8"): string;
    readFileSync(path: string, encoding?: string | { encoding?: string }): string | Uint8Array;
    writeFileSync(path: string, data: string | Uint8Array): void;
    appendFileSync(path: string, data: string | Uint8Array): void;
    mkdirSync(path: string, options?: { recursive?: boolean }): void;
    readdirSync(path: string): string[];
    statSync(path: string): YkmFsStats;
    unlinkSync(path: string): void;
    rmSync(path: string, options?: { recursive?: boolean; force?: boolean }): void;
    existsSync(path: string): boolean;
    accessSync(path: string): void;
  }

  const ykm: YkmApi;

  function require(id: "fs" | "node:fs"): YkmFs;
  function require(id: "fs/promises" | "node:fs/promises"): YkmFsPromises;
  function require(id: "path" | "node:path" | "path/win32" | "path/posix"): YkmPath;
  function require(id: string): unknown;

  function setTimeout(callback: () => void, delay?: number): number;
  function setInterval(callback: () => void, delay?: number): number;
  function clearTimeout(id: number): void;
  function clearInterval(id: number): void;
  function queueMicrotask(callback: () => void): void;
}
