import QtQuick

// A text box for several lines, growing with what is typed. Ctrl+Enter sends submitted().
Rectangle {
    id: root
    property string placeholder
    property alias text: edit.text
    property Item nextField: null          // Tab jumps here
    property int minHeight: 88
    signal submitted()
    function focusInput() { edit.forceActiveFocus() }
    function clear() { edit.text = "" }

    readonly property color pri: settings.ink
    implicitWidth: 200
    implicitHeight: Math.max(minHeight, edit.contentHeight + 24)
    color: settings.round ? settings.field : Qt.rgba(1, 1, 1, 0.85); radius: settings.round ? 10 : 0
    border.width: 1
    border.color: edit.activeFocus ? Qt.rgba(pri.r, pri.g, pri.b, 0.6) : Qt.rgba(0, 0, 0, 0.2)

    Text {
        visible: edit.text.length === 0
        x: 14; y: 12
        width: parent.width - 28
        wrapMode: Text.Wrap
        text: root.placeholder
        font.pixelSize: 14
        color: settings.round ? settings.inkSoft : Qt.rgba(0, 0, 0, 0.5)
    }
    TextEdit {
        id: edit
        anchors { fill: parent; leftMargin: 14; rightMargin: 14; topMargin: 12; bottomMargin: 12 }
        font.pixelSize: 14
        color: root.pri
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        clip: true
        Keys.onPressed: (e) => {
            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && (e.modifiers & Qt.ControlModifier)) { root.submitted(); e.accepted = true }
        }
        Keys.onTabPressed: (e) => { if (root.nextField) root.nextField.focusInput(); e.accepted = true }
    }
    MouseArea {      // the whole box takes the click, not only the lines that have text
        anchors.fill: parent
        z: -1
        cursorShape: Qt.IBeamCursor
        onClicked: edit.forceActiveFocus()
    }
}
