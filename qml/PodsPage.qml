import QtQuick
import QtQuick.Dialogs
import QtQuick.Shapes

// Pods: pick a folder of documents and Streamline sorts them into topics, or into the points of your course's spec.
// All pods are binders on a shelf. Inside one, it works like a file manager: its topics (or the spec's points) are
// folders, documents are sheets of paper, and the document you pick says where it was filed and why, so you can keep
// it there or move it. On a phone the folders are a list and a document opens on its own page.
Item {
    id: root
    property int gutter: 36
    property real bottomMargin: 0
    property alias contentY: flick.contentY
    signal chooseFolder()

    property string mode: "list"            // list | pod | doc (a phone: one document on its own page)
    property string podId: ""
    property string folder: ""              // the folder open in the pod: a topic's id, "" for the top, "check" for documents to check
    property var backStack: []
    property var forwardStack: []
    property string docPath: ""             // the document picked
    property string query: ""
    property string panel: ""               // "" | sort | more: the pod's settings, above its folders
    property bool moving: false             // the list of places to move the picked document to is open
    property string moveQuery: ""
    property int detail: 1                  // what the next "Sort" uses: 0 broad, 1 balanced, 2 specific
    property bool renaming: false
    property bool confirmMove: false
    property bool confirmDelete: false
    property string note: ""
    property string docNote: ""
    property string layout: "nested"        // what the next "Sort again" makes: nested | topics | spec
    property bool pasting: false            // the box for pasting a spec is open
    property string specNote: ""
    property bool finding: false            // the spec finder is open
    property string specBoard: ""
    property string specStage: ""
    property int specShown: 30
    readonly property var specResults: { specs.version; return finding ? specs.find(specQuery.text, specBoard, specStage) : [] }
    onSpecResultsChanged: specShown = 30
    property bool allowed: pods.filesAllowed()

    readonly property bool narrow: width < 640
    readonly property bool sidePanel: !narrow && docPath !== "" && mode === "pod"
    readonly property var podList: { pods.revision; return pods.pods() }
    readonly property var info: { pods.revision; return podId !== "" ? pods.pod(podId, query) : ({}) }
    readonly property var topics: info.topics || []
    readonly property color warn: "#e5484d"
    readonly property color amber: settings.round ? "#f5a524" : "#a35f00"
    // the colours a binder can be; a pod without one of its own takes one by its place on the shelf
    readonly property var colours: ["#1bcc5c", "#3d9bff", "#a78bfa", "#f5a524", "#ff5fa2", "#ff6b5e", "#2dd4bf"]
    function tintOf(id) {
        for (let i = 0; i < podList.length; ++i)
            if (podList[i].id === id) return podList[i].colour ? podList[i].colour : (i === 0 ? settings.ink : colours[i % colours.length])
        return settings.ink
    }
    readonly property color tint: tintOf(podId)
    function mixed(c, t) {      // c over the page's ground, t of the way: solid, so what is behind it is hidden
        const g = settings.ground
        return Qt.rgba(g.r + (c.r - g.r) * t, g.g + (c.g - g.g) * t, g.b + (c.b - g.b) * t, 1)
    }

    function plural(n, word) { return n + " " + word + (n === 1 ? "" : "s") }
    function siteOf(url) { const m = /^https?:\/\/(?:www\.)?([^\/]+)/.exec(url); return m ? m[1] : url }
    function byNumber(a, b) {      // spec points in order: 4.1.2 before 4.1.10
        const x = (a.number || "").split("."), y = (b.number || "").split(".")
        for (let i = 0; i < Math.max(x.length, y.length); ++i) {
            const p = parseInt(x[i] || "-1"), q = parseInt(y[i] || "-1")
            if (isNaN(p) || isNaN(q)) { const c = String(x[i] || "").localeCompare(String(y[i] || "")); if (c) return c; continue }
            if (p !== q) return p - q
        }
        return 0
    }
    function topicById(id) { for (let i = 0; i < topics.length; ++i) if (topics[i].id === id) return topics[i]; return null }
    function topicLabel(t) { return t ? (t.number ? t.number + " " + t.name : t.name) : "" }

    // what the open folder holds: the folders inside it and the documents directly in it
    readonly property var here: {
        const folders = [], docs = []
        if (root.query.trim() !== "") {      // a search: every document that matches, wherever it is
            for (let i = 0; i < topics.length; ++i)
                for (let j = 0; j < topics[i].docs.length; ++j) docs.push(Object.assign({ topicId: topics[i].id }, topics[i].docs[j]))
            return { folders: folders, docs: docs }
        }
        if (root.folder === "check") {
            for (let i = 0; i < topics.length; ++i)
                for (let j = 0; j < topics[i].docs.length; ++j)
                    if (topics[i].docs[j].check) docs.push(Object.assign({ topicId: topics[i].id }, topics[i].docs[j]))
            return { folders: folders, docs: docs }
        }
        let others = []
        for (let i = 0; i < topics.length; ++i) {
            const t = topics[i]
            if (root.folder === "" ? t.depth === 0 : (t.ancestors.length > 0 && t.ancestors[0] === root.folder)) (t.other ? others : folders).push(t)
            if (t.id === root.folder) for (let j = 0; j < t.docs.length; ++j) docs.push(Object.assign({ topicId: t.id }, t.docs[j]))
        }
        if (root.info.layout === "spec") folders.sort(root.byNumber)
        return { folders: folders.concat(others), docs: docs }
    }
    // the picked document, with the folder it is in
    readonly property var docInfo: {
        for (let i = 0; i < topics.length; ++i)
            for (let j = 0; j < topics[i].docs.length; ++j)
                if (topics[i].docs[j].path === docPath) return Object.assign({ topicId: topics[i].id, topicName: topicLabel(topics[i]), other: topics[i].other }, topics[i].docs[j])
        return ({})
    }
    // where you are: the folders from the top of the pod down to the open one
    readonly property var trail: {
        const out = []
        if (folder === "check") return [{ id: "check", name: "To check" }]
        for (let t = topicById(folder); t; t = t.ancestors.length ? topicById(t.ancestors[0]) : null) out.unshift({ id: t.id, name: topicLabel(t) })
        return out
    }

    Connections {      // a spec chosen from the list or as a file has been read
        target: pods
        function onSpecLoaded(id, r) {
            if (id !== root.podId) return
            if (r.error) { root.specNote = r.error; return }
            root.specNote = "Found " + root.plural(r.points, "point") + " in " + r.name + "."
            root.finding = false
        }
    }
    function goTo(f) {      // open a folder, as a step you can go back from
        if (f === folder) return
        backStack = backStack.concat([folder]); forwardStack = []
        folder = f; docPath = ""; moving = false
        flick.contentY = 0
    }
    function goBack() {
        if (mode === "doc") { mode = "pod"; docPath = ""; moving = false; return true }
        if (moving) { moving = false; return true }
        if (mode === "pod" && backStack.length > 0) {
            forwardStack = forwardStack.concat([folder])
            folder = backStack[backStack.length - 1]; backStack = backStack.slice(0, -1)
            docPath = ""
            return true
        }
        if (mode === "pod" && folder !== "") { folder = ""; docPath = ""; return true }
        if (mode === "pod") { mode = "list"; podId = ""; docPath = ""; return true }
        return false
    }
    function goForward() {
        if (forwardStack.length === 0) return
        backStack = backStack.concat([folder])
        folder = forwardStack[forwardStack.length - 1]; forwardStack = forwardStack.slice(0, -1)
        docPath = ""
    }
    function openPod(id) {
        podId = id; query = ""; note = ""; docNote = ""; renaming = false; confirmMove = false; confirmDelete = false
        folder = ""; backStack = []; forwardStack = []; docPath = ""; moving = false; panel = ""
        pasting = false; specNote = ""; finding = false
        const p = pods.pod(id)
        detail = p.detail !== undefined ? p.detail : 1
        layout = p.layout || "nested"
        mode = "pod"
        flick.contentY = 0
    }
    function openFinder(q) {      // the spec finder, open (at a search), scrolled into view
        panel = "sort"; layout = "spec"; finding = true; pasting = false; specNote = ""; specQuery.text = q
        specs.update()
        Qt.callLater(function() {
            const y = specSection.mapToItem(col, 0, 0).y
            flick.contentY = Math.max(0, Math.min(y - 70, flick.contentHeight - flick.height))
        })
    }
    function pickDoc(path) {
        docPath = path; moving = false; moveQuery = ""; docNote = ""
        if (narrow) {
            preview.text = pods.docText(path)
            mode = "doc"
            flick.contentY = 0
        }
    }
    onVisibleChanged: if (visible) allowed = pods.filesAllowed()
    Timer {      // --set startPick=true (pictures for CI): once sorted, open a folder with documents in it and pick one
        interval: 500; repeat: true
        running: !!settings.startPick && root.mode === "pod" && root.docPath === ""
        onTriggered: {
            if (root.info.busy || root.topics.length === 0) return
            if ((root.info.checkCount || 0) > 0) root.goTo("check")
            else for (let i = 0; i < root.topics.length; ++i) if (root.topics[i].docs.length > 0 && !root.topics[i].other) { root.goTo(root.topics[i].id); break }
            if (root.here.docs.length > 0) root.pickDoc(root.here.docs[0].path)
            running = false
        }
    }
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
        width: parent ? parent.width : 0
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
    component Label: Text {      // a small heading in capitals
        font.pixelSize: 11
        font.letterSpacing: 0.9
        font.capitalization: Font.AllUppercase
        color: settings.inkSoft
    }

    // ---- the pictures ----
    // a folder, front open; the files in it pop up out of it while you point at it
    component FolderIcon: Item {
        id: fi
        property color tint: settings.ink
        property int papers: 0
        property bool open: false
        width: 64; height: 50
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: fi.tint; strokeWidth: 2; joinStyle: ShapePath.RoundJoin
                fillColor: Qt.rgba(fi.tint.r, fi.tint.g, fi.tint.b, 0.22)
                PathSvg { path: "M4 8a4 4 0 0 1 4-4h16l6 6h26a4 4 0 0 1 4 4v28a4 4 0 0 1-4 4H8a4 4 0 0 1-4-4z" }
            }
        }
        Repeater {
            model: Math.min(3, fi.papers)
            Rectangle {
                id: paper
                required property int index
                // where each sheet sits in the folder, and where it pops to: x, then how far across, up and turned
                readonly property var spot: fi.papers === 1 ? [22, 0, -26, 2] : [[10, -6, -20, -14], [22, 0, -26, 2], [34, 6, -20, 14]][fi.papers === 2 ? index * 2 : index]
                x: spot[0] + (fi.open ? spot[1] : 0)
                y: 16 + (fi.open ? spot[2] : 0)
                rotation: fi.open ? spot[3] : 0
                width: 20; height: 26; radius: 3
                color: "#f2f4f3"
                border.width: 1.5; border.color: fi.tint
                Behavior on x { NumberAnimation { duration: 350; easing.type: Easing.OutBack } }
                Behavior on y { NumberAnimation { duration: 350 + paper.index * 60; easing.type: Easing.OutBack } }
                Behavior on rotation { NumberAnimation { duration: 350 + paper.index * 60; easing.type: Easing.OutBack } }
                Column {
                    x: 4; y: 6; spacing: 3
                    Repeater { model: 3; Rectangle { width: 12; height: 2; color: "#9aa39e" } }
                }
            }
        }
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: fi.tint; strokeWidth: 2; joinStyle: ShapePath.RoundJoin
                fillColor: root.mixed(fi.tint, 0.28)
                PathSvg { path: "M4 18h56v24a4 4 0 0 1-4 4H8a4 4 0 0 1-4-4z" }
            }
        }
    }
    // a sheet of paper with a corner turned down and the kind of file on it
    component SheetIcon: Item {
        id: si
        property string ext: ""
        property real size: 64
        width: size; height: size
        Item {
            width: 64; height: 64
            scale: si.size / 64
            transformOrigin: Item.TopLeft
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: "#c7ccc9"; strokeWidth: 1.5; joinStyle: ShapePath.RoundJoin; fillColor: "#f2f4f3"
                    PathSvg { path: "M18 4h20l10 10v45a1 1 0 0 1-1 1H18a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1z" }
                }
                ShapePath {
                    strokeColor: "#c7ccc9"; strokeWidth: 1.5; joinStyle: ShapePath.RoundJoin; fillColor: "#d9dedb"
                    PathSvg { path: "M38 4v9a1 1 0 0 0 1 1h9z" }
                }
            }
            Column {
                x: 22; y: 21; spacing: 4
                Repeater { model: 4; Rectangle { required property int index; width: index === 3 ? 13 : 20; height: 2; radius: 1; color: "#9aa39e" } }
            }
            Rectangle {
                x: 21; y: 46; width: 22; height: 9; radius: 2
                color: "#3a3a3c"
                Text { anchors.centerIn: parent; text: si.ext.toUpperCase().slice(0, 4); font.pixelSize: 6; font.bold: true; color: "white" }
            }
        }
    }
    // a lever arch binder, seen from its spine: a label to write on and a hole to pull it off the shelf
    component BinderIcon: Item {
        id: bi
        property color tint: settings.ink
        width: 64; height: 64
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: bi.tint; strokeWidth: 2; joinStyle: ShapePath.RoundJoin
                fillColor: Qt.rgba(bi.tint.r * 0.62, bi.tint.g * 0.62, bi.tint.b * 0.62, 1)
                PathSvg { path: "M19 2h26a4 4 0 0 1 4 4v52a4 4 0 0 1-4 4H19a4 4 0 0 1-4-4V6a4 4 0 0 1 4-4z" }
            }
            ShapePath {
                strokeColor: "transparent"
                fillColor: Qt.rgba(bi.tint.r * 0.4, bi.tint.g * 0.4, bi.tint.b * 0.4, 1)
                PathSvg { path: "M16 6a4 4 0 0 1 3-3h1v58h-1a4 4 0 0 1-3-3z" }
            }
            ShapePath {
                strokeColor: Qt.rgba(0.55 + bi.tint.r * 0.45, 0.55 + bi.tint.g * 0.45, 0.55 + bi.tint.b * 0.45, 1); strokeWidth: 2.5
                fillColor: "black"
                PathSvg { path: "M40 47a6.5 6.5 0 1 1-13 0 6.5 6.5 0 0 1 13 0z" }
            }
        }
        Rectangle {
            x: 23; y: 8; width: 21; height: 24; radius: 2
            color: "#f2f4f3"
            Column {
                x: 3.5; y: 5; spacing: 3
                Repeater { model: 3; Rectangle { required property int index; width: index === 2 ? 9 : 14; height: 2; radius: 1; color: "#9aa39e" } }
            }
        }
    }

    // ---- the tiles in the file manager ----
    component FolderTile: Rectangle {
        id: ft
        required property var topic
        signal clicked()
        readonly property bool hovered: ftMa.containsMouse
        height: ftCol.implicitHeight + 20
        radius: settings.round ? 10 : 0
        color: hovered ? Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.10) : "transparent"
        Column {
            id: ftCol
            y: 10
            width: parent.width
            spacing: 6
            FolderIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                tint: ft.topic.other ? settings.inkSoft : root.tint
                papers: ft.topic.total
                open: ft.hovered
            }
            Column {
                width: parent.width
                spacing: 3
                opacity: ft.hovered && ft.topic.total > 0 ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 180 } }
                Text { visible: !!ft.topic.number; width: parent.width - 8; x: 4; horizontalAlignment: Text.AlignHCenter; text: ft.topic.number; font.pixelSize: 11; font.bold: true; color: settings.ink; elide: Text.ElideRight }
                Text {
                    width: parent.width - 8; x: 4
                    horizontalAlignment: Text.AlignHCenter
                    text: ft.topic.name
                    font.pixelSize: 11; lineHeight: 1.1
                    wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                    color: ft.topic.other ? settings.inkSoft : settings.ink
                }
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: ft.topic.total === 0 ? "Empty" : root.plural(ft.topic.total, "file"); font.pixelSize: 11; color: settings.inkSoft }
            }
        }
        MouseArea { id: ftMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ft.clicked() }
    }
    component SheetTile: Rectangle {
        id: st
        required property var doc
        readonly property bool picked: root.docPath === doc.path
        height: stCol.implicitHeight + 20
        radius: settings.round ? 10 : 0
        color: picked ? settings.hiSel : stMa.containsMouse ? settings.boxHover : "transparent"
        Column {
            id: stCol
            y: 10
            width: parent.width
            spacing: 6
            SheetIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                size: 50; ext: st.doc.ext
                rotation: stMa.containsMouse ? -2 : 0
                Behavior on rotation { NumberAnimation { duration: 200 } }
            }
            Text {
                width: parent.width - 8; x: 4
                horizontalAlignment: Text.AlignHCenter
                text: st.doc.name
                font.pixelSize: 11; lineHeight: 1.1
                wrapMode: Text.WrapAnywhere; maximumLineCount: 2; elide: Text.ElideRight
                color: settings.ink
            }
            Text {
                visible: st.doc.check || root.folder === "check" || root.query.trim() !== ""
                width: parent.width - 8; x: 4
                horizontalAlignment: Text.AlignHCenter
                text: st.doc.check ? "Check" : (root.topicLabel(root.topicById(st.doc.topicId)) || "")
                font.pixelSize: 11; elide: Text.ElideRight
                color: st.doc.check ? root.amber : settings.inkSoft
            }
        }
        MouseArea { id: stMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.pickDoc(st.doc.path) }
    }
    // a phone's row: a folder or a document
    component Row2: Rectangle {
        id: r2
        property string icon: "folder"
        property color iconColour: root.tint
        property string title
        property string sub
        property color subColour: settings.inkSoft
        property bool first: false
        signal clicked()
        width: parent ? parent.width : 0
        height: Math.max(56, r2Col.implicitHeight + 20)
        color: r2Ma.pressed ? settings.boxHover : "transparent"
        Rectangle { visible: !r2.first; x: 50; width: parent.width - 50; height: 1; color: settings.boxLine }
        Icon { id: r2Icon; x: 14; anchors.verticalCenter: parent.verticalCenter; name: r2.icon; size: 22; color: r2.iconColour }
        Column {
            id: r2Col
            anchors { left: r2Icon.right; leftMargin: 14; right: r2Chev.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 2
            Text { width: parent.width; text: r2.title; textFormat: Text.StyledText; font.pixelSize: 14; color: settings.ink; elide: Text.ElideRight }
            Text { visible: r2.sub !== ""; width: parent.width; text: r2.sub; font.pixelSize: 12; color: r2.subColour; elide: Text.ElideRight }
        }
        Icon { id: r2Chev; anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter } name: "chevron"; size: 16; color: settings.inkSoft }
        MouseArea { id: r2Ma; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: r2.clicked() }
    }
    component Swatches: Flow {
        spacing: 10
        Repeater {
            model: root.colours
            Rectangle {
                required property string modelData
                required property int index
                readonly property bool sel: root.tint.toString().toLowerCase() === modelData.toLowerCase()
                width: 24; height: 24; radius: 12
                color: modelData
                border.width: sel ? 2 : 0
                border.color: settings.ink
                Rectangle { visible: parent.sel; anchors.centerIn: parent; width: 10; height: 10; radius: 5; color: settings.ground }
                Accessible.role: Accessible.RadioButton
                Accessible.name: ["Green", "Blue", "Purple", "Amber", "Pink", "Red", "Teal"][index]
                MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: pods.setColour(root.podId, parent.modelData) }
            }
        }
    }
    // what the picked document is, where it was filed, and what you can do with it
    component DocDetails: Column {
        spacing: 12
        readonly property var d: root.docInfo
        Text { width: parent.width; text: parent.d.name || ""; font.pixelSize: root.narrow ? 22 : 15; font.weight: Font.DemiBold; color: settings.ink; wrapMode: Text.Wrap }
        Text {
            width: parent.width
            text: parent.d.path ? (parent.d.ext || "").toUpperCase() + (parent.d.words ? " · " + root.plural(parent.d.words, "word") : "") + (parent.d.folder ? " · " + parent.d.folder : "") : ""
            font.pixelSize: 12; color: settings.inkSoft; wrapMode: Text.WrapAnywhere
        }
        Rectangle {
            visible: !root.narrow
            width: parent.width
            height: Math.min(140, snip.implicitHeight + 20)
            clip: true
            radius: settings.round ? 10 : 0
            color: settings.round ? settings.surface : settings.boxFill
            Text {
                id: snip
                x: 12; y: 10; width: parent.width - 24
                text: parent.parent.d.readable ? (parent.parent.d.snippet || "") : "Streamline can't read the text of this kind of file, so it was sorted by its name."
                font.pixelSize: 13; lineHeight: 1.2; wrapMode: Text.Wrap
                color: settings.inkSoft
            }
        }
        Column {
            width: parent.width
            spacing: 4
            Label { text: parent.parent.d.kept ? "Filed by you in" : root.info.layout === "spec" ? "Matched to" : "In" }
            Text {
                width: parent.width; wrapMode: Text.Wrap
                text: parent.parent.d.other ? "Not in the spec" : (parent.parent.d.topicName || "")
                font.pixelSize: 13; color: settings.ink
            }
            Text {
                visible: !!parent.parent.d.check
                width: parent.width; wrapMode: Text.Wrap
                text: "Streamline isn't sure about this one. Keep it here, or move it."
                font.pixelSize: 12; color: root.amber
            }
            Label { visible: !!parent.parent.d.second && !parent.parent.d.kept; topPadding: 6; text: parent.parent.d.other ? "Closest point" : "Close second" }
            Link {
                visible: !!parent.parent.d.second && !parent.parent.d.kept
                width: parent.width; wrapMode: Text.Wrap
                text: parent.parent.d.second || ""
                color: settings.inkSoft
                onClicked: {      // move it there
                    const c = pods.topicChoices(root.podId)
                    const want = parent.parent.d.second || ""
                    for (let i = 0; i < c.length; ++i)
                        if ((c[i].number ? c[i].number + " " + c[i].title : c[i].title) === want) { pods.moveDoc(root.podId, root.docPath, c[i].id); root.docNote = "Moved to " + want + "."; return }
                }
            }
        }
        Flow {
            width: parent.width
            spacing: 8
            Btn { visible: !parent.parent.d.kept; label: "Keep here"; onClicked: { pods.keepDoc(root.podId, root.docPath); root.docNote = "Kept here. Documents like it will go here too." } }
            Btn { label: "Move to…"; primary: !!parent.parent.d.kept; onClicked: { root.moving = !root.moving; root.moveQuery = ""; moveField.clear() } }
            Btn { label: "Open"; primary: false; onClicked: if (!pods.openDoc(root.docPath)) root.docNote = "Nothing on this device could open it." }
        }
        Para { visible: root.docNote !== ""; text: root.docNote; color: settings.accent }
        // where it can go
        Column {
            visible: root.moving
            width: parent.width
            spacing: 6
            Field { id: moveField; width: parent.width; placeholder: root.info.layout === "spec" ? "Search the spec's points" : "Search topics"; onTextChanged: root.moveQuery = text }
            Column {
                width: parent.width
                Repeater {
                    model: {
                        if (!root.moving) return []
                        pods.revision
                        const q = root.moveQuery.trim().toLowerCase()
                        const all = pods.topicChoices(root.podId)
                        return all.filter(c => q === "" || c.name.toLowerCase().indexOf(q) >= 0).slice(0, 40)
                    }
                    Rectangle {
                        id: mv
                        required property var modelData
                        width: parent.width
                        height: mvText.implicitHeight + 14
                        radius: settings.round ? 8 : 0
                        color: mvMa.containsMouse ? settings.boxHover : "transparent"
                        Text { id: mvText; x: 8; y: 7; width: parent.width - 16; text: mv.modelData.name; font.pixelSize: 13; color: settings.ink; wrapMode: Text.Wrap }
                        MouseArea {
                            id: mvMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: { pods.moveDoc(root.podId, root.docPath, mv.modelData.id); root.moving = false; root.docNote = "Moved. Documents like it will go there too." }
                        }
                    }
                }
            }
        }
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
            width: Math.min(root.width - 2 * root.gutter, root.mode === "pod" ? 1280 : 960)
            topPadding: 8
            spacing: 14

            // ---------------- all pods: binders on a shelf ----------------
            Column {
                visible: root.mode === "list"
                width: parent.width
                spacing: 14

                Para { text: "Pick a folder of documents and Streamline sorts them into topics, or into your course's spec. Your files stay where they are unless you choose to move them." }

                Card {
                    visible: !root.allowed
                    Para { color: settings.ink; text: "To read your documents, Streamline needs “All files access”. Turn it on for Streamline on the page that opens, then come back." }
                    Btn { label: "Allow access to files"; onClicked: pods.askFiles() }
                }

                Btn { label: "New pod"; enabled: root.allowed; onClicked: root.chooseFolder() }

                Rectangle {
                    visible: root.podList.length > 0
                    width: parent.width
                    height: shelf.implicitHeight + 24
                    radius: settings.round ? 14 : 0
                    color: settings.boxFill
                    border.width: settings.round ? 0 : 1
                    border.color: settings.boxLine
                    Grid {
                        id: shelf
                        x: 12; y: 12
                        width: parent.width - 24
                        columns: Math.max(3, Math.min(7, Math.floor(width / 118)))
                        columnSpacing: 8; rowSpacing: 8
                        Repeater {
                            model: root.podList
                            Rectangle {
                                id: bt
                                required property var modelData
                                readonly property color tint: root.tintOf(modelData.id)
                                width: (shelf.width - (shelf.columns - 1) * shelf.columnSpacing) / shelf.columns
                                height: btCol.implicitHeight + 20
                                radius: settings.round ? 10 : 0
                                color: btMa.containsMouse ? Qt.rgba(tint.r, tint.g, tint.b, 0.10) : "transparent"
                                Column {
                                    id: btCol
                                    y: 10; width: parent.width; spacing: 6
                                    BinderIcon {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        tint: bt.tint
                                        rotation: btMa.containsMouse ? -4 : 0
                                        Behavior on rotation { NumberAnimation { duration: 220 } }
                                    }
                                    Text { width: parent.width - 8; x: 4; horizontalAlignment: Text.AlignHCenter; text: bt.modelData.name; font.pixelSize: 12; font.bold: true; color: settings.ink; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                                    Text {
                                        width: parent.width - 8; x: 4; horizontalAlignment: Text.AlignHCenter
                                        text: bt.modelData.busy ? bt.modelData.status : bt.modelData.error ? bt.modelData.error : root.plural(bt.modelData.docs, "file")
                                        font.pixelSize: 11; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                                        color: bt.modelData.error ? root.warn : bt.modelData.busy ? settings.accent : settings.inkSoft
                                    }
                                    Text { visible: bt.modelData.check > 0 && !bt.modelData.busy; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: bt.modelData.check + " to check"; font.pixelSize: 11; color: root.amber }
                                }
                                MouseArea { id: btMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openPod(bt.modelData.id) }
                            }
                        }
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

            // ---------------- one pod, as a file manager ----------------
            Column {
                visible: root.mode === "pod"
                width: parent.width
                spacing: 12

                // back and forward, where you are, search and the pod's settings
                Flow {
                    width: parent.width
                    spacing: 10
                    Rectangle {      // back | forward, one box
                        width: 82; height: 36
                        radius: settings.round ? 8 : 0
                        color: settings.round ? settings.roundButton : "transparent"
                        border.width: settings.round ? 0 : 1; border.color: settings.boxLine
                        Rectangle { anchors.centerIn: parent; width: 1; height: 20; color: settings.boxLine }
                        Item {
                            width: 41; height: 36
                            Accessible.role: Accessible.Button; Accessible.name: "Back"
                            Icon { anchors.centerIn: parent; name: "back"; size: 16; color: settings.ink }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.goBack() }
                        }
                        Item {
                            x: 41; width: 41; height: 36
                            opacity: root.forwardStack.length > 0 ? 1 : 0.4
                            Accessible.role: Accessible.Button; Accessible.name: "Forward"
                            Icon { anchors.centerIn: parent; name: "chevron"; size: 16; color: settings.ink }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.goForward() }
                        }
                    }
                    Flow {      // Pods / the pod / its folders
                        width: root.narrow ? parent.width - 92 : Math.max(200, parent.width - 82 - 10 - 230 - 10 - sortPill.width - setPill.width - 20)
                        spacing: 6
                        Link { height: 36; verticalAlignment: Text.AlignVCenter; text: "Pods"; font.pixelSize: 14; color: settings.inkSoft; onClicked: { root.mode = "list"; root.podId = "" } }
                        Text { height: 36; verticalAlignment: Text.AlignVCenter; text: "/"; font.pixelSize: 14; color: settings.inkSoft }
                        Link {
                            height: 36; verticalAlignment: Text.AlignVCenter
                            text: root.info.name || ""; font.pixelSize: 14
                            font.bold: root.trail.length === 0
                            color: root.trail.length === 0 ? settings.ink : settings.inkSoft
                            onClicked: root.goTo("")
                        }
                        Repeater {
                            model: root.trail
                            Row {
                                required property var modelData
                                required property int index
                                spacing: 6
                                Text { height: 36; verticalAlignment: Text.AlignVCenter; text: "/"; font.pixelSize: 14; color: settings.inkSoft }
                                Link {
                                    height: 36; verticalAlignment: Text.AlignVCenter
                                    text: parent.modelData.name; font.pixelSize: 14
                                    font.bold: parent.index === root.trail.length - 1
                                    color: parent.index === root.trail.length - 1 ? settings.ink : settings.inkSoft
                                    onClicked: root.goTo(parent.modelData.id)
                                }
                            }
                        }
                    }
                    Field { width: root.narrow ? parent.width - sortPill.width - setPill.width - 20 : 230; height: 36; placeholder: "Search this pod"; onTextChanged: root.query = text }
                    Pill { id: sortPill; label: "Sort"; selected: root.panel === "sort"; height: 36; onClicked: root.panel = root.panel === "sort" ? "" : "sort" }
                    Pill { id: setPill; label: root.narrow ? "Settings" : "Pod settings"; selected: root.panel === "more"; height: 36; onClicked: root.panel = root.panel === "more" ? "" : "more" }
                }

                // what it is following, and how it was sorted
                Rectangle {
                    width: parent.width
                    height: banner.implicitHeight + 20
                    radius: settings.round ? 12 : 0
                    color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.10)
                    Flow {
                        id: banner
                        x: 14; y: 10; width: parent.width - 28
                        spacing: 6
                        Text {
                            text: root.info.busy ? root.info.status
                                : root.info.error ? root.info.error
                                : root.info.layout === "spec" && root.info.specName ? "Following <b>" + root.info.specName + "</b>"
                                : root.plural(root.info.docCount || 0, "document")
                            textFormat: Text.StyledText
                            font.pixelSize: 13
                            color: root.info.busy ? settings.accent : root.info.error ? root.warn : settings.ink
                            width: Math.min(implicitWidth, banner.width)
                            wrapMode: Text.Wrap
                        }
                        Text {
                            visible: !root.info.busy && !!root.info.sortedLabel
                            text: (root.info.layout === "spec" ? root.plural(root.info.specPoints || 0, "point") + " · " : "") + (root.info.sortedLabel || "")
                            font.pixelSize: 13; color: settings.inkSoft
                            width: Math.min(implicitWidth, banner.width); wrapMode: Text.Wrap
                        }
                        Link {
                            visible: !root.info.busy
                            text: root.info.layout === "spec" ? "Change spec" : "Match to your course's spec…"
                            font.underline: true
                            onClicked: root.openFinder("")
                        }
                    }
                }

                // documents Streamline isn't sure about
                Rectangle {
                    visible: (root.info.checkCount || 0) > 0 && root.folder !== "check" && root.query.trim() === "" && !root.info.busy
                    width: parent.width
                    height: 48
                    radius: settings.round ? 12 : 0
                    color: Qt.rgba(0.96, 0.65, 0.14, 0.14)
                    Text {
                        x: 14; anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 50; elide: Text.ElideRight
                        text: (root.info.checkCount === 1 ? "1 document might" : root.info.checkCount + " documents might") + " be in the wrong place"
                        font.pixelSize: 14; color: root.amber
                    }
                    Icon { anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter } name: "chevron"; size: 16; color: root.amber }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.goTo("check") }
                }

                // ---- the pod's settings: sorting and the spec ----
                Card {
                    visible: root.panel === "sort"
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
                        id: specSection
                        visible: root.layout === "spec"
                        width: parent.width
                        spacing: 10
                        Para {
                            color: settings.ink
                            visible: !!root.info.specName
                            text: "Spec: " + root.info.specName + " · " + root.plural(root.info.specPoints || 0, "point")
                        }
                        Link {
                            visible: !!root.info.specSource
                            text: "See it on " + root.siteOf(root.info.specSource || "")
                            font.underline: true
                            onClicked: Qt.openUrlExternally(root.info.specSource)
                        }
                        Para {
                            visible: !root.info.specName
                            text: "Add your course’s specification: find it in the list of exam boards’ specs (Cambridge, AQA, Edexcel, OCR), "
                                  + "or choose the file (PDF, Word or text), or paste its text."
                        }
                        Flow {
                            width: parent.width
                            spacing: 10
                            Btn {
                                label: root.info.specName ? "Find another spec" : "Find your spec"
                                primary: !root.info.specName
                                visible: !root.finding
                                onClicked: { root.openFinder(""); specQuery.focusInput() }
                            }
                            Btn { label: root.info.specName ? "Choose another file" : "Choose a file"; primary: false; onClicked: specDialog.open() }
                            Btn { label: "Paste text"; primary: false; visible: !root.pasting; onClicked: { root.pasting = true; root.finding = false; specBox.focusInput() } }
                        }

                        // the spec finder: the boards' specifications, downloaded from the board's own website
                        Column {
                            visible: root.finding
                            width: parent.width
                            spacing: 10
                            Field {
                                id: specQuery
                                width: root.narrow ? parent.width : 420
                                placeholder: "Search, for example “AQA GCSE Biology” or “9700”"
                                onAccepted: if (root.specResults.length === 1) specs.use(root.podId, root.specResults[0].id)
                            }
                            Flow {
                                width: parent.width
                                spacing: 8
                                Repeater {
                                    model: [{ k: "", t: "All boards" }, { k: "Cambridge", t: "Cambridge" }, { k: "AQA", t: "AQA" },
                                            { k: "Edexcel", t: "Edexcel" }, { k: "OCR", t: "OCR" }]
                                    Pill { required property var modelData; label: modelData.t; selected: root.specBoard === modelData.k; onClicked: root.specBoard = modelData.k }
                                }
                            }
                            Flow {
                                width: parent.width
                                spacing: 8
                                Repeater {
                                    model: [{ k: "", t: "Any level" }, { k: "gcse", t: "GCSE and IGCSE" }, { k: "alevel", t: "AS and A level" }]
                                    Pill { required property var modelData; label: modelData.t; selected: root.specStage === modelData.k; onClicked: root.specStage = modelData.k }
                                }
                            }
                            Column {
                                width: parent.width
                                spacing: 0
                                Repeater {
                                    model: root.specResults.slice(0, root.specShown)
                                    Rectangle {
                                        id: specRow
                                        required property var modelData
                                        required property int index
                                        width: parent.width
                                        height: rowCol.implicitHeight + 16
                                        color: rowMa.containsMouse ? settings.boxHover : "transparent"
                                        radius: settings.round ? 8 : 0
                                        Rectangle { visible: specRow.index > 0; width: parent.width; height: 1; color: settings.boxLine }
                                        Column {
                                            id: rowCol
                                            x: 8; y: 8
                                            width: parent.width - 16
                                            spacing: 2
                                            Text { width: parent.width; text: specRow.modelData.name; font.pixelSize: 14; color: settings.ink; wrapMode: Text.Wrap }
                                            Text {
                                                width: parent.width
                                                text: (specRow.modelData.exams ? "Exams " + specRow.modelData.exams + " · " : "") + "from " + specRow.modelData.site
                                                font.pixelSize: 12; color: settings.inkSoft; elide: Text.ElideRight
                                            }
                                        }
                                        MouseArea {
                                            id: rowMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: !specs.busy && !root.info.specBusy
                                            onClicked: { root.specNote = ""; specs.use(root.podId, specRow.modelData.id) }
                                        }
                                    }
                                }
                            }
                            Link {
                                visible: root.specResults.length > root.specShown
                                text: "Show all " + root.specResults.length
                                font.underline: true
                                onClicked: root.specShown = 1000
                            }
                            Para {
                                visible: root.specResults.length === 0
                                text: "No specs in the list match that. Try fewer words, or download it from your exam board’s website and choose the file."
                            }
                            Para {
                                font.pixelSize: 12
                                text: "Each spec is downloaded from its exam board’s own website and read on this device. "
                                      + root.plural(specs.count, "spec") + " in the list."
                            }
                            Link { text: "Close the list"; font.underline: true; onClicked: root.finding = false }
                        }

                        // downloading, then reading
                        Column {
                            visible: (specs.busy && specs.podId === root.podId) || !!root.info.specBusy
                            width: parent.width
                            spacing: 6
                            Text {
                                text: specs.busy ? specs.status : "Reading the spec…"
                                font.pixelSize: 13; color: settings.ink
                            }
                            Rectangle {
                                width: Math.min(parent.width, 360); height: 4
                                clip: true
                                color: Qt.rgba(settings.ink.r, settings.ink.g, settings.ink.b, 0.15)
                                Rectangle {
                                    height: parent.height
                                    color: settings.accent
                                    width: specs.busy && specs.progress >= 0 ? parent.width * specs.progress : parent.width * 0.3
                                    x: specs.busy && specs.progress >= 0 ? 0 : sweep.x
                                    Item { id: sweep; NumberAnimation on x { from: 0; to: 250; duration: 1100; loops: Animation.Infinite; running: !(specs.busy && specs.progress >= 0) } }
                                }
                            }
                            Link { visible: specs.busy; text: "Cancel"; font.underline: true; onClicked: specs.cancel() }
                        }
                        Para { visible: !!specs.error && specs.podId === root.podId && !specs.busy; text: specs.error; color: root.warn }

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
                        Btn { label: "Sort again"; enabled: !root.info.busy && (root.layout !== "spec" || !!root.info.specName); onClicked: { pods.resort(root.podId, root.detail); root.folder = ""; root.backStack = []; root.forwardStack = []; root.docPath = "" } }
                        Btn { label: "Check for new files"; primary: false; enabled: !root.info.busy; onClicked: pods.refresh(root.podId) }
                    }
                    Para {
                        text: { ai.revision
                            const m = ai.installed("meaning"), w = ai.installed("writer")
                            const how = root.layout === "spec"
                                ? (m ? "Matches by meaning, with the on-device AI, and learns from where you keep and move documents." : "Matches by key words. Download the meaning model on the Pods page to match by meaning.")
                                : m && w ? "Sorts with the on-device AI: the meaning model groups your documents and the writer names each group."
                                : m ? "Sorts with the on-device AI's meaning model. Download the writer on the Pods page to have groups named and summed up."
                                : w ? "Groups by key words, then the writer names each group. Download the meaning model on the Pods page to group by meaning."
                                : "Sorts by key words. Download the free on-device AI on the Pods page to sort by meaning."
                            return how + (root.layout === "spec" ? " Sorting again keeps the documents you have kept or moved." : " Sorting again replaces names you have changed; “Check for new files” keeps them.") }
                    }
                }
                FileDialog {
                    id: specDialog
                    title: "Choose your spec"
                    nameFilters: ["Specifications (*.pdf *.docx *.odt *.txt *.md)", "All files (*)"]
                    onAccepted: { root.specNote = ""; root.finding = false; pods.loadSpecFile(root.podId, selectedFile.toString()) }
                }

                // ---- the pod's settings: name, colour, the real files ----
                Card {
                    visible: root.panel === "more"
                    Label { text: "Name" }
                    Flow {
                        visible: !root.renaming
                        width: parent.width
                        spacing: 12
                        Text { text: root.info.name || ""; font.pixelSize: 16; font.weight: Font.DemiBold; color: settings.ink; height: 32; verticalAlignment: Text.AlignVCenter }
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

                    Label { topPadding: 8; text: "Colour" }
                    Swatches { width: parent.width }
                    Para { text: "Its binder and the folders inside it take this colour." }

                    Label { topPadding: 8; text: "Your files" }
                    Para {
                        color: settings.ink
                        text: root.info.moved ? "Your documents were moved into a folder for each topic, inside its subject's folder."
                                              : "Folders only exist in Streamline for now. Your files are where they were."
                    }
                    Link {
                        visible: !root.confirmMove && !root.info.busy
                        text: root.info.moved ? "Undo the move" : "Move the files into these folders…"
                        font.underline: true
                        onClicked: {
                            if (root.info.moved) {
                                const r = pods.undoMove(root.podId)
                                root.note = "Put back " + root.plural(r.moved, "file") + (r.failed > 0 ? ". " + r.failed + " couldn't be moved: is one open in another app?" : ".")
                            } else {
                                const p = pods.movePlan(root.podId)
                                planText.text = p.files === 0 ? "Every document is already in its folder."
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

                    Item { width: 1; height: 4 }
                    Link {
                        visible: !root.confirmDelete
                        text: "Delete this pod"
                        color: root.warn
                        onClicked: root.confirmDelete = true
                    }
                    Column {
                        visible: root.confirmDelete
                        width: parent.width
                        spacing: 10
                        Para { color: settings.ink; text: "Delete this pod? Streamline forgets its folders. Your documents aren't touched." }
                        Flow {
                            width: parent.width
                            spacing: 10
                            Btn { label: "Delete pod"; onClicked: { pods.remove(root.podId); root.confirmDelete = false; root.mode = "list"; root.podId = "" } }
                            Btn { label: "Cancel"; primary: false; onClicked: root.confirmDelete = false }
                        }
                    }
                }

                // ---- the folder: its folders and documents, and the document picked ----
                Item {
                    width: parent.width
                    height: Math.max(filesBox.height, side.visible ? side.height : 0)

                    Rectangle {
                        id: filesBox
                        width: root.sidePanel ? parent.width - 300 - 12 : parent.width
                        height: Math.max(root.narrow ? 0 : 320, filesCol.implicitHeight + 16)
                        radius: settings.round ? 14 : 0
                        color: root.narrow ? "transparent" : settings.boxFill
                        border.width: settings.round || root.narrow ? 0 : 1
                        border.color: settings.boxLine

                        Column {
                            id: filesCol
                            x: root.narrow ? 0 : 12; y: 8
                            width: parent.width - (root.narrow ? 0 : 24)
                            spacing: 8

                            Label {
                                leftPadding: root.narrow ? 0 : 4; topPadding: 4
                                visible: text !== ""
                                text: root.query.trim() !== "" ? root.plural(root.here.docs.length, "document") + " found"
                                    : root.folder === "check" ? root.plural(root.here.docs.length, "document") + " to check"
                                    : [root.here.folders.length > 0 ? root.plural(root.here.folders.length, root.info.layout === "spec" ? "point" : "folder") : "",
                                       root.here.docs.length > 0 ? root.plural(root.here.docs.length, "document") : ""].filter(x => x).join(" · ")
                            }
                            Para {
                                visible: !root.info.busy && root.here.folders.length === 0 && root.here.docs.length === 0
                                topPadding: 8
                                text: root.query.trim() !== "" ? "Nothing in this pod matches “" + root.query.trim() + "”."
                                    : root.folder === "check" ? "Nothing to check: you've been through them all."
                                    : root.folder === "" ? "No folders yet." : "This folder is empty."
                            }
                            Para {
                                visible: root.folder === "check" && root.here.docs.length > 0
                                text: "Streamline matched these, but not by much. Pick one to see where else it could go, then keep it or move it. Documents like the ones you file follow them."
                            }

                            // a computer: folders and sheets in a grid
                            Grid {
                                visible: !root.narrow
                                width: parent.width
                                columns: Math.max(3, Math.min(7, Math.floor(width / 104)))
                                columnSpacing: 6; rowSpacing: 6
                                readonly property real cell: (width - (columns - 1) * columnSpacing) / columns
                                Repeater {
                                    model: root.narrow ? [] : root.here.folders
                                    FolderTile { required property var modelData; topic: modelData; width: parent.cell; onClicked: root.goTo(modelData.id) }
                                }
                                Repeater {
                                    model: root.narrow ? [] : root.here.docs
                                    SheetTile { required property var modelData; doc: modelData; width: parent.cell }
                                }
                            }

                            // a phone: a list
                            Rectangle {
                                visible: root.narrow && root.here.folders.length > 0
                                width: parent.width
                                height: folderRows.implicitHeight
                                radius: settings.round ? 14 : 0
                                color: settings.boxFill
                                border.width: settings.round ? 0 : 1; border.color: settings.boxLine
                                clip: true
                                Column {
                                    id: folderRows
                                    width: parent.width
                                    Repeater {
                                        model: root.narrow ? root.here.folders : []
                                        Row2 {
                                            required property var modelData
                                            required property int index
                                            first: index === 0
                                            iconColour: modelData.other ? settings.inkSoft : root.tint
                                            title: (modelData.number ? "<b>" + modelData.number + "</b> " : "") + modelData.name
                                            sub: modelData.total === 0 ? "Empty" : root.plural(modelData.total, "document")
                                            onClicked: root.goTo(modelData.id)
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                visible: root.narrow && root.here.docs.length > 0
                                width: parent.width
                                height: docRows.implicitHeight
                                radius: settings.round ? 14 : 0
                                color: settings.boxFill
                                border.width: settings.round ? 0 : 1; border.color: settings.boxLine
                                clip: true
                                Column {
                                    id: docRows
                                    width: parent.width
                                    Repeater {
                                        model: root.narrow ? root.here.docs : []
                                        Row2 {
                                            required property var modelData
                                            required property int index
                                            first: index === 0
                                            icon: "doc"
                                            iconColour: settings.inkSoft
                                            title: modelData.name
                                            sub: modelData.check ? "Check where this goes" : (root.folder === "check" || root.query.trim() !== "") ? root.topicLabel(root.topicById(modelData.topicId)) : (modelData.readable ? modelData.snippet : modelData.ext.toUpperCase())
                                            subColour: modelData.check ? root.amber : settings.inkSoft
                                            onClicked: root.pickDoc(modelData.path)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // the document picked (a computer)
                    Rectangle {
                        id: side
                        visible: root.sidePanel
                        x: parent.width - width
                        width: 300
                        height: details.implicitHeight + 32
                        radius: settings.round ? 14 : 0
                        color: settings.boxFill
                        border.width: settings.round ? 0 : 1; border.color: settings.boxLine
                        DocDetails { id: details; x: 16; y: 16; width: parent.width - 32 }
                        Icon {
                            anchors { right: parent.right; top: parent.top; margins: 12 }
                            name: "close"; size: 16; color: settings.inkSoft
                            MouseArea { anchors.fill: parent; anchors.margins: -8; cursorShape: Qt.PointingHandCursor; onClicked: { root.docPath = ""; root.moving = false } }
                        }
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: settings.inkSoft
                    textFormat: Text.StyledText
                    text: root.plural(root.info.docCount || 0, "document")
                          + ((root.info.checkCount || 0) > 0 ? " · <font color=\"" + root.amber + "\">" + root.info.checkCount + " to check</font>" : "")
                          + " · " + (root.info.moved ? "Your files were moved into these folders; you can undo that in Pod settings." : "Your files stay where they are until you choose to move them.")
                }
            }

            // ---------------- one document (a phone) ----------------
            Column {
                visible: root.mode === "doc"
                width: parent.width
                spacing: 14

                Link { text: "‹ " + (root.trail.length ? root.trail[root.trail.length - 1].name : (root.info.name || "Pod")); font.pixelSize: 14; onClicked: root.goBack() }
                DocDetails { width: parent.width }

                Label { text: "Preview" }
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
