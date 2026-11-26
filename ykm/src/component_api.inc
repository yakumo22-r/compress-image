R"YKMJS(
(() => {
  let nextKey = 1;
  Object.freeze(ykm.Relation);
  Object.freeze(ykm.Layer);

  function isReactive(value) {
    return value != null && typeof value === "object" &&
      typeof value.subscribe === "function" && "value" in value;
  }

  function asArray(value) {
    if (value == null) return [];
    return Array.isArray(value) ? value : [value];
  }

  class State {
    constructor(value) {
      this._value = value;
      this._listeners = [];
    }
    get value() { return this._value; }
    set value(value) { this.set(value); }
    set(value) {
      if (Object.is(this._value, value)) return;
      const previous = this._value;
      this._value = value;
      for (const listener of this._listeners.slice()) listener(value, previous);
    }
    update(updater) { this.set(updater(this._value)); }
    subscribe(listener) {
      if (typeof listener !== "function") throw new TypeError("subscribe requires a function");
      this._listeners.push(listener);
      return () => {
        const index = this._listeners.indexOf(listener);
        if (index >= 0) this._listeners.splice(index, 1);
      };
    }
  }

  class Computed {
    constructor(compute, dependencies = []) {
      if (typeof compute !== "function") throw new TypeError("computed requires a function");
      this._compute = compute;
      this._listeners = [];
      this._value = compute();
      for (const dependency of dependencies) {
        if (!isReactive(dependency)) throw new TypeError("computed dependencies must be reactive");
        dependency.subscribe(() => {
          const next = this._compute();
          if (Object.is(next, this._value)) return;
          const previous = this._value;
          this._value = next;
          for (const listener of this._listeners.slice()) listener(next, previous);
        });
      }
    }
    get value() { return this._value; }
    subscribe(listener) {
      if (typeof listener !== "function") throw new TypeError("subscribe requires a function");
      this._listeners.push(listener);
      return () => {
        const index = this._listeners.indexOf(listener);
        if (index >= 0) this._listeners.splice(index, 1);
      };
    }
  }

  class Control {
    constructor(type, props = {}) {
      if (typeof type !== "string" || !type) throw new TypeError("control type must be a non-empty string");
      if (props == null || typeof props !== "object") throw new TypeError("props must be an object");
      this.__ykmNodeVersion = 1;
      this.type = type;
      this.props = {};
      this.children = [];
      this.relations = [];
      this.parent = null;
      this.tree = null;
      this._unsubs = Object.create(null);
      for (const key of Object.keys(props)) this._bind(key, props[key], false);
      if (this.props.key == null) this.props.key = `ykm-${nextKey++}`;
      else this.props.key = String(this.props.key);
    }

    get key() { return this.props.key; }

    get(name) { return this.props[name]; }

    set(name, value) {
      this._bind(name, value, true);
      return this;
    }

    patch(props) {
      if (props == null || typeof props !== "object") throw new TypeError("patch requires an object");
      for (const key of Object.keys(props)) this._bind(key, props[key], true);
      return this;
    }

    setLayout(layout) { return this.patch(layout); }

    setLayer(layer) {
      if (this.type !== "window") throw new TypeError("setLayer applies only to Window");
      if (!Number.isInteger(layer) || layer < ykm.Layer.BG || layer > ykm.Layer.Debug) {
        throw new TypeError("layer must be a ykm.Layer enum value");
      }
      return this.set("layer", layer);
    }

    setMenu(menu) {
      if (this.type !== "window") throw new TypeError("setMenu applies only to Window");
      if (menu !== false && menu !== true && !Array.isArray(menu)) {
        throw new TypeError("menu must be boolean or an array of items");
      }
      return this.set("menu", menu);
    }

    setBackground(background) {
      return this.set("background", background);
    }

    _bind(name, value, notify) {
      if (name === "key" && this.props.key != null && String(value) !== String(this.props.key)) {
        throw new TypeError("control key is immutable");
      }
      const current = this._unsubs[name];
      if (current) {
        current();
        delete this._unsubs[name];
      }
      if (isReactive(value)) {
        this.props[name] = value.value;
        this._unsubs[name] = value.subscribe((next) => {
          this.props[name] = next;
          if (this.tree) this.tree._notify();
        });
      } else {
        this.props[name] = value;
      }
      if (notify && this.tree) this.tree._notify();
    }

    append(...items) {
      for (const item of items) {
        if (Array.isArray(item)) this.append(...item);
        else this.insert(this.children.length, item);
      }
      return this;
    }

    prepend(...items) {
      let index = 0;
      for (const item of items) {
        if (Array.isArray(item)) {
          for (const child of item) {
            this.insert(index, child);
            if (normalizeChild(child, false)) index += 1;
          }
        } else {
          const before = this.children.length;
          this.insert(index, item);
          if (this.children.length > before) index += 1;
        }
      }
      return this;
    }

    insert(index, item) {
      const child = normalizeChild(item, true);
      if (child == null) return this;
      if (child.parent) {
        throw new TypeError(`control '${child.key}' already has a parent`);
      }
      for (let node = this; node; node = node.parent) {
        if (node === child) throw new TypeError("cycle in control tree");
      }
      if (this.tree) this.tree._assertAttach(child);
      index = Math.max(0, Math.min(this.children.length, Number(index) || 0));
      this.children.splice(index, 0, child);
      child.parent = this;
      if (this.tree) this.tree._attach(child);
      else child._setTree(null);
      return this;
    }

    replace(current, replacement) {
      const index = this._indexOf(current);
      if (index < 0) return null;
      const removed = this.removeAt(index);
      this.insert(index, replacement);
      return removed;
    }

    removeChild(childOrKey) {
      const index = this._indexOf(childOrKey);
      if (index < 0) return null;
      return this.removeAt(index);
    }

    removeAt(index) {
      index = Number(index);
      if (!Number.isInteger(index) || index < 0 || index >= this.children.length) return null;
      const child = this.children.splice(index, 1)[0];
      child.parent = null;
      if (this.tree) this.tree._detach(child);
      else child._setTree(null);
      return child;
    }

    clear() {
      const removed = this.children.slice();
      this.children.length = 0;
      for (const child of removed) {
        child.parent = null;
        if (this.tree) this.tree._detach(child);
        else child._setTree(null);
      }
      return removed;
    }

    childAt(index) {
      return this.children[index] ?? null;
    }

    findChild(key) {
      return this.children.find((child) => child.key === key) ?? null;
    }

    addRelation(target, type, options = {}) {
      const targetKey = typeof target === "string" ? target : target?.key;
      if (!targetKey) throw new TypeError("relation target requires a stable key");
      if (!Number.isInteger(type) || type < ykm.Relation.Width || type > ykm.Relation.BottomBottom) {
        throw new TypeError("relation type must be a ykm.Relation enum value");
      }
      const percent = typeof options === "boolean" ? options : Boolean(options.percent);
      const offset = typeof options === "boolean" ? 0 : (Number(options.offset) || 0);
      const relation = { target: targetKey, type, percent, offset };
      const existing = this.relations.findIndex((item) => item.target === targetKey && item.type === type);
      if (existing >= 0) this.relations[existing] = relation;
      else this.relations.push(relation);
      if (this.tree) this.tree._notify();
      return this;
    }

    removeRelation(target, type) {
      const targetKey = typeof target === "string" ? target : target?.key;
      this.relations = this.relations.filter((item) =>
        item.target !== targetKey || (type != null && item.type !== type));
      if (this.tree) this.tree._notify();
      return this;
    }

    clearRelations() {
      this.relations.length = 0;
      if (this.tree) this.tree._notify();
      return this;
    }

    remove() {
      if (this.parent) this.parent.removeChild(this);
      return this;
    }

    dispose() {
      for (const name of Object.keys(this._unsubs)) {
        this._unsubs[name]();
        delete this._unsubs[name];
      }
      for (const child of this.children.slice()) child.dispose();
      this.remove();
    }

    _indexOf(childOrKey) {
      return typeof childOrKey === "string"
        ? this.children.findIndex((child) => child.key === childOrKey)
        : this.children.indexOf(childOrKey);
    }

    _setTree(tree) {
      this.tree = tree;
      for (const child of this.children) child._setTree(tree);
    }

    _collect(out) {
      out.push(this);
      for (const child of this.children) child._collect(out);
    }
  }

  function normalizeChild(child, required) {
    if (child == null || child === false || child === true) {
      if (required === false) return null;
      return null;
    }
    if (typeof child === "string" || typeof child === "number") {
      return new Text({ text: String(child) });
    }
    if (!(child instanceof Control)) {
      throw new TypeError("child must be a Control or text value");
    }
    return child;
  }

  function defineControl(type) {
    return class extends Control {
      constructor(props = {}, children) {
        super(type, props);
        if (children != null) this.append(...asArray(children));
      }
    };
  }

)YKMJS"
R"YKMJS(

  class VectorDrawing {
    constructor() { this.commands = []; }
    rect(options) { this.commands.push({ kind: "rect", ...options }); return this; }
    roundedRect(options) { this.commands.push({ kind: "roundedRect", ...options }); return this; }
    circle(options) { this.commands.push({ kind: "circle", ...options }); return this; }
    ellipse(options) { this.commands.push({ kind: "ellipse", ...options }); return this; }
    line(options) { this.commands.push({ kind: "line", ...options }); return this; }
    polyline(options) { this.commands.push({ kind: "polyline", ...options }); return this; }
    polygon(options) { this.commands.push({ kind: "polygon", ...options }); return this; }
    path(options) { this.commands.push({ kind: "path", ...options }); return this; }
    fill(paint) { this.commands.push({ kind: "fill", paint }); return this; }
    stroke(paint, width) { this.commands.push({ kind: "stroke", paint, width }); return this; }
  }

  const Root = defineControl("root");
  const Window = defineControl("window");
  const Page = defineControl("page");
  const Modal = defineControl("modal");
  const Row = defineControl("row");
  const Column = defineControl("column");
  const Group = defineControl("group");
  const Panel = defineControl("panel");
  const Graphics = defineControl("graphics");
  const Text = defineControl("text");
  const Button = defineControl("button");
  const Checkbox = defineControl("checkbox");
  const Input = defineControl("input");
  const Slider = defineControl("slider");
  const Separator = defineControl("separator");
  const Select = defineControl("select");
  const Radio = defineControl("radio");
  const Tabs = defineControl("tabs");
  const List = defineControl("list");
  const Scroll = defineControl("scroll");
  const Progress = defineControl("progress");
  const TextArea = defineControl("textArea");
  const NumberInput = defineControl("numberInput");
  const Image = defineControl("image");
  const Tree = defineControl("tree");
  const TypeBadge = defineControl("typeBadge");
  const Disclosure = defineControl("disclosure");
  const IconButton = defineControl("iconButton");
  const TreeIndent = defineControl("treeIndent");

  class ControlTree {
    constructor(root) {
      this.__ykmTreeVersion = 1;
      this.revision = 0;
      this._index = new Map();
      this._transactionDepth = 0;
      this._dirty = false;
      this._scheduled = false;
      this.root = null;
      if (root) this.setRoot(root);
    }

    setRoot(root) {
      if (!(root instanceof Control)) throw new TypeError("ControlTree root must be a Control");
      if (root.parent) throw new TypeError("ControlTree root cannot have a parent");
      if (root.tree && root.tree !== this) throw new TypeError("control belongs to another ControlTree");
      if (root === this.root) return this;
      this._assertAttach(root, this.root);
      if (this.root) {
        const previous = this.root;
        this.root = null;
        this._detach(previous);
      }
      this.root = root;
      this._attach(root);
      return this;
    }

    find(key) { return this._index.get(key) ?? null; }
    has(key) { return this._index.has(key); }

    transaction(callback) {
      if (typeof callback !== "function") throw new TypeError("transaction requires a function");
      this._transactionDepth += 1;
      try { callback(); }
      finally {
        this._transactionDepth -= 1;
        if (this._transactionDepth === 0) this._flush();
      }
    }

    validate() {
      const diagnostics = [];
      const seen = new Map();
      if (!this.root) {
        diagnostics.push({ code: "empty", message: "tree has no root" });
        return diagnostics;
      }
      const nodes = [];
      this.root._collect(nodes);
      for (const node of nodes) {
        if (seen.has(node.key)) {
          diagnostics.push({ code: "duplicate-key", message: `duplicate key '${node.key}'` });
        } else {
          seen.set(node.key, node);
        }
      }
      return diagnostics;
    }

    dispose() {
      const root = this.root;
      this.root = null;
      if (root) {
        this._detach(root);
        root.dispose();
      }
      this._index.clear();
    }

    _assertAttach(node, ignoredRoot = null) {
      const nodes = [];
      const keys = new Set();
      const ignoredNodes = [];
      node._collect(nodes);
      if (ignoredRoot) ignoredRoot._collect(ignoredNodes);
      const ignored = new Set(ignoredNodes);
      for (const item of nodes) {
        if (keys.has(item.key)) throw new TypeError(`duplicate control key '${item.key}'`);
        keys.add(item.key);
        const existing = this._index.get(item.key);
        if (existing && existing !== item && !ignored.has(existing)) {
          throw new TypeError(`duplicate control key '${item.key}'`);
        }
      }
    }

    _attach(node) {
      const nodes = [];
      node._collect(nodes);
      for (const item of nodes) {
        this._index.set(item.key, item);
        item.tree = this;
      }
      this._notify();
    }

    _detach(node) {
      const nodes = [];
      node._collect(nodes);
      for (const item of nodes) {
        if (this._index.get(item.key) === item) this._index.delete(item.key);
        item.tree = null;
      }
      this._notify();
    }

    _notify() {
      this._dirty = true;
      if (this._transactionDepth > 0 || this._scheduled) return;
      this._scheduled = true;
      queueMicrotask(() => {
        this._scheduled = false;
        this._flush();
      });
    }

    _flush() {
      if (!this._dirty) return;
      this._dirty = false;
      this._bump();
    }

    _bump() { this.revision += 1; }
  }

  class Inspect extends Control {
    constructor(type, props = {}) {
      super(type, props);
      this._path = "";
      this._dirty = new State(false);
      this._document = props.value !== undefined ? props.value : (props.document !== undefined ? props.document : null);
    }

    get path() { return this._path; }
    get dirty() { return this._dirty; }
    get document() { return this._document; }
    set document(value) {
      this._document = value;
      this._dirty.set(true);
      this.rebuild();
      this._emitChange();
    }

    bind(state) {
      if (!isReactive(state)) throw new TypeError("Inspect.bind requires a State");
      this.document = state.value;
      state.subscribe((value) => {
        if (value !== this._document) {
          this._document = value;
          this.rebuild();
        }
      });
      return this;
    }

    load(source) {
      this._document = this.parse(source);
      this._applyDefaults(this.props.schema, this._document);
      this._dirty.set(false);
      this.rebuild();
      this._emitChange();
      return this;
    }

    openFile() {
      const selected = ykm.dialog.openFile();
      if (!selected) return this;
      try {
        this._document = this.parse(selected.content);
        this._path = selected.path;
        this._dirty.set(false);
        this.rebuild();
        this._emitChange();
      } catch (error) {
        this._onError(error instanceof Error ? error.message : String(error));
      }
      return this;
    }

    save() {
      if (!this._path) return this.saveAs();
      this._write(this._path);
      return this;
    }

    saveAs() {
      const path = ykm.dialog.saveFile(this.serialize(this._document));
      if (path) {
        this._path = path;
        this._dirty.set(false);
        this._onSaved(path);
      }
      return this;
    }

    get fileMenu() {
      return {
        label: "文件",
        items: [
          { label: "打开", shortcut: "Ctrl+O", onClick: () => this.openFile() },
          { label: "保存", shortcut: "Ctrl+S", onClick: () => this.save() },
          { label: "另存为", shortcut: "Ctrl+Shift+S", onClick: () => this.saveAs() },
        ],
      };
    }

    installShortcuts() {
      addEventListener("keydown", (event) => {
        if (!event.ctrlKey || event.altKey || event.repeat) return;
        const key = event.key.toLowerCase();
        if (key === "o" && !event.shiftKey) this.openFile();
        else if (key === "s" && event.shiftKey) this.saveAs();
        else if (key === "s") this.save();
      });
    }

    parse() { throw new Error(`${this.constructor.name} is not implemented`); }
    serialize() { throw new Error(`${this.constructor.name} is not implemented`); }
    rebuild() {}
    _onSaved() {}
    _onError() {}
    _emitChange() {
      const callback = this.props.onChange;
      if (typeof callback === "function") callback(this._document);
    }
    _write(path) {
      const saved = ykm.dialog.saveFile(this.serialize(this._document), path);
      if (saved) {
        this._dirty.set(false);
        this._onSaved(saved);
      }
    }
  }

)YKMJS"
R"YKMJS(

  class JInspect extends Inspect {
    constructor(props = {}) {
      super("jinspect", props);
      this._collapsed = new Set();
      this._errors = [];
      if (this._document == null) this._document = {};
      this._applyDefaults(this.props.schema, this._document);
      this.rebuild();
    }

    parse(source) { return JSON.parse(source); }
    serialize(document) { return `${JSON.stringify(document, null, 2)}\n`; }

    _applyDefaults(schema, value) {
      if (!schema || typeof schema !== "object") return value;
      if (value === undefined && schema.default !== undefined) value = schema.default;
      if (schema.type === "object" && schema.properties) {
        if (!value || typeof value !== "object" || Array.isArray(value)) value = {};
        for (const key of Object.keys(schema.properties)) {
          const childSchema = schema.properties[key];
          if (!Object.prototype.hasOwnProperty.call(value, key) && childSchema && childSchema.default !== undefined) {
            value[key] = childSchema.default;
          }
          if (Object.prototype.hasOwnProperty.call(value, key)) {
            value[key] = this._applyDefaults(childSchema, value[key]);
          }
        }
      } else if (schema.type === "array" && schema.items && Array.isArray(value)) {
        value.forEach((child, index) => { value[index] = this._applyDefaults(schema.items, child); });
      }
      return value;
    }

    get errors() { return this._errors.slice(); }

    _schemaTypeMatches(type, value) {
      if (value === undefined) return true;
      if (type === "null") return value === null;
      if (type === "array") return Array.isArray(value);
      if (type === "object") return value !== null && typeof value === "object" && !Array.isArray(value);
      if (type === "number") return typeof value === "number" && Number.isFinite(value);
      return typeof value === type;
    }

    validate() {
      const errors = [];
      const errorsByPath = {};
      const addError = (path, message) => {
        const error = `${path || "根"}: ${message}`;
        errors.push(error);
        const key = path || "root";
        (errorsByPath[key] || (errorsByPath[key] = [])).push(message);
      };
      const visit = (schema, value, path) => {
        if (!schema || typeof schema !== "object") return;
        const label = path || "根";
        if (schema.required && schema.type === "object") {
          for (const key of schema.required) {
            if (!value || typeof value !== "object" || !Object.prototype.hasOwnProperty.call(value, key)) {
              addError(`${label === "根" ? "根" : label}.${key}`, "必填");
            }
          }
        }
        if (schema.enum && !schema.enum.some((item) => Object.is(item, value))) {
           addError(label, `必须是 ${schema.enum.join(", ")}`);
        }
        if (schema.type && !this._schemaTypeMatches(schema.type, value)) {
          addError(label, `类型必须是 ${schema.type}`);
          return;
        }
        if (schema.type === "number" && typeof value === "number") {
          if (schema.min !== undefined && value < schema.min) addError(label, `不能小于 ${schema.min}`);
          if (schema.max !== undefined && value > schema.max) addError(label, `不能大于 ${schema.max}`);
        }
        if (schema.type === "string" && typeof value === "string") {
          if (schema.minLength !== undefined && value.length < schema.minLength) addError(label, `长度不能小于 ${schema.minLength}`);
          if (schema.maxLength !== undefined && value.length > schema.maxLength) addError(label, `长度不能大于 ${schema.maxLength}`);
        }
        if (schema.type === "object" && schema.properties) {
          for (const key of Object.keys(schema.properties)) {
            const child = value && typeof value === "object" ? value[key] : undefined;
            visit(schema.properties[key], child, `${label}.${key}`);
          }
        } else if (schema.type === "array" && schema.items && Array.isArray(value)) {
          value.forEach((child, index) => visit(schema.items, child, `${label}[${index}]`));
        }
      };
      visit(this.props.schema, this._document, "");
      this._errors = errors;
      this._errorsByPath = errorsByPath;
      return errors.slice();
    }

    rebuild() {
      this.clear();
      this.validate();
      if (this._errors.length) {
        this.append(new Text({ key: "schema-errors", wrap: true, text: `校验错误：${this._errors.join("；")}` }));
      }
      this.append(this._node([], "根", this._document, true));
    }

    _schemaAt(path) {
      let schema = this.props.schema;
      if (schema == null || typeof schema !== "object") return null;
      for (const key of path) {
        if (schema.type === "array") schema = schema.items || null;
        else if (schema.properties && Object.prototype.hasOwnProperty.call(schema.properties, key)) {
          schema = schema.properties[key];
        } else if (schema.additionalProperties && typeof schema.additionalProperties === "object") {
          schema = schema.additionalProperties;
        } else return null;
        if (schema == null || typeof schema !== "object") return null;
      }
      return schema;
    }

    _defaultFor(schema) {
      if (!schema || typeof schema !== "object") return null;
      if (schema.default !== undefined) return schema.default;
      if (Array.isArray(schema.enum) && schema.enum.length) return schema.enum[0];
      if (schema.type === "boolean") return false;
      if (schema.type === "number") return 0;
      if (schema.type === "string") return "";
      if (schema.type === "array") return [];
      if (schema.type === "object") return {};
      return null;
    }

    _node(path, name, value, root) {
      const id = path.length ? path.join(".") : "root";
      const schema = this._schemaAt(path);
      const fieldErrors = this._errorsByPath[id] || [];
      const locked = schema != null;
      const container = new Column({ key: `node-${id}`, gap: 3 });
      const row = new Row({ key: `row-${id}`, gap: 6 });
      row.append(new TreeIndent({
        key: `indent-${id}`,
        levels: path.length,
        width: path.length * 22,
      }));
      const type = (schema && schema.type) || this._typeOf(value);
      const expandable = type === "object" || type === "array";
      const hasChildren = expandable && (
        type === "array"
          ? Array.isArray(value) && value.length > 0
          : (schema && schema.properties)
            ? Object.keys(schema.properties).length > 0
            : value && typeof value === "object" && Object.keys(value).length > 0
      );
      const expanded = !this._collapsed.has(id);
      if (hasChildren) {
        row.append(new Disclosure({
          key: `disclosure-${id}`,
          expanded,
          width: 22,
          onClick: () => {
            if (expanded) this._collapsed.add(id);
            else this._collapsed.delete(id);
            this.rebuild();
          },
        }));
      } else {
        row.append(new Disclosure({
          key: `disclosure-${id}`,
          expanded: false,
          empty: true,
          disabled: true,
          width: 22,
        }));
      }

      if (locked || root || typeof path[path.length - 1] === "number") {
        row.append(new Text({ key: `name-${id}`, text: name, width: 150 }));
      } else {
        const keyInput = new Input({
          key: `name-${id}`,
          label: `##name-${id}`,
          value: name,
          width: 150,
          onChange: (next) => keyInput.set("value", next),
          onSubmit: (next) => this._rename(path, next),
        });
        row.append(keyInput);
      }

      if (schema && schema.description) {
        row.append(new Text({ key: `description-${id}`, wrap: true, text: schema.description }));
      }
      if (fieldErrors.length) {
        row.append(new Text({ key: `errors-${id}`, wrap: true, text: fieldErrors.join("；") }));
      }

      if (!locked) {
        row.append(new TypeBadge({
          key: `type-${id}`,
          label: this._typeOf(value),
          onChange: (next) => this._changeType(path, next),
        }));
      }

      const enums = schema && Array.isArray(schema.enum) ? schema.enum : null;
      const readOnly = Boolean(schema && schema.readOnly);
      if (enums) {
        row.append(new Select({
          key: `value-${id}`,
          value: String(value),
          options: enums.map((item) => String(item)),
          disabled: readOnly,
          width: 220,
          onChange: (next) => {
            this._setPath(path, schema.type === "number" ? Number(next) : next);
            this.rebuild();
          },
        }));
      } else if (type === "string" || type === "number") {
        const valueInput = new Input({
          key: `value-${id}`,
          label: `##value-${id}`,
          value: String(value ?? ""),
          width: 300,
          readOnly,
          onChange: (next) => {
            valueInput.set("value", next);
            this._changeScalar(path, type, next);
          },
        });
        row.append(valueInput);
      } else if (type === "boolean") {
        row.append(new Checkbox({
          key: `value-${id}`,
          label: "值",
          value: Boolean(value),
          disabled: readOnly,
          onChange: (next) => {
            this._setPath(path, next);
            this.rebuild();
          },
        }));
      } else if (type === "null") {
        row.append(new Text({ key: `value-${id}`, text: "null" }));
      } else if (!readOnly && (!locked || schema.additionalProperties !== false)) {
        row.append(new IconButton({
          key: `add-${id}`,
          icon: "plus",
          onClick: () => this._add(path, type),
        }));
      }
      if (!root && !locked && !readOnly) {
        row.append(new IconButton({
          key: `delete-${id}`,
          icon: "delete",
          onClick: () => this._remove(path),
        }));
      }
      container.append(row);
      if (!expanded) return container;
      if (type === "array") {
        (Array.isArray(value) ? value : []).forEach((child, index) => {
          container.append(this._node([...path, index], `[${index}]`, child));
        });
      } else if (type === "object") {
        const properties = schema && schema.properties && typeof schema.properties === "object"
          ? schema.properties : null;
        const object = value && typeof value === "object" && !Array.isArray(value) ? value : {};
        if (properties) {
          for (const key of Object.keys(properties)) {
            const field = properties[key];
            const child = Object.prototype.hasOwnProperty.call(object, key)
              ? object[key]
              : this._defaultFor(field);
            container.append(this._node([...path, key], (field && field.title) || key, child));
          }
          if (schema.additionalProperties !== false) {
            for (const key of Object.keys(object)) {
              if (!Object.prototype.hasOwnProperty.call(properties, key)) {
                container.append(this._node([...path, key], key, object[key]));
              }
            }
          }
        } else {
          Object.entries(object).forEach(([key, child]) => container.append(this._node([...path, key], key, child)));
        }
      }
      return container;
    }

    _typeOf(value) {
      if (value === null) return "null";
      if (Array.isArray(value)) return "array";
      return typeof value;
    }

    _get(path) {
      let value = this._document;
      for (const key of path) value = value[key];
      return value;
    }

    _setPath(path, value) {
      if (!path.length) {
        this._document = value;
      } else {
        this._get(path.slice(0, -1))[path[path.length - 1]] = value;
      }
      this._dirty.set(true);
      this._emitChange();
    }

    _rename(path, next) {
      if (this._schemaAt(path)) return;
      const key = path[path.length - 1];
      if (typeof key !== "string" || !next || next === key) return;
      const parent = this._get(path.slice(0, -1));
      if (Object.prototype.hasOwnProperty.call(parent, next)) return;
      const entries = Object.entries(parent).map(([name, value]) => [name === key ? next : name, value]);
      for (const name of Object.keys(parent)) delete parent[name];
      for (const [name, value] of entries) parent[name] = value;
      this._dirty.set(true);
      this._emitChange();
      this.rebuild();
    }

    _changeType(path, next) {
      if (this._schemaAt(path)) return;
      const defaults = { object: {}, array: [], string: "", number: 0, boolean: false, null: null };
      this._setPath(path, defaults[next]);
      this.rebuild();
    }

    _changeScalar(path, type, source) {
      if (type === "string") this._setPath(path, source);
      else if (source.trim() !== "" && Number.isFinite(Number(source))) this._setPath(path, Number(source));
    }

    _add(path, type) {
      const schema = this._schemaAt(path);
      if (schema && schema.additionalProperties === false) return;
      const parent = this._get(path);
      if (type === "array") parent.push(null);
      else {
        let index = 1;
        while (`property${index}` in parent) index++;
        parent[`property${index}`] = null;
      }
      this._collapsed.delete(path.length ? path.join(".") : "root");
      this._dirty.set(true);
      this._emitChange();
      this.rebuild();
    }

    _remove(path) {
      if (this._schemaAt(path)) return;
      const key = path[path.length - 1];
      const parent = this._get(path.slice(0, -1));
      if (Array.isArray(parent)) parent.splice(key, 1);
      else delete parent[key];
      this._dirty.set(true);
      this._emitChange();
      this.rebuild();
    }
  }

  class XmlInspect extends Inspect {
    constructor(props = {}) { super("xml-inspect", props); }
  }
  class YamlInspect extends Inspect {
    constructor(props = {}) { super("yaml-inspect", props); }
  }
  class TomlInspect extends Inspect {
    constructor(props = {}) { super("toml-inspect", props); }
  }

  const ui = {
    Control, ControlTree, VectorDrawing,
    Root, Window, Page, Modal, Row, Column, Group, Panel, Graphics,
    Text, Button, Checkbox, Input, Slider, Separator, Select, Radio, Tabs, List, Scroll, Progress,
    TextArea, NumberInput, Image,
    Tree,
    TypeBadge, Disclosure, IconButton, TreeIndent,
    Inspect, JInspect, XmlInspect, YamlInspect, TomlInspect,
  };

  ykm.ui = Object.freeze(ui);
  ykm.state = (value) => new State(value);
  ykm.computed = (compute, dependencies) => new Computed(compute, dependencies);
})();
)YKMJS"
