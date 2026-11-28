import QtQuick

// To-dos, the same list Dayline keeps. Signed in, it syncs with Dayline on your phone.
// Tap the circle to tick a to-do off; tap the rest of the row to edit it.
Item {
    id: root
    property int gutter: 36
    property real bottomMargin: 0
    property alias contentY: flick.contentY
    readonly property alias scrolling: flick.moving       // the person is scrolling it (not a page that changed size)
    signal signInRequested()

    property string filter: "all"          // all | today | doing | overdue | flagged
    property string sort: "manual"         // manual | due | flag | color | az
    property string query: ""
    property bool showDone: true
    property bool confirmClear: false
    // the editor
    property string editingId: ""
    property string editColor: "amber"
    property bool editFlag: false
    property bool editDoing: false
    property bool dueError: false
    property bool confirmDelete: false

    readonly property var filters: [{ k: "all", t: "All" }, { k: "today", t: "Today" }, { k: "doing", t: "In progress" }, { k: "overdue", t: "Overdue" }, { k: "flagged", t: "Flagged" }]
    readonly property var sorts: [{ k: "manual", t: "Newest first" }, { k: "due", t: "Due date" }, { k: "flag", t: "Flagged first" }, { k: "color", t: "Colour" }, { k: "az", t: "A to Z" }]
    readonly property var openList: { todos.revision; return todos.open(filter, sort, query, "") }
    readonly property var doneList: { todos.revision; return todos.done(filter, query, "") }
    readonly property var palette: todos.colors()
    readonly property color warn: "#e5484d"

    onVisibleChanged: if (visible) todos.refresh()

    function sortName() {
        for (let i = 0; i < sorts.length; ++i) if (sorts[i].k === sort) return sorts[i].t
        return sorts[0].t
    }
    function nextSort() {
        for (let i = 0; i < sorts.length; ++i) if (sorts[i].k === sort) { sort = sorts[(i + 1) % sorts.length].k; return }
        sort = "manual"
    }
    function emptyText() {
        if (query.trim() !== "") return "No to-dos match “" + query.trim() + "”."
        return ({ all: "No to-dos yet. Add your first one above.", today: "Nothing due today.", doing: "Nothing in progress.",
                  overdue: "Nothing overdue.", flagged: "Nothing flagged." })[filter]
    }
    // Back (the phone's key or Escape): close the editor. False when there was nothing to close.
    function goBack() {
        if (editingId !== "") { editingId = ""; return true }
        return false
    }
    function addQuick() {
        if (todos.add(quickField.text, filter, "") !== "") quickField.clear()
        quickField.focusInput()
    }
    function openEditor(id) {
        const t = todos.item(id)
        if (!t.id) return
        titleField.text = t.title
        notesBox.text = t.notes
        dueField.text = t.due
        editColor = t.color; editFlag = t.flag; editDoing = t.doing
        dueError = false; confirmDelete = false
        editingId = id
        flick.contentY = 0
    }
    function saveEdit() {
        const due = dueField.text.trim()
        if (due !== "" && !todos.validDate(due)) { dueError = true; dueField.focusInput(); return }
        if (titleField.text.trim() === "") { titleField.focusInput(); return }
        todos.update(editingId, titleField.text, notesBox.text, due, editColor, editFlag, editDoing)
        editingId = ""
    }
    // A sync can delete the to-do being edited (it was deleted on another device): close the editor then.
    Connections {
        target: todos
        function onRevisionChanged() { if (root.editingId !== "" && !todos.item(root.editingId).id) root.editingId = "" }
    }

    component Link: Text {
        id: link
        signal clicked()
        font.pixelSize: 13
        color: settings.ink
        MouseArea { anchors.fill: parent; anchors.margins: -8; cursorShape: Qt.PointingHandCursor; onClicked: link.clicked() }
    }
    component Pill: Rectangle {
        id: pill
        property string label
        property bool selected: false
        signal clicked()
        implicitWidth: pillText.implicitWidth + 28
        implicitHeight: 34
        radius: settings.round ? height / 2 : 0
        color: selected ? settings.ink : (pillMa.containsMouse ? settings.boxHover : settings.boxFill)
        border.width: settings.round ? 0 : 1
        border.color: selected ? settings.ink : settings.boxLine
        Accessible.role: Accessible.Button
        Accessible.name: label
        Accessible.checked: selected
        Accessible.onPressAction: pill.clicked()
        Text { id: pillText; anchors.centerIn: parent; text: pill.label; font.pixelSize: 13; color: pill.selected ? settings.onInk : settings.ink }
        MouseArea { id: pillMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pill.clicked() }
    }
    component TodoRow: Rectangle {
        id: tr
        required property var modelData
        width: col.width
        height: Math.max(58, body.implicitHeight + 26)
        radius: settings.round ? 14 : 0
        color: rowMa.containsMouse ? settings.boxHover : settings.boxFill

        Rectangle {      // the to-do's colour
            x: 0
            width: 4
            height: parent.height - (settings.round ? 22 : 0)
            anchors.verticalCenter: parent.verticalCenter
            radius: settings.round ? 2 : 0
            color: tr.modelData.hex
            opacity: tr.modelData.done ? 0.4 : 1
        }
        MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openEditor(tr.modelData.id) }
        Column {
            id: body
            anchors { left: parent.left; leftMargin: 18; right: flagMark.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
            spacing: 4
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: tr.modelData.title
                font.pixelSize: 14
                font.strikeout: tr.modelData.done
                color: tr.modelData.done ? settings.inkSoft : settings.ink
            }
            Text {
                readonly property var parts: {
                    const m = []
                    if (tr.modelData.due) m.push((tr.modelData.overdue ? "Overdue · " : "Due ") + tr.modelData.dueLabel)
                    if (tr.modelData.doing) m.push("In progress")
                    if (tr.modelData.snippet) m.push(tr.modelData.snippet)
                    return m
                }
                visible: parts.length > 0
                width: parent.width
                elide: Text.ElideRight
                text: parts.join("  ·  ")
                font.pixelSize: 12
                color: tr.modelData.overdue ? root.warn : settings.inkSoft
            }
        }
        Icon {
            id: flagMark
            visible: tr.modelData.flag
            width: visible ? 18 : 0
            name: "flag"; size: 18
            color: tr.modelData.hex
            anchors { right: check.left; rightMargin: visible ? 12 : 0; verticalCenter: parent.verticalCenter }
        }
        Rectangle {      // done or not
            id: check
            width: 26; height: 26; radius: 13
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            color: tr.modelData.done ? tr.modelData.hex : "transparent"
            border.width: 2
            border.color: tr.modelData.hex
            Accessible.role: Accessible.CheckBox
            Accessible.checked: tr.modelData.done
            Accessible.name: (tr.modelData.done ? "Mark as not done: " : "Mark as done: ") + tr.modelData.title
            Accessible.onToggleAction: todos.toggle(tr.modelData.id)
            Accessible.onPressAction: todos.toggle(tr.modelData.id)
            Text { anchors.centerIn: parent; visible: tr.modelData.done; text: "✓"; font.pixelSize: 14; font.bold: true; color: "#12131a" }
            MouseArea { anchors.fill: parent; anchors.margins: -8; cursorShape: Qt.PointingHandCursor; onClicked: todos.toggle(tr.modelData.id) }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + root.bottomMargin + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            x: root.gutter
            width: Math.min(flick.width - 2 * root.gutter, 760)
            spacing: 14

            // ------------------------------------------------------------ the list
            Column {
                visible: root.editingId === ""
                width: parent.width
                spacing: 14

                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: 14
                    color: settings.inkSoft
                    text: {
                        todos.revision
                        const n = todos.attention()
                        const open = root.filter === "all" && root.query.trim() === "" ? root.openList.length : -1
                        const lead = open >= 0 ? open + (open === 1 ? " to-do open" : " to-dos open") : ""
                        const late = n > 0 ? n + " due today or overdue" : ""
                        return [lead, late].filter(function(s) { return s !== "" }).join(" · ") || " "
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 10
                    Field {
                        id: quickField
                        width: Math.min(parent.width - addBtn.width - 10, 480)
                        placeholder: "Add a to-do and press Enter"
                        onAccepted: root.addQuick()
                    }
                    Btn { id: addBtn; height: 44; label: "Add"; onClicked: root.addQuick() }
                }

                Flow {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: root.filters
                        Pill {
                            required property var modelData
                            label: modelData.t
                            selected: root.filter === modelData.k
                            onClicked: { root.filter = modelData.k; root.confirmClear = false }
                        }
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 18
                    Field {
                        id: searchField
                        width: Math.min(parent.width, 260)
                        height: 38
                        placeholder: "Search to-dos"
                        onTextChanged: root.query = text
                    }
                    Link {
                        height: 38
                        verticalAlignment: Text.AlignVCenter
                        text: "Sort: " + root.sortName()
                        onClicked: root.nextSort()
                    }
                }

                Repeater { model: root.openList; TodoRow {} }
                Text {
                    visible: root.openList.length === 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: 14
                    color: settings.inkSoft
                    text: root.emptyText()
                }

                Row {
                    visible: root.doneList.length > 0
                    spacing: 24
                    topPadding: 8
                    Link {
                        text: (root.showDone ? "▾ " : "▸ ") + "Done · " + root.doneList.length
                        onClicked: { root.showDone = !root.showDone; root.confirmClear = false }
                    }
                    Link {
                        visible: root.showDone
                        text: root.confirmClear ? "Confirm: clear " + root.doneList.length + " done" : "Clear done"
                        font.weight: root.confirmClear ? Font.Bold : Font.Normal
                        onClicked: {
                            if (!root.confirmClear) { root.confirmClear = true; return }
                            root.confirmClear = false
                            todos.clearDone()
                        }
                    }
                    Link { visible: root.showDone && root.confirmClear; text: "Keep them"; onClicked: root.confirmClear = false }
                }
                Repeater { model: root.showDone ? root.doneList : []; TodoRow {} }

                // where the list is kept
                Flow {
                    width: parent.width
                    spacing: 18
                    topPadding: 10
                    Text {
                        width: Math.min(implicitWidth, parent.width)
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: settings.inkSoft
                        text: account.signedIn ? "Synced with Dayline as @" + account.username + ". " + todos.syncLine
                                               : "Saved on this device. Sign in to share this list with Dayline on your phone."
                    }
                    Link {
                        font.pixelSize: 12
                        text: account.signedIn ? (todos.syncing ? "Syncing…" : "Sync now") : "Sign in"
                        onClicked: { if (account.signedIn) { if (!todos.syncing) todos.syncNow() } else root.signInRequested() }
                    }
                }
            }

            // ------------------------------------------------------------ editing one to-do
            Column {
                visible: root.editingId !== ""
                width: parent.width
                spacing: 12

                Link { text: "‹ To-dos"; onClicked: root.editingId = "" }

                Field { id: titleField; width: parent.width; placeholder: "To-do"; nextField: notesBox; onAccepted: root.saveEdit() }
                TextBox { id: notesBox; width: parent.width; minHeight: 80; placeholder: "Notes"; nextField: dueField }

                SectionLabel { text: "DUE DATE" }
                Flow {
                    width: parent.width
                    spacing: 8
                    Field {
                        id: dueField
                        width: 160
                        height: 34
                        placeholder: "YYYY-MM-DD"
                        nextField: titleField
                        onAccepted: root.saveEdit()
                        onTextChanged: root.dueError = false
                    }
                    Pill { label: "None"; selected: dueField.text === ""; onClicked: dueField.text = "" }
                    Pill { label: "Today"; selected: dueField.text === todos.dayFromToday(0); onClicked: dueField.text = todos.dayFromToday(0) }
                    Pill { label: "Tomorrow"; selected: dueField.text === todos.dayFromToday(1); onClicked: dueField.text = todos.dayFromToday(1) }
                    Pill { label: "In a week"; selected: dueField.text === todos.dayFromToday(7); onClicked: dueField.text = todos.dayFromToday(7) }
                }
                Text {
                    visible: root.dueError || dueField.text.trim() !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: root.dueError ? root.warn : settings.inkSoft
                    text: root.dueError ? "Write the date as year-month-day, for example " + todos.dayFromToday(7) + "."
                                        : todos.dueLabel(dueField.text.trim())
                }

                SectionLabel { text: "COLOUR" }
                Flow {
                    width: parent.width
                    spacing: 10
                    Repeater {
                        model: root.palette
                        Rectangle {
                            id: swatch
                            required property var modelData
                            readonly property bool chosen: root.editColor === modelData.key
                            width: 32; height: 32; radius: 16
                            color: modelData.hex
                            border.width: chosen ? 3 : 0
                            border.color: settings.ink
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: modelData.name
                            Accessible.checked: chosen
                            Accessible.onPressAction: root.editColor = modelData.key
                            Text { anchors.centerIn: parent; visible: swatch.chosen; text: "✓"; font.pixelSize: 14; font.bold: true; color: "#12131a" }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.editColor = swatch.modelData.key }
                        }
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 8
                    topPadding: 6
                    Pill { label: (root.editDoing ? "✓ " : "") + "In progress"; selected: root.editDoing; onClicked: root.editDoing = !root.editDoing }
                    Pill { label: (root.editFlag ? "✓ " : "") + "Flagged"; selected: root.editFlag; onClicked: root.editFlag = !root.editFlag }
                }

                Flow {
                    width: parent.width
                    spacing: 10
                    topPadding: 8
                    Btn { label: "Save"; onClicked: root.saveEdit() }
                    Btn { primary: false; label: "Cancel"; onClicked: root.editingId = "" }
                    Btn {
                        primary: false
                        label: root.confirmDelete ? "Confirm delete" : "Delete"
                        onClicked: {
                            if (!root.confirmDelete) { root.confirmDelete = true; return }
                            const id = root.editingId
                            root.editingId = ""
                            todos.remove(id)
                        }
                    }
                }
            }
        }
    }
}
