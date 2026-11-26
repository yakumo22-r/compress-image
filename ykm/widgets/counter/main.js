const { Window, Text, Input, Separator, Row, Button, Checkbox } = ykm.ui;

const count = ykm.state(0);
const enabled = ykm.state(true);
const name = ykm.state("YKM");

const root = new Window({ key: "counter", title: "Counter", width: 430, height: 300 }).append(
  new Text({ text: ykm.computed(() => `Hello, ${name.value}`, [name]) }),
  new Input({
    key: "name",
    label: "Name",
    value: name,
    onChange(value) { name.set(value); },
  }),
  new Separator(),
  new Text({ text: ykm.computed(() => `Count: ${count.value}`, [count]) }),
  new Row({ key: "actions" }).append(
    new Button({ label: "Increment", onClick() { count.update((value) => value + 1); } }),
    new Button({
      label: "Notify monitor",
      onClick() { ykm.send("monitor", { type: "counter", count: count.value }); },
    }),
  ),
  new Checkbox({
    label: "Enabled",
    value: enabled,
    onChange(value) { enabled.set(value); },
  }),
);

function render() {
  return root;
}
