import QtQuick
import QtQuick.Dialogs

// Pods: pick a folder of documents and Streamline sorts them into topics.
// The list of pods, one pod's topics, and one document (a preview, which topic it is in, and Open).
Item {
    id: root
    property int gutter: 36
    property real bottomMargin: 0
    property alias contentY: flick.contentY
    signal chooseFolder()

    property string mode: "list"            // list | pod | doc
    property string podId: ""
    property string docPath: ""
    property string query: ""
    property int detail: 1                  // what the next "Sort" uses: 0 broad, 1 balanced, 2 specific
    property bool renaming: false
    property string renamingTopic: ""
    property bool confirmMove: false
    property bool confirmDelete: false
    property string note: ""
    property var expanded: ({})             // topic id → showing all its documents
    property var collapsed: ({})            // topic id → the topics inside it are hidden
    property string layout: "nested"        // what the next "Sort again" makes: nested | topics | spec
    property bool pasting: false            // the box for pasting a spec is open
    property string specNote: ""
    property bool allowed: pods.filesAllowed()

    readonly property bool narrow: width < 640
    readonly property var podList: { pods.revision; return pods.pods() }
    readonly property var info: { pods.revision; return podId !== "" ? pods.pod(podId, query) : ({}) }
    readonly property var docInfo: {
        const t = info.topics || []
        for (let i = 0; i < t.length; ++i)
            for (let j = 0; j < t[i].docs.length; ++j)
                if (t[i].docs[j].path === docPath) return Object.assign({ topicId: t[i].id, topicName: t[i].name }, t[i].docs[j])
        return ({})
    }
    readonly property color warn: "#e5484d"

    function plural(n, word) { return n + " " + word + (n === 1 ? "" : "s") }
    function goBack() {
        if (mode === "doc") { mode = "pod"; return true }
        if (mode === "pod") { mode = "list"; podId = ""; return true }
        return false
    }
    function openPod(id) {
        podId = id; query = ""; note = ""; renaming = false; renamingTopic = ""; confirmMove = false; confirmDelete = false
        expanded = ({}); collapsed = ({}); pasting = false; specNote = ""
        const info = pods.pod(id)
        detail = info.detail !== undefined ? info.detail : 1
        layout = info.layout || "nested"
        mode = "pod"
        flick.contentY = 0
    }
    function openDoc(path) {
        docPath = path
        preview.text = pods.docText(path)
        note = ""
        mode = "doc"
        flick.contentY = 0
    }
    onVisibleChanged: if (visible) allowed = pods.filesAllowed()
    Timer {      // Android: "All files access" is switched on in Settings, outside the app; notice when it is
        interval: 1000; repeat: true
        running: root.visible && !root.allowed
        onTriggered: root.allowed = pods.filesAllowed()
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
        Text { id: pillText; anchors.centerIn: parent; text: pill.label; font.pixelSize: 13; color: pill.selected ? settings.onInk : settings.ink }
        MouseArea { id: pillMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pill.clicked() }
    }
    component Card: Rectangle {
        default property alias content: inner.data
        property real pad: 16
        width: col.width - x
        height: inner.implicitHeight + 2 * pad
        radius: settings.round ? 16 : 0
        color: settings.boxFill
        border.width: settings.round ? 0 : 1
        border.color: settings.boxLine
        Column { id: inner; x: parent.pad; y: parent.pad; width: parent.width - 2 * parent.pad; spacing: 8 }
    }
    component Para: Text {
        width: parent ? parent.width : 0
        wrapMode: Text.Wrap
        font.pixelSize: 13
        lineHeight: 1.15
        color: settings.inkSoft
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.implicitHeight + 40 + root.bottomMargin
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: col
            x: root.gutter
            width: Math.min(root.width - 2 * root.gutter, 880)
            topPadding: 8
            spacing: 14

            // ---------------- the list of pods ----------------
            Column {
                visible: root.mode === "list"
                width: parent.width
                spacing: 14

                Para { text: "Pick a folder of documents and Streamline sorts them into topics. Your files stay where they are unless you choose to move them." }

                Card {
                    visible: !root.allowed
                    Para { color: settings.ink; text: "To read your documents, Streamline needs “All files access”. Turn it on for Streamline on the page that opens, then come back." }
                    Btn { label: "Allow access to files"; onClicked: pods.askFiles() }
                }

                Btn { label: "New pod"; enabled: root.allowed; onClicked: root.chooseFolder() }

                Repeater {
                    model: root.podList
                    Rectangle {
                        id: pc
                        required property var modelData
                        width: col.width
                        height: pcCol.implicitHeight + 28
                        radius: settings.round ? 16 : 0
                        color: pcMa.containsMouse ? settings.boxHover : settings.boxFill
                        border.width: settings.round ? 0 : 1
                        border.color: settings.boxLine
                        MouseArea { id: pcMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openPod(pc.modelData.id) }
                        Icon { id: pcIcon; x: 16; anchors.verticalCenter: parent.verticalCenter; name: "pod"; size: 24; color: settings.accent }
                        Column {
                            id: pcCol
                            anchors { left: pcIcon.right; leftMargin: 14; right: pcChev.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                            spacing: 3
                            Text { width: parent.width; elide: Text.ElideRight; text: pc.modelData.name; font.pixelSize: 16; font.weight: Font.DemiBold; color: settings.ink }
                            Text {
                                width: parent.width; elide: Text.ElideRight; font.pixelSize: 12
                                color: pc.modelData.error ? root.warn : pc.modelData.busy ? settings.accent : settings.inkSoft
                                text: pc.modelData.busy ? pc.modelData.status
                                    : pc.modelData.error ? pc.modelData.error
                                    : root.plural(pc.modelData.docs, "document") + " · " + root.plural(pc.modelData.topics, "topic")
                            }
                            Text { width: parent.width; elide: Text.ElideMiddle; text: pc.modelData.folder; font.pixelSize: 12; color: settings.inkSoft }
                        }
                        Icon { id: pcChev; anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter } name: "chevron"; size: 18; color: settings.ink }
                    }
                }

                Card {      // the free on-device AI
                    Text { text: "On-device AI"; font.pixelSize: 15; font.weight: Font.DemiBold; color: settings.ink }
                    Para {
                        text: "Two free AI models that read your documents and sort them by what they're about. They run on this device: "
                            + "your documents never leave it, and once downloaded they work offline. Without them, pods sort by key words."
                    }
                    Repeater {
                        model: { ai.revision; return ai.models() }
                        AiModelRow { required property var modelData; width: parent.width; info: modelData }
                    }
                }
            }

            // ---------------- one pod ----------------
            Column {
                visible: root.mode === "pod"
                width: parent.width
                spacing: 14

                Link { text: "‹ All pods"; font.pixelSize: 14; onClicked: root.goBack() }

                Flow {
                    width: parent.width
                    spacing: 12
                    visible: !root.renaming
                    Text { text: root.info.name || ""; font.pixelSize: 24; font.weight: settings.round ? Font.Black : Font.DemiBold; color: settings.ink; width: Math.min(implicitWidth, parent.width) ; elide: Text.ElideRight }
                    Link { height: 32; verticalAlignment: Text.AlignVCenter; text: "Rename"; font.underline: true; onClicked: { podName.text = root.info.name; root.renaming = true; podName.focusInput() } }
                }
                Flow {
                    visible: root.renaming
                    width: parent.width
                    spacing: 10
                    Field { id: podName; width: root.narrow ? parent.width : 360; placeholder: "Pod name"; onAccepted: { pods.rename(root.podId, text); root.renaming = false } }
                    Btn { label: "Save"; onClicked: { pods.rename(root.podId, podName.text); root.renaming = false } }
                    Btn { label: "Cancel"; primary: false; onClicked: root.renaming = false }
                }
                Text { width: parent.width; wrapMode: Text.WrapAnywhere; text: root.info.folder || ""; font.pixelSize: 12; color: settings.inkSoft }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    color: root.info.busy ? settings.accent : root.info.error ? root.warn : settings.inkSoft
                    text: root.info.busy ? root.info.status : root.info.error ? root.info.error
                        : (root.plural(root.info.docCount || 0, "document") + (root.info.sortedLabel ? " · " + root.info.sortedLabel : ""))
                }

                Field { width: root.narrow ? parent.width : 360; placeholder: "Search this pod"; onTextChanged: root.query = text }

                Para {
                    visible: !root.info.busy && (root.info.topics || []).length === 0
                    text: root.query.trim() !== "" ? "Nothing in this pod matches “" + root.query.trim() + "”." : "No topics yet."
                }

                Repeater {
                    model: root.info.topics || []
                    Card {
                        id: tc
                        required property var modelData
                        readonly property bool all: root.expanded[modelData.id] === true || root.query.trim() !== ""
                        readonly property int limit: 6
                        readonly property int depth: Math.min(modelData.depth, 3)
                        readonly property bool shut: root.collapsed[modelData.id] === true && root.query.trim() === ""
                        // a topic inside a subject sits a little to the right of it; hidden while a topic above it is closed
                        x: depth * (root.narrow ? 12 : 20)
                        visible: { root.collapsed; for (let i = 0; i < modelData.ancestors.length; ++i) if (root.collapsed[modelData.ancestors[i]] === true) return root.query.trim() !== ""; return true }
                        color: depth === 0 && modelData.hasChildren ? settings.boxHover : settings.boxFill

                        Flow {
                            visible: root.renamingTopic !== tc.modelData.id
                            width: parent.width
                            spacing: 10
                            Text {      // open / close the topics inside
                                visible: tc.modelData.hasChildren
                                height: 24; width: 18
                                verticalAlignment: Text.AlignVCenter
                                text: tc.shut ? "▸" : "▾"
                                font.pixelSize: 16; color: settings.ink
                                MouseArea {
                                    anchors.fill: parent; anchors.margins: -10; cursorShape: Qt.PointingHandCursor
                                    onClicked: { const c = Object.assign({}, root.collapsed); c[tc.modelData.id] = !tc.shut; root.collapsed = c }
                                }
                            }
                            Text {
                                text: (tc.modelData.number ? tc.modelData.number + "  " : "") + tc.modelData.name
                                width: Math.min(implicitWidth, parent.width - (tc.modelData.hasChildren ? 28 : 0))
                                wrapMode: Text.Wrap
                                font.pixelSize: tc.depth === 0 ? (tc.modelData.hasChildren ? 20 : 17) : tc.depth === 1 ? 16 : 15
                                font.weight: tc.depth === 0 && tc.modelData.hasChildren ? Font.Bold : Font.DemiBold
                                color: tc.modelData.other ? settings.inkSoft : settings.ink
                            }
                            Text { height: 24; verticalAlignment: Text.AlignVCenter; text: tc.modelData.total; font.pixelSize: 13; color: settings.accent }
                            Link { height: 24; verticalAlignment: Text.AlignVCenter; text: "Rename"; font.pixelSize: 12; color: settings.inkSoft; onClicked: { topicName.text = tc.modelData.name; root.renamingTopic = tc.modelData.id; topicName.focusInput() } }
                        }
                        Flow {
                            visible: root.renamingTopic === tc.modelData.id
                            width: parent.width
                            spacing: 10
                            Field { id: topicName; width: root.narrow ? parent.width : 320; placeholder: "Topic name"; onAccepted: { pods.renameTopic(root.podId, tc.modelData.id, text); root.renamingTopic = "" } }
                            Btn { label: "Save"; onClicked: { pods.renameTopic(root.podId, tc.modelData.id, topicName.text); root.renamingTopic = "" } }
                            Btn { label: "Cancel"; primary: false; onClicked: root.renamingTopic = "" }
                        }
                        Para { visible: tc.modelData.summary !== ""; text: tc.modelData.summary; color: settings.ink }
                        Para { visible: tc.modelData.keywords.length > 0; text: tc.modelData.keywords.join(" · "); font.pixelSize: 12 }
                        Para { visible: tc.modelData.other; font.pixelSize: 12; text: root.info.layout === "spec" ? "Documents that didn't match any point of the spec." : "Documents that didn't share enough with any others." }

                        Repeater {
                            model: tc.shut ? [] : tc.all ? tc.modelData.docs : tc.modelData.docs.slice(0, tc.limit)
                            Rectangle {
                                id: dr
                                required property var modelData
                                width: parent.width
                                height: Math.max(52, drCol.implicitHeight + 16)
                                radius: settings.round ? 10 : 0
                                color: drMa.containsMouse ? settings.boxHover : "transparent"
                                MouseArea { id: drMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openDoc(dr.modelData.path) }
                                Icon { id: drIcon; x: 4; y: 14; name: "doc"; size: 20; color: settings.inkSoft }
                                Column {
                                    id: drCol
                                    anchors { left: drIcon.right; leftMargin: 12; right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
                                    spacing: 2
                                    Text { width: parent.width; elide: Text.ElideRight; text: dr.modelData.name; font.pixelSize: 14; color: settings.ink }
                                    Text {
                                        width: parent.width
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                        font.pixelSize: 12
                                        color: settings.inkSoft
                                        text: dr.modelData.readable ? dr.modelData.snippet : dr.modelData.ext.toUpperCase() + " · sorted by its name"
                                    }
                                }
                            }
                        }
                        Link {
                            visible: !tc.shut && !tc.all && tc.modelData.docs.length > tc.limit
                            text: "Show all " + tc.modelData.docs.length
                            font.underline: true
                            onClicked: { const e = Object.assign({}, root.expanded); e[tc.modelData.id] = true; root.expanded = e }
                        }
                    }
                }

                // how to sort
                Card {
                    Text { text: "Sort again"; font.pixelSize: 13; color: settings.inkSoft }
                    Flow {
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: [{ k: "nested", t: "Subjects › Topics" }, { k: "topics", t: "Topics only" }, { k: "spec", t: "Match to a spec" }]
                            Pill {
                                required property var modelData
                                label: modelData.t
                                selected: root.layout === modelData.k
                                onClicked: { root.layout = modelData.k; pods.setLayout(root.podId, modelData.k); root.specNote = "" }
                            }
                        }
                    }
                    Para {
                        color: settings.ink
                        text: root.layout === "nested" ? "Subjects, the topics in each, and the parts of each topic, for a folder with a bit of everything in it."
                            : root.layout === "topics" ? "One level of topics. Choose how wide they are:"
                            : "Match each document to the point of your course specification it covers, such as “3.2.1 Cell structure”."
                    }
                    Flow {
                        visible: root.layout === "topics"
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: [{ k: 0, t: "Broad" }, { k: 1, t: "Balanced" }, { k: 2, t: "Specific" }]
                            Pill { required property var modelData; label: modelData.t; selected: root.detail === modelData.k; onClicked: root.detail = modelData.k }
                        }
                    }

                    // the spec
                    Column {
                        visible: root.layout === "spec"
                        width: parent.width
                        spacing: 10
                        Para {
                            color: settings.ink
                            text: root.info.specName ? "Spec: " + root.info.specName + " · " + root.info.specPoints + " points"
                                : "Add your spec: the specification from your exam board's website (AQA, Edexcel, OCR, WJEC…) or your module's outline. "
                                  + "Choose the file, or open it, copy all of its text and paste it."
                        }
                        Flow {
                            width: parent.width
                            spacing: 10
                            Btn { label: root.info.specName ? "Choose another file" : "Choose a file"; primary: false; onClicked: specDialog.open() }
                            Btn { label: "Paste text"; primary: false; visible: !root.pasting; onClicked: { root.pasting = true; specBox.focusInput() } }
                        }
                        Column {
                            visible: root.pasting
                            width: parent.width
                            spacing: 10
                            Field { id: specName; width: root.narrow ? parent.width : 360; placeholder: "Name (for example AQA GCSE Biology)" }
                            TextBox { id: specBox; width: parent.width; minHeight: 140; placeholder: "Paste the spec's text here" }
                            Flow {
                                width: parent.width
                                spacing: 10
                                Btn {
                                    label: "Use this spec"
                                    onClicked: {
                                        const r = pods.setSpec(root.podId, specName.text, specBox.text)
                                        if (r.error) { root.specNote = r.error; return }
                                        root.specNote = "Found " + r.points + " points."
                                        root.pasting = false
                                        specBox.clear()
                                        pods.resort(root.podId, root.detail)
                                    }
                                }
                                Btn { label: "Cancel"; primary: false; onClicked: root.pasting = false }
                            }
                        }
                        Para { visible: root.specNote !== ""; text: root.specNote; color: root.specNote.indexOf("Found") === 0 ? settings.accent : root.warn }
                    }

                    Flow {
                        width: parent.width
                        spacing: 10
                        Btn { label: "Sort again"; enabled: !root.info.busy && (root.layout !== "spec" || !!root.info.specName); onClicked: pods.resort(root.podId, root.detail) }
                        Btn { label: "Check for new files"; primary: false; enabled: !root.info.busy; onClicked: pods.refresh(root.podId) }
                    }
                    Para {
                        text: { ai.revision
                            const m = ai.installed("meaning"), w = ai.installed("writer")
                            const how = root.layout === "spec"
                                ? (m ? "Matches by meaning, with the on-device AI." : "Matches by key words. Download the meaning model on the Pods page to match by meaning.")
                                : m && w ? "Sorts with the on-device AI: the meaning model groups your documents and the writer names each group."
                                : m ? "Sorts with the on-device AI's meaning model. Download the writer on the Pods page to have groups named and summed up."
                                : w ? "Groups by key words, then the writer names each group. Download the meaning model on the Pods page to group by meaning."
                                : "Sorts by key words. Download the free on-device AI on the Pods page to sort by meaning."
                            return how + " Sorting again replaces names you have changed; “Check for new files” keeps them." }
                    }
                }
                FileDialog {
                    id: specDialog
                    title: "Choose your spec"
                    nameFilters: ["Specifications (*.pdf *.docx *.odt *.txt *.md)", "All files (*)"]
                    onAccepted: {
                        const r = pods.readSpecFile(selectedFile.toString())
                        if (r.error) { root.specNote = r.error; return }
                        const s = pods.setSpec(root.podId, r.name, r.text)
                        if (s.error) { root.specNote = s.error; return }
                        root.specNote = "Found " + s.points + " points."
                        pods.resort(root.podId, root.detail)
                    }
                }

                // the real files
                Card {
                    Text { text: "Your files"; font.pixelSize: 13; color: settings.inkSoft }
                    Para {
                        color: settings.ink
                        text: root.info.moved ? "Your documents were moved into a folder for each topic, inside its subject's folder."
                                              : "Topics only exist in Streamline for now. Your files are where they were."
                    }
                    Link {
                        visible: !root.confirmMove && !root.info.busy
                        text: root.info.moved ? "Undo the move" : "Move the files into topic folders…"
                        font.underline: true
                        onClicked: {
                            if (root.info.moved) {
                                const r = pods.undoMove(root.podId)
                                root.note = "Put back " + root.plural(r.moved, "file") + (r.failed > 0 ? ". " + r.failed + " couldn't be moved: is one open in another app?" : ".")
                            } else {
                                const p = pods.movePlan(root.podId)
                                planText.text = p.files === 0 ? "Every document is already in its topic's folder."
                                    : "This makes " + root.plural(p.folders, "folder") + " inside " + root.info.folder + " ("
                                      + (p.names.length > 6 ? p.names.slice(0, 6).join(", ") + "…" : p.names.join(", ")) + ") and moves "
                                      + root.plural(p.files, "document") + " into them. Documents under Other stay where they are. You can undo this here."
                                moveBtn.visible = p.files > 0
                                root.confirmMove = true
                            }
                        }
                    }
                    Column {
                        visible: root.confirmMove
                        width: parent.width
                        spacing: 10
                        Para { id: planText; color: settings.ink }
                        Flow {
                            width: parent.width
                            spacing: 10
                            Btn {
                                id: moveBtn
                                label: "Move files"
                                onClicked: {
                                    const r = pods.moveFiles(root.podId)
                                    root.confirmMove = false
                                    root.note = "Moved " + root.plural(r.moved, "file") + (r.failed > 0 ? ". " + r.failed + " couldn't be moved: is one open in another app?" : ".")
                                }
                            }
                            Btn { label: "Cancel"; primary: false; onClicked: root.confirmMove = false }
                        }
                    }
                    Para { visible: root.note !== ""; text: root.note; color: settings.accent }
                }

                Item { width: 1; height: 8 }
                Link {
                    visible: !root.confirmDelete
                    text: "Delete this pod"
                    color: root.warn
                    onClicked: root.confirmDelete = true
                }
                Card {
                    visible: root.confirmDelete
                    Para { color: settings.ink; text: "Delete this pod? Streamline forgets its topics. Your documents aren't touched." }
                    Flow {
                        width: parent.width
                        spacing: 10
                        Btn { label: "Delete pod"; onClicked: { pods.remove(root.podId); root.confirmDelete = false; root.mode = "list"; root.podId = "" } }
                        Btn { label: "Cancel"; primary: false; onClicked: root.confirmDelete = false }
                    }
                }
            }

            // ---------------- one document ----------------
            Column {
                visible: root.mode === "doc"
                width: parent.width
                spacing: 14

                Link { text: "‹ " + (root.info.name || "Pod"); font.pixelSize: 14; onClicked: root.goBack() }
                Text { width: parent.width; wrapMode: Text.Wrap; text: root.docInfo.name || ""; font.pixelSize: 22; font.weight: Font.DemiBold; color: settings.ink }
                Text {
                    width: parent.width
                    wrapMode: Text.WrapAnywhere
                    font.pixelSize: 12
                    color: settings.inkSoft
                    text: root.docInfo.path ? (root.docInfo.ext || "").toUpperCase() + (root.docInfo.words ? " · " + root.plural(root.docInfo.words, "word") : "") + "\n" + root.docInfo.path : ""
                }
                Flow {
                    width: parent.width
                    spacing: 10
                    Btn { label: "Open"; onClicked: { if (!pods.openDoc(root.docPath)) root.note = "Nothing on this device could open it." } }
                }
                Para { visible: root.note !== "" && root.mode === "doc"; text: root.note; color: root.warn }

                Text { text: "Topic"; font.pixelSize: 13; color: settings.inkSoft }
                Flow {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: { pods.revision; return root.mode === "doc" ? pods.topicChoices(root.podId) : [] }
                        Pill {
                            required property var modelData
                            label: modelData.name
                            selected: modelData.id === root.docInfo.topicId
                            onClicked: if (!selected) pods.moveDoc(root.podId, root.docPath, modelData.id)
                        }
                    }
                }

                Text { text: "Preview"; font.pixelSize: 13; color: settings.inkSoft }
                Rectangle {
                    width: parent.width
                    height: preview.implicitHeight + 28
                    radius: settings.round ? 14 : 0
                    color: settings.boxFill
                    border.width: settings.round ? 0 : 1
                    border.color: settings.boxLine
                    TextEdit {
                        id: preview
                        x: 14; y: 14
                        width: parent.width - 28
                        readOnly: true
                        selectByMouse: !settings.mobile
                        wrapMode: TextEdit.Wrap
                        font.pixelSize: 14
                        color: settings.ink
                        textFormat: TextEdit.PlainText
                    }
                    Text {
                        visible: preview.text === ""
                        x: 14; y: 14
                        width: parent.width - 28
                        wrapMode: Text.Wrap
                        font.pixelSize: 13
                        color: settings.inkSoft
                        text: "Streamline can't read the text of this kind of file, so it was sorted by its name. Open it to see it."
                    }
                }
            }
        }
    }
}
