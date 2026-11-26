const { Window, Text, Separator } = ykm.ui;

const lastMessage = ykm.state("No messages yet");

function onMessage(message, sender) {
  lastMessage.set(`${sender}: ${JSON.stringify(message)}`);
}

const root = new Window({ key: "monitor", title: "Session monitor", width: 430, height: 180 }).append(
  new Text({ text: "Messages are cloned between isolated QuickJS runtimes." }),
  new Separator(),
  new Text({ text: lastMessage }),
);

function render() {
  return root;
}
