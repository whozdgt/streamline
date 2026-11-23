import QtQuick

// The first time Streamline opens: a welcome, and the choice to download the free on-device AI for Pods.
// "Not now" is fine: the same downloads wait on the Pods page.
Rectangle {
    id: root
    signal done()
    property real topInset: 0                // the phone's status bar and gesture bar, which this screen reaches under
    property real bottomInset: 0
    color: settings.ground
    readonly property bool narrow: width < 640
    readonly property var list: { ai.revision; return ai.models() }
    property bool wantMeaning: true
    property bool wantWriter: !settings.mobile          // the big one: ticked on a computer, a choice on a phone
    readonly property real total: (wantMeaning ? list[0].size : 0) + (wantWriter ? list[1].size : 0)

    MouseArea { anchors.fill: parent }      // nothing behind it takes clicks

    Flickable {
        anchors.fill: parent
        contentHeight: col.y + col.implicitHeight + 40 + root.bottomInset
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: col
            width: Math.min(root.width - 48, 560)
            x: (root.width - width) / 2
            y: Math.max(root.topInset + 32, (root.height - implicitHeight) / 2 - 20)
            spacing: 18

            Icon { name: "pod"; size: 44; color: settings.accent }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Welcome to Streamline"
                font.pixelSize: root.narrow ? 30 : 36
                font.weight: settings.round ? Font.Black : Font.DemiBold
                font.letterSpacing: settings.round ? -1 : 0
                color: settings.ink
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Your music, flashcards, to-dos and Pods in one place."
                font.pixelSize: 15; lineHeight: 1.2; color: settings.inkSoft
            }

            Rectangle {
                width: parent.width
                height: aiCol.implicitHeight + 36
                radius: settings.round ? 18 : 0
                color: settings.boxFill
                border.width: settings.round ? 0 : 1
                border.color: settings.boxLine
                Column {
                    id: aiCol
                    x: 18; y: 18
                    width: parent.width - 36
                    spacing: 16
                    Text { text: "On-device AI for Pods"; font.pixelSize: 18; font.weight: Font.DemiBold; color: settings.ink }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        lineHeight: 1.2
                        font.pixelSize: 13
                        color: settings.inkSoft
                        text: "Pods sort a folder of documents into topics. These two free AI models read your documents and sort them by what they're about. "
                            + "They run on this device, so your documents never leave it, there's nothing to pay, and once downloaded they work offline."
                    }
                    AiModelRow {
                        width: parent.width
                        info: root.list[0]
                        selectable: true
                        selected: root.wantMeaning
                        onToggled: root.wantMeaning = !root.wantMeaning
                    }
                    AiModelRow {
                        width: parent.width
                        info: root.list[1]
                        selectable: true
                        selected: root.wantWriter
                        onToggled: root.wantWriter = !root.wantWriter
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: settings.inkSoft
                        text: root.total > 0 ? "Download: " + ai.sizeLabel(root.total) + (settings.mobile ? ". Best on Wi-Fi." : ".") + " It carries on in the background while you use the app."
                                             : "Without the models, Pods sort by key words."
                    }
                }
            }

            Flow {
                width: parent.width
                spacing: 12
                Btn {
                    large: true
                    label: root.total > 0 ? "Download and start" : "Start"
                    onClicked: {
                        if (root.wantMeaning) ai.download("meaning")
                        if (root.wantWriter) ai.download("writer")
                        ai.offered = true
                        root.done()
                    }
                }
                Btn {
                    visible: root.total > 0
                    large: true
                    primary: false
                    label: "Not now"
                    onClicked: { ai.offered = true; root.done() }
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: settings.inkSoft
                text: "You can download or remove them any time on the Pods page (Study › Pods on a phone)."
            }
        }
    }
}
