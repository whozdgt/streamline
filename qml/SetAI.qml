import QtQuick
import QtQuick.Layouts

// Settings › AI: the free on-device models that sort pods (download, cancel, remove).
ColumnLayout {
    id: root
    readonly property color pri: settings.ink
    readonly property color muted: Qt.rgba(pri.r, pri.g, pri.b, 0.72)

    spacing: 0

    SetGroup {
        first: true
        title: "On-device AI"
        section: "AI"
        SetBlock {
            label: "Models"
            keywords: "ai model models download remove meaning writer pods sort offline"
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12; lineHeight: 1.3; color: root.muted
                text: "Two free AI models that read your documents and sort them by what they're about. They run on this device: "
                    + "your documents never leave it, and once downloaded they work offline. Without them, pods sort by key words."
            }
            Repeater {
                model: { ai.revision; return ai.models() }
                AiModelRow { required property var modelData; Layout.fillWidth: true; info: modelData }
            }
        }
    }
}
