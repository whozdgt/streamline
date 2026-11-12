import QtQuick

// Flashcards: your decks, one deck's cards, and a study session.
// Space shows the answer (then counts as "Good"); 1 to 4 rate the card.
Item {
    id: root
    property int gutter: 36
    property real bottomMargin: 0
    property alias contentY: flick.contentY

    property string mode: "decks"          // decks | deck | study
    property string deckId: ""
    // a study session
    property var queue: []                 // card ids still to go; "Again" puts a card back at the end
    property bool shown: false             // the answer is showing
    property bool practice: false          // going through every card, not only what is due
    property int sessionTotal: 0
    property int sessionDone: 0
    property int sessionAgain: 0
    // the deck page
    property string editingId: ""
    property string confirmCardId: ""
    property bool confirmDeck: false
    property string tool: ""               // "" | "import"
    property string note: ""               // one line of feedback: "Imported 12 cards", "Copied"…

    readonly property bool narrow: width < 640
    readonly property var deckList: { flashcards.revision; return flashcards.decks() }
    readonly property var deckInfo: { flashcards.revision; return deckId !== "" ? flashcards.deck(deckId) : ({}) }
    readonly property var cardList: { flashcards.revision; return mode === "deck" && deckId !== "" ? flashcards.cards(deckId) : [] }
    readonly property string currentId: mode === "study" && queue.length > 0 ? queue[0] : ""
    readonly property var current: { flashcards.revision; return currentId !== "" ? flashcards.card(deckId, currentId) : ({}) }
    readonly property int todoAll: { flashcards.revision; return flashcards.todoTotal() }

    function plural(n, word) { return n + " " + word + (n === 1 ? "" : "s") }

    // Back (the phone's key, Escape, or the "‹" links): up one level. False when already at the deck list.
    function goBack() {
        if (mode === "study") { mode = "deck"; return true }
        if (mode === "deck") { mode = "decks"; deckId = ""; return true }
        return false
    }
    function openDeck(id) {
        deckId = id
        editingId = ""; confirmCardId = ""; confirmDeck = false; tool = ""; note = ""
        nameField.text = flashcards.deck(id).name || ""
        mode = "deck"
        flick.contentY = 0
    }
    function startStudy(all) {
        const src = flashcards.studyQueue(deckId, all)
        const q = []
        for (let i = 0; i < src.length; ++i) q.push(src[i])
        if (q.length === 0) return
        queue = q
        sessionTotal = q.length; sessionDone = 0; sessionAgain = 0
        shown = false
        practice = all
        mode = "study"
        flick.contentY = 0
    }
    function rate(rating) {
        if (!shown || currentId === "") return
        const id = currentId
        const q = queue.slice(1)
        if (rating === "again") { q.push(id); sessionAgain += 1 } else sessionDone += 1
        flashcards.rate(deckId, id, rating)
        shown = false
        queue = q
    }
    function addCard() {
        if (flashcards.addCard(deckId, frontBox.text, backBox.text)) {
            frontBox.clear(); backBox.clear(); note = ""
            frontBox.focusInput()
        } else if (frontBox.text.trim() === "") frontBox.focusInput()
        else backBox.focusInput()
    }

    component Link: Text {
        id: link
        signal clicked()
        font.pixelSize: 13
        color: settings.ink
        MouseArea { anchors.fill: parent; anchors.margins: -8; cursorShape: Qt.PointingHandCursor; onClicked: link.clicked() }
    }

    Shortcut { sequence: "Space"; enabled: root.visible && root.currentId !== ""; onActivated: { if (root.shown) root.rate("good"); else root.shown = true } }
    Shortcut { sequence: "1"; enabled: root.visible && root.currentId !== "" && root.shown; onActivated: root.rate("again") }
    Shortcut { sequence: "2"; enabled: root.visible && root.currentId !== "" && root.shown; onActivated: root.rate("hard") }
    Shortcut { sequence: "3"; enabled: root.visible && root.currentId !== "" && root.shown; onActivated: root.rate("good") }
    Shortcut { sequence: "4"; enabled: root.visible && root.currentId !== "" && root.shown; onActivated: root.rate("easy") }

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

            // ------------------------------------------------------------ your decks
            Column {
                visible: root.mode === "decks"
                width: parent.width
                spacing: 14

                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: 14
                    color: settings.inkSoft
                    text: root.deckList.length === 0 ? "Make a deck for each module, add cards, and study a little every day."
                        : root.todoAll > 0 ? root.plural(root.todoAll, "card") + " to study today."
                        : "Nothing due today. You're caught up."
                }

                Repeater {
                    model: root.deckList
                    Rectangle {
                        id: deckRow
                        required property var modelData
                        width: col.width
                        height: 68
                        radius: settings.round ? 16 : 0
                        color: dma.containsMouse ? settings.boxHover : settings.boxFill
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.name
                        Accessible.onPressAction: root.openDeck(modelData.id)
                        MouseArea { id: dma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openDeck(deckRow.modelData.id) }
                        Column {
                            anchors { left: parent.left; leftMargin: 18; right: studyBtn.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            spacing: 3
                            Text { width: parent.width; elide: Text.ElideRight; text: deckRow.modelData.name; font.pixelSize: 15; font.weight: Font.DemiBold; color: settings.ink }
                            Text {
                                width: parent.width; elide: Text.ElideRight
                                font.pixelSize: 12; color: settings.inkSoft
                                text: root.plural(deckRow.modelData.total, "card") + " · " + (deckRow.modelData.todo > 0 ? deckRow.modelData.todo + " to study" : "caught up")
                            }
                        }
                        Btn {
                            id: studyBtn
                            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                            label: "Study"
                            primary: deckRow.modelData.todo > 0
                            enabled: deckRow.modelData.total > 0
                            onClicked: { root.deckId = deckRow.modelData.id; root.startStudy(deckRow.modelData.todo === 0) }
                        }
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 10
                    Field {
                        id: newDeckField
                        width: Math.min(parent.width, 340)
                        placeholder: "New deck name, e.g. Organic Chemistry"
                        onAccepted: createBtn.clicked()
                    }
                    Btn {
                        id: createBtn
                        height: 44
                        label: "Create deck"
                        onClicked: {
                            const id = flashcards.createDeck(newDeckField.text)
                            if (id === "") { newDeckField.focusInput(); return }
                            newDeckField.clear()
                            root.openDeck(id)
                        }
                    }
                    Btn {
                        visible: root.deckList.length === 0
                        height: 44
                        primary: false
                        label: "Add an example deck"
                        onClicked: root.openDeck(flashcards.createSampleDeck())
                    }
                }
            }

            // ------------------------------------------------------------ one deck
            Column {
                visible: root.mode === "deck"
                width: parent.width
                spacing: 14

                Link { text: "‹ All decks"; onClicked: root.goBack() }

                Flow {
                    width: parent.width
                    spacing: 10
                    Field {
                        id: nameField
                        width: Math.min(parent.width, 340)
                        placeholder: "Deck name"
                        onAccepted: { flashcards.renameDeck(root.deckId, text); root.note = "Renamed." }
                    }
                    Text {
                        height: 44
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 12; color: settings.inkSoft
                        text: root.plural(root.deckInfo.total || 0, "card") + " · " + (root.deckInfo.due || 0) + " due · " + (root.deckInfo.fresh || 0) + " new"
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 10
                    Btn {
                        label: (root.deckInfo.todo || 0) > 0 ? "Study " + root.deckInfo.todo + " due" : "Nothing due today"
                        enabled: (root.deckInfo.todo || 0) > 0
                        onClicked: root.startStudy(false)
                    }
                    Btn { primary: false; label: "Practise all cards"; enabled: (root.deckInfo.total || 0) > 0; onClicked: root.startStudy(true) }
                }

                SectionLabel { text: "ADD A CARD" }
                Flow {
                    width: parent.width
                    spacing: 10
                    TextBox {
                        id: frontBox
                        width: root.narrow ? parent.width : (parent.width - 10) / 2
                        placeholder: "Question or term"
                        nextField: backBox
                        onSubmitted: root.addCard()
                    }
                    TextBox {
                        id: backBox
                        width: root.narrow ? parent.width : (parent.width - 10) / 2
                        placeholder: "Answer or definition"
                        nextField: frontBox
                        onSubmitted: root.addCard()
                    }
                }
                Flow {
                    width: parent.width
                    spacing: 10
                    Btn { label: "Add card"; onClicked: root.addCard() }
                    Btn { primary: false; label: root.tool === "import" ? "Hide import" : "Import many"; onClicked: { root.tool = root.tool === "import" ? "" : "import"; root.note = "" } }
                    Btn {
                        primary: false
                        label: "Copy all cards"
                        enabled: (root.deckInfo.total || 0) > 0
                        onClicked: { flashcards.copyText(flashcards.exportText(root.deckId)); root.note = "Copied " + root.plural(root.deckInfo.total, "card") + ". Paste into a spreadsheet or text file to keep a backup." }
                    }
                }
                Text {
                    visible: !settings.mobile && root.tool === "" && root.note === ""
                    font.pixelSize: 12; color: settings.inkSoft
                    text: "Ctrl+Enter adds the card. Tab moves between the two boxes."
                }
                Text { visible: root.note !== ""; width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 13; color: settings.ink; text: root.note }

                Column {
                    visible: root.tool === "import"
                    width: parent.width
                    spacing: 10
                    TextBox {
                        id: importBox
                        width: parent.width
                        minHeight: 130
                        placeholder: "One card per line: question ; answer\nTwo columns copied from a spreadsheet work too."
                    }
                    Flow {
                        width: parent.width
                        spacing: 10
                        Btn {
                            label: "Import cards"
                            onClicked: {
                                const n = flashcards.importText(root.deckId, importBox.text)
                                if (n === 0) { root.note = "No cards found. Put a semicolon or a tab between each question and its answer."; return }
                                importBox.clear()
                                root.tool = ""
                                root.note = "Imported " + root.plural(n, "card") + "."
                            }
                        }
                        Btn { primary: false; label: "Paste"; onClicked: importBox.text = flashcards.clipboardText() }
                    }
                }

                SectionLabel { text: "CARDS"; visible: root.cardList.length > 0 }
                Repeater {
                    model: root.cardList
                    Rectangle {
                        id: cardRow
                        required property var modelData
                        readonly property bool editing: root.editingId === modelData.id
                        readonly property bool confirming: root.confirmCardId === modelData.id
                        width: col.width
                        height: (editing ? editCol.implicitHeight : viewCol.implicitHeight) + 24
                        radius: settings.round ? 14 : 0
                        color: settings.boxFill

                        Column {
                            id: viewCol
                            visible: !cardRow.editing
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12; leftMargin: 16; rightMargin: 16 }
                            spacing: 6
                            Text { width: parent.width; wrapMode: Text.Wrap; text: cardRow.modelData.front; font.pixelSize: 14; font.weight: Font.DemiBold; color: settings.ink }
                            Text { width: parent.width; wrapMode: Text.Wrap; text: cardRow.modelData.back; font.pixelSize: 14; color: settings.inkSoft }
                            Row {
                                spacing: 22
                                topPadding: 4
                                Link {
                                    text: "Edit"
                                    onClicked: {
                                        editFront.text = cardRow.modelData.front
                                        editBack.text = cardRow.modelData.back
                                        root.confirmCardId = ""
                                        root.editingId = cardRow.modelData.id
                                    }
                                }
                                Link {
                                    text: cardRow.confirming ? "Confirm delete" : "Delete"
                                    font.weight: cardRow.confirming ? Font.Bold : Font.Normal
                                    onClicked: {
                                        if (!cardRow.confirming) { root.confirmCardId = cardRow.modelData.id; return }
                                        root.confirmCardId = ""
                                        flashcards.removeCard(root.deckId, cardRow.modelData.id)
                                    }
                                }
                                Link { visible: cardRow.confirming; text: "Keep"; onClicked: root.confirmCardId = "" }
                            }
                        }
                        Column {
                            id: editCol
                            visible: cardRow.editing
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                            spacing: 10
                            TextBox { id: editFront; width: parent.width; minHeight: 60; placeholder: "Question or term"; nextField: editBack }
                            TextBox { id: editBack; width: parent.width; minHeight: 60; placeholder: "Answer or definition"; nextField: editFront }
                            Row {
                                spacing: 10
                                Btn {
                                    label: "Save card"
                                    onClicked: {
                                        if (editFront.text.trim() === "" || editBack.text.trim() === "") return
                                        root.editingId = ""
                                        flashcards.updateCard(root.deckId, cardRow.modelData.id, editFront.text, editBack.text)
                                    }
                                }
                                Btn { primary: false; label: "Cancel"; onClicked: root.editingId = "" }
                            }
                        }
                    }
                }
                Text {
                    visible: root.cardList.length === 0
                    font.pixelSize: 14; color: settings.inkSoft
                    text: "This deck has no cards yet. Add your first one above."
                }

                Row {
                    spacing: 22
                    topPadding: 10
                    Link {
                        text: root.confirmDeck ? "Confirm: delete this deck and all its cards" : "Delete deck"
                        font.weight: root.confirmDeck ? Font.Bold : Font.Normal
                        onClicked: {
                            if (!root.confirmDeck) { root.confirmDeck = true; return }
                            const id = root.deckId
                            root.confirmDeck = false
                            root.goBack()
                            flashcards.removeDeck(id)
                        }
                    }
                    Link { visible: root.confirmDeck; text: "Keep deck"; onClicked: root.confirmDeck = false }
                }
            }

            // ------------------------------------------------------------ studying
            Column {
                visible: root.mode === "study"
                width: parent.width
                spacing: 14

                Link { text: "‹ " + (root.deckInfo.name || "Deck"); onClicked: root.goBack() }

                // --- a card to answer ---
                Column {
                    visible: root.currentId !== ""
                    width: parent.width
                    spacing: 14

                    Item {
                        width: parent.width
                        height: 18
                        Text { text: root.practice ? "PRACTICE" : "REVIEW"; font.pixelSize: 11; font.letterSpacing: 1; color: settings.inkSoft }
                        Text {
                            anchors.right: parent.right
                            font.pixelSize: 12; color: settings.inkSoft
                            text: root.sessionDone + " of " + root.sessionTotal + " done · " + root.queue.length + " left"
                        }
                    }
                    Rectangle {
                        width: parent.width; height: 4
                        radius: 2
                        color: settings.boxLine
                        Rectangle {
                            width: parent.width * (root.sessionTotal > 0 ? root.sessionDone / root.sessionTotal : 0)
                            height: parent.height; radius: 2
                            color: settings.accent
                            Behavior on width { NumberAnimation { duration: 180 } }
                        }
                    }

                    Rectangle {      // the card
                        id: face
                        width: parent.width
                        height: Math.max(280, faceCol.implicitHeight + 96)
                        radius: settings.round ? 20 : 0
                        color: settings.cardFill
                        border.width: 1
                        border.color: settings.boxLine
                        Accessible.role: Accessible.Button
                        Accessible.name: root.shown ? "Answer" : "Question. Press to show the answer"
                        Accessible.onPressAction: root.shown = true
                        Text {
                            x: 24; y: 18
                            text: root.shown ? "ANSWER" : "QUESTION"
                            font.pixelSize: 11; font.letterSpacing: 1; color: settings.inkSoft
                        }
                        Rectangle { x: 0; y: 46; width: parent.width; height: 1; color: settings.boxLine }      // an index card's top rule
                        Column {
                            id: faceCol
                            anchors { left: parent.left; right: parent.right; leftMargin: 28; rightMargin: 28; verticalCenter: parent.verticalCenter; verticalCenterOffset: 20 }
                            spacing: 14
                            Text {
                                visible: root.shown
                                width: parent.width
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                                text: root.current.front || ""
                                font.pixelSize: 13; color: settings.inkSoft
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                                text: (root.shown ? root.current.back : root.current.front) || ""
                                font.pixelSize: 21; color: settings.ink
                            }
                        }
                        MouseArea { anchors.fill: parent; enabled: !root.shown; cursorShape: Qt.PointingHandCursor; onClicked: root.shown = true }
                    }

                    Flow {
                        visible: !root.shown
                        width: parent.width
                        spacing: 14
                        Btn { label: "Show answer"; onClicked: root.shown = true }
                        Text { visible: !settings.mobile; height: 40; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; color: settings.inkSoft; text: "or press Space" }
                    }
                    Row {
                        id: rates
                        visible: root.shown
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: [{ k: "again", t: "Again" }, { k: "hard", t: "Hard" }, { k: "good", t: "Good" }, { k: "easy", t: "Easy" }]
                            Column {
                                id: rateItem
                                required property var modelData
                                required property int index
                                width: (rates.width - 3 * rates.spacing) / 4
                                spacing: 6
                                Btn {
                                    width: parent.width
                                    primary: rateItem.modelData.k === "good"
                                    label: rateItem.modelData.t
                                    onClicked: root.rate(rateItem.modelData.k)
                                }
                                Text {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    font.pixelSize: 11; color: settings.inkSoft
                                    text: { flashcards.revision; return root.currentId !== "" ? flashcards.intervalLabel(root.deckId, root.currentId, rateItem.modelData.k) : "" }
                                }
                            }
                        }
                    }
                    Text {
                        visible: root.shown
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: 12; color: settings.inkSoft
                        text: "How well did you know it? The time under each button is when the card comes back."
                            + (settings.mobile ? "" : " Keys 1 to 4, or Space for Good.")
                    }
                }

                // --- nothing left ---
                Column {
                    visible: root.mode === "study" && root.currentId === ""
                    width: parent.width
                    spacing: 12
                    Text { text: "Session complete"; font.pixelSize: 26; font.weight: Font.DemiBold; color: settings.ink }
                    Text {
                        font.pixelSize: 14; color: settings.ink
                        text: root.plural(root.sessionTotal, "card") + " reviewed · " + root.sessionAgain + " repeated"
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: 14; color: settings.inkSoft
                        text: {
                            flashcards.revision
                            const next = root.deckId !== "" ? flashcards.nextDueLabel(root.deckId) : ""
                            return next !== "" ? "The next card in this deck comes back in " + next + "." : "Every card is still due today."
                        }
                    }
                    Flow {
                        width: parent.width
                        spacing: 10
                        topPadding: 6
                        Btn { label: "Back to the deck"; onClicked: root.goBack() }
                        Btn { primary: false; label: "Practise all cards again"; onClicked: root.startStudy(true) }
                    }
                }
            }
        }
    }
}
