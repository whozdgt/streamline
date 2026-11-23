import QtQuick

// One of the on-device AI models: what it does, its size, and Download / Cancel / Remove.
Column {
    id: root
    property var info: ({})                  // an entry of ai.models()
    property bool selectable: false          // the welcome screen: a tick box instead of buttons
    property bool selected: false
    signal toggled()
    readonly property color warn: "#e5484d"
    spacing: 6

    Item {
        width: parent.width
        height: Math.max(nameCol.implicitHeight, tick.height)
        Rectangle {      // welcome screen: tick to include it
            id: tick
            visible: root.selectable
            width: root.selectable ? 24 : 0; height: 24
            y: 2
            radius: settings.round ? 6 : 0
            color: root.selected ? settings.ink : "transparent"
            border.width: 2
            border.color: settings.ink
            Text { anchors.centerIn: parent; visible: root.selected; text: "✓"; font.pixelSize: 15; font.bold: true; color: settings.onInk }
        }
        Column {
            id: nameCol
            anchors { left: tick.right; leftMargin: root.selectable ? 12 : 0; right: parent.right }
            spacing: 3
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: (root.info.name || "") + "  ·  " + (root.info.sizeLabel || "")
                font.pixelSize: 15; font.weight: Font.DemiBold
                color: settings.ink
            }
            Text { width: parent.width; wrapMode: Text.Wrap; text: root.info.about || ""; font.pixelSize: 13; lineHeight: 1.15; color: settings.inkSoft }
        }
        MouseArea { enabled: root.selectable; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.toggled() }
    }

    // progress while it downloads
    Column {
        visible: !root.selectable && root.info.downloading === true
        width: parent.width
        spacing: 6
        Rectangle {
            width: parent.width; height: 6
            radius: 3
            color: settings.boxLine
            Rectangle { width: parent.width * Math.min(1, root.info.progress || 0); height: parent.height; radius: 3; color: settings.accent }
        }
        Row {
            spacing: 16
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.info.checking ? "Checking the download…" : (root.info.received || "0 MB") + " of " + (root.info.sizeLabel || "")
                font.pixelSize: 12; color: settings.inkSoft
            }
            Text {
                visible: !root.info.checking
                anchors.verticalCenter: parent.verticalCenter
                text: "Cancel"; font.pixelSize: 12; font.underline: true; color: settings.ink
                MouseArea { anchors.fill: parent; anchors.margins: -8; cursorShape: Qt.PointingHandCursor; onClicked: ai.cancel(root.info.id) }
            }
        }
    }
    Text {
        visible: !root.selectable && !!root.info.error
        width: parent.width
        wrapMode: Text.Wrap
        text: root.info.error || ""
        font.pixelSize: 12; color: root.warn
    }
    Row {
        visible: !root.selectable && !root.info.downloading
        spacing: 16
        Btn {
            visible: !root.info.installed
            label: root.info.error ? "Try again" : "Download"
            primary: false
            onClicked: ai.download(root.info.id)
        }
        Text {
            visible: root.info.installed === true
            anchors.verticalCenter: parent.verticalCenter
            text: "✓ Downloaded"; font.pixelSize: 13; color: settings.accent
        }
        Text {
            visible: root.info.installed === true
            anchors.verticalCenter: parent.verticalCenter
            text: "Remove"; font.pixelSize: 13; font.underline: true; color: settings.ink
            MouseArea { anchors.fill: parent; anchors.margins: -8; cursorShape: Qt.PointingHandCursor; onClicked: ai.remove(root.info.id) }
        }
    }
}
