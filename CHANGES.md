# Streamline 0.8.3: a steadier phone, and Apple's devices
- **The bottom bar stays still while you scroll.** On a phone, scrolling down tucks the tabs away and scrolling up brings them back, but a list that moved because the bars changed size (or bounced at its end) counted as scrolling too, so they tucked and untucked over and over and the mini player jumped about. Now only your own scrolling counts.
- **Pause from anywhere on Android.** The lock screen's, the notification's and a phone's pop-up ("island") controls showed the music as paused, but it carried on until you opened the app; next and previous waited too, and with the screen off the next song didn't start. Streamline now keeps running in the background, so they work straight away.
- **Settings › AI.** The on-device AI models are downloaded and removed in Settings, under AI. The Pods page says what's downloaded and links there.
- **iPhone, iPad and Mac.** Streamline builds for Apple's devices too, on the apple-latest release: an iPhone and iPad app to install with your own Apple ID (through SideStore, AltStore or Sideloadly) and a Mac app for Apple silicon. On an iPhone, a pod's folder comes in from the Files app. See APPLE.md.

# Streamline 0.8.2: every course in the spec finder, each one checked
- **467 specifications for 346 courses** in the spec finder (it had 153 for 130): AQA's, Pearson Edexcel's and OCR's GCSEs, AS and A levels, Edexcel's International GCSEs and International A levels, and Cambridge's IGCSEs (the 9–1 ones too), AS & A Levels and AS Levels. Besides the sciences, maths and humanities, that's languages (first and second languages too), the arts, PE, Law, Philosophy, Politics, Classics, Statistics, Engineering, Enterprise, Marine Science, Thinking Skills and more. Cambridge's new syllabuses are in, such as IGCSE Business (0264), which replaces Business Studies from 2027.
- **The syllabus for your exams.** Cambridge publishes a syllabus for each run of exam years, and the finder shows each one's ("Exams 2027–2029"). One whose exams are over isn't offered any more, and once the summer's exams are over the one for next year's exams comes first.
- **Every spec checked.** Each specification in the list was downloaded and read with the app's own reader, and checked for points read wrongly: numbers that end up in the wrong topic, a document's numbering read twice, headings that are really sentences, tables of marks, points with too little or far too much text, and points so alike that a document could go to either. What turned up was fixed, for example:
  - Edexcel GCSE Religious Studies: each religion keeps its own four sections; before, Islam's "3.1 The Qur'an" was taken for Catholic Christianity's 3.1.
  - Cambridge IGCSE PE: 53 points instead of 4 (its subtopics aren't numbered).
  - OCR's tables of topics: Ancient History's "Education in Sparta" and the other key topics, Law's components and sections ("Civil courts and other forms of dispute resolution"), GCSE History A's studies.
  - Edexcel International GCSE Maths: topics 3 and 5 were lost to a worked example's numbers ("5 8 12").
  - Older Cambridge syllabuses with "5.1 Paper 1" (Islamiyat), Edexcel A level Maths' topic names, International A level Economics, International GCSE Physics, OCR Chemistry B's storylines, OCR A Level Business's tables, AQA's new language specs, and more.

  Pods already matched to one of these specs: tap Sort again.
- **Tested with notes, subject by subject.** Students' notes were written for 63 courses, at least one in each subject: 761 of 775 went to the right point, 8 to a wrong one (5 of those marked to check) and 6 were left for you to file. 5 of 195 notes from other subjects were filed. Music is the weakest: a note about one area of study can go to another.
- Still rough: specs whose content is mostly skills or set texts (English Literature, languages, art, drama, music, some Classics) have few points to match to, so documents are sorted coarsely. WJEC/Eduqas and CCEA aren't in the finder yet.

# Streamline 0.8.1: A Level specs with papers
- **Cambridge A Level History (9489) and specs like it are read properly.** Papers 1 and 2 number their topics 1–9, and Paper 4 numbers its own 1–9 again: these got mixed up, so Paper 4's key questions (Mussolini's Italy, Stalin's Russia…) landed inside AS topic 9, and documents about the peace treaties or the League of Nations went under "Not in the spec". Each paper now keeps its own points, and a number used twice is shown with its paper ("P4 1.1"). Pods matched to such a spec: tap Sort again.
- **History notes are filed.** A Level key questions are alike ("How effectively did … govern …?"), so a note stood out less from the others and was left out. A document that comes close to a point's own text is now filed, marked to check if Streamline isn't sure. On A Level History notes written for the test (the Treaties of Trianon and Sèvres, Wilson's Fourteen Points, the Covenant, the Conference of Ambassadors, Mussolini and Hitler, and more), 22 of 22 went to the right key question.
- Across all the test notes (AQA Biology, Chemistry, Geography, Computer Science; Cambridge IGCSE Physics and A Level History): 181 of 184 to the right point, 2 to a wrong one (both marked to check), 1 left for you; 1 of 25 notes from other subjects filed.

# Streamline 0.8: Pods as a file manager, and better spec matching
- **Binders and folders.** All your pods are binders on a shelf, each in a colour you choose (Pod settings › Colour). Inside a pod it works like a file manager: back and forward, where you are along the top, the topics or the spec's points as folders, and documents as sheets of paper. Point at a folder and its files pop out. On a phone the folders are a list.
- **Why a document is where it is.** Pick a document to see the point it was matched to and the close second. **Keep here** if it's right, or **Move to…** any point of the spec, even one with nothing in it yet.
- **To check.** Documents Streamline matched, but not by much, are marked "Check" and gathered under "N documents might be in the wrong place", so you can go through them in one go.
- **Learns from you.** Documents you keep or move stay where you put them when the pod is sorted again, and new documents like them lean towards the same point.
- **More accurate matching.** A document has to come close to a point's own text, not just closer than to the others, so notes from other subjects (a Cold War essay in a Biology pod) stay out. Catch-all points such as AQA's "Key ideas" no longer collect stray documents, and shorter specs (AQA GCSE Geography, Computer Science) match as well as long ones. Tested on students' notes for AQA Biology, Chemistry, Geography and Computer Science and Cambridge IGCSE Physics: 159 of 162 notes went to the right point, 1 to a wrong one (marked to check) and 2 were left for you to file; 1 of 22 notes from other subjects was filed. "Check for new files" now matches new documents to the spec too.
- The Study tab has a new icon: a card and a circle, for your pods and the AI that sorts them.

# Streamline 0.7: exam boards' specs, and PDFs
- **Find your spec.** In a pod, tap "Match these to your course's spec…" (or Sort again › Match to a spec › Find your spec) for a list of 153 specifications: Cambridge (IGCSE, AS & A Level), AQA (GCSE, AS and A-level), Pearson Edexcel (GCSE, International GCSE, A level, International A level) and OCR (GCSE, A Level). Search by board, level, subject or code ("aqa biology", "9700"). Streamline downloads the spec from the board's own website, reads it on your device and matches your documents to it. The list holds only the boards' links; when a board moves a document, the app picks up a corrected list by itself.
- **PDFs.** Pods read PDF documents now (the text of the first 120 pages), and a spec can be a PDF. Protected PDFs that open without a password work (AQA's are like this); scanned pages, which are pictures of text, can't be read.
- **Knows the boards' layouts.** The spec reader was taught on the boards' real specifications: Cambridge's topics and numbered subtopics (Core and Extended together), AQA's four levels of numbering and History's options, Edexcel's papers, options and key topics, International GCSE sections and lettered parts, A level Maths' topics and statements, and OCR's topics and sub-topics. Contents pages, introductions, assessment details and appendices are left out. A spec chosen as a file is named after its course, for example "AQA GCSE Biology (8461)".
- A spec is read in the background, so the app doesn't pause on a long one. A spec chosen while a pod is still sorting is matched as soon as the sort ends.

# Streamline 0.6: nested pods and spec matching
- **Subjects › Topics › Subtopics.** A pod can sort a messy folder in levels: each subject, the topics in it, and the parts of each topic. New pods start this way. With the writer model, each level gets its own name ("History", then "Cold War", then "Cuban Missile Crisis"); subjects fold open and shut.
- **Match to a spec.** Add your own course specification (from your exam board's website, or a module outline: a Word or text file, or pasted text) and each document is matched to the point it covers, such as "3.2.1 Cell structure", inside its section. Documents that match nothing go under "Not in the spec". With the meaning model it matches by meaning; without it, by key words. A spec's contents page is skipped.
- **Moving the files** follows the layout: folders inside folders (`History/Cold War/…`, or `1 History/1.2 The Cold War/1.2.1 Origins…`). Undo puts everything back and removes the folders it made.
- "Topics only" is still there, with broad, balanced and specific.

# Streamline 0.5: on-device AI for Pods
- **Free AI that runs on your device.** Two open models read your documents and sort them by what they're about. Nothing is sent anywhere, there's nothing to pay, and once downloaded they work offline.
  - **Meaning model** (BGE small, 37 MB): reads each document's name, headings and text, in pieces through long documents, and groups documents by meaning, even when they share no words.
  - **Writer model** (Qwen2.5 0.5B Instruct, 491 MB): reads the documents in each topic and names the topic, with a one-line summary.
- **Welcome screen** the first time Streamline opens, offering the models. "Not now" is fine: they can be downloaded, cancelled or removed any time on the Pods page. Downloads carry on from where they stopped and are checked before use.
- Without the models, Pods sort by key words as before. "Sort with Claude" and the API key are gone.

# Streamline 0.4: Pods
- **Pods** (Study › Pods on a phone, Pods in the sidebar on a computer): pick a folder of documents and Streamline sorts them into topics, named after what the documents in each share. Reads Word (.docx), OpenDocument (.odt), text and Markdown files, including the folders inside; older Word (.doc), RTF and PDF files are sorted by their name.
- Choose broad, balanced or specific topics. Rename topics, move a document to another topic, search a pod, and preview a document's text or open it in Word.
- Sorting happens on your device. **Sort with Claude** is optional: with your own Anthropic API key it gives better topic names and a summary of each topic.
- Topics live in Streamline until you choose **Move the files into topic folders**, which makes a folder per topic and moves the files into them. It can be undone.
- "Check for new files" adds new documents to the topic they fit best and keeps the topic names you changed.
- On Android, Pods ask once for "All files access", which reading documents needs.
- On a phone, the player in the bottom bar shows previous, play and next instead of the song's name. The album-colour glow reaches the top and bottom of the screen, and Settings covers it.

# Streamline 0.3.1: Linux package, and a tidier phone layout
- **Linux:** a ready-made package for CachyOS, Arch, EndeavourOS and Manjaro (any desktop, including Hyprland): `streamline-x86_64.pkg.tar.zst` on the `linux-latest` release, installed with `sudo pacman -U`. pacman brings everything it needs. Settings › About › Update now installs newer packages through pacman.
- On Linux, Streamline now has its own names: the program `streamline`, its own menu entry and icon, the window class `streamline` (for Hyprland rules), and its own media-key name. It no longer installs over Paraplay.
- **Phone tabs** along the bottom are icons only now.
- **Add a server** on a phone: the tabs, the address hint and the buttons fit the screen. The address example sits under the box, and the buttons stack (Add server across the top, Test connection and Cancel below).
- Text-box hints that are too long end with "…" instead of running out of the box, and long setting names wrap instead of being cut short.
- An empty library's message wraps to fit a phone screen.

# Streamline 0.3: Android
- **Streamline for Android.** It has its own app ID (`io.github.whozdgt.streamline`), so it installs beside Paraplay and keeps its own settings, music library, flashcards and to-dos.
- Everything the Windows app has: music, flashcards (the Study tab) and to-dos (the To-dos tab). Signed in, to-dos sync with Dayline and with Streamline on your computer.
- Every push to `main` builds it. The newest APK is always `Streamline-android.apk` on the `android-latest` release, and **Settings › About › Update now** downloads newer builds and hands them to Android's installer (Android asks once to allow installs from Streamline).
- Builds are signed with a private key kept in GitHub's secrets, so nobody else can make an APK that Android would accept as an update.
  Builds 1 and 2 were signed with a key that was public in this repository for a day; from build 3 on, that key only signs
  the hand-over to the private one (Android's key rotation), so a phone with build 1 or 2 still updates normally and keeps its data.

# Streamline 0.2.1: updates that install themselves on Windows
- **Settings › About › Check now works on Windows.** It used to ask GitHub for a kind of release Streamline doesn't publish, so it always failed. It now reads the newest Windows build directly.
- **Update now** downloads the new installer, checks it isn't damaged, installs it without any questions, and opens Streamline again. Your settings, flashcards and to-dos stay.
- The version in About now shows its build number, e.g. "0.2.1 (build 4)".
- Copies installed before this version can't update themselves: install this one by hand once, and later updates work from inside the app.

# Streamline 0.2: to-dos from Dayline
- **To-dos** (sidebar, under Study; the To-dos tab on a phone): add a to-do and press Enter, tick it off with the circle, tap the rest of the row to edit it. Each to-do can have notes, a due date, a colour, "In progress" and a flag.
- Filters: All, Today, In progress, Overdue, Flagged. Search, and sort by newest, due date, flagged, colour or A to Z. Finished to-dos gather under Done, and "Clear done" removes them.
- **Shared with Dayline:** signed in, the list syncs through your account with the same records Dayline uses, so a to-do added on your phone in Dayline shows up here and the other way round. Whichever copy was edited last wins. Calendar events are left alone for now.
- Signed out, to-dos are saved on this device (`todos.json`). Signing in sends them into your account the first time.
- The sidebar shows how many to-dos are due today or overdue.
- The hint text in text boxes is now readable on the Midnight theme.

# Streamline 0.1: the music player gains flashcards
- **Streamline** is a new app built on Paraplay's code: the same music player, with study tools being added beside it. It keeps its own settings and data, so it can sit next to Paraplay on the same computer.
- **Flashcards** (sidebar, under Study; the Study tab on a phone): make a deck for each module, add cards, and study what is due. Rate each card Again, Hard, Good or Easy: hard cards come back sooner, easy ones are spaced further apart. "Practise all cards" goes through a whole deck whatever is due.
- Keys while studying: Space shows the answer (and then counts as Good), 1 to 4 rate the card. Ctrl+Enter adds a card.
- **Import many:** one card per line as `question ; answer`, or two columns pasted from a spreadsheet. **Copy all cards** puts a deck on the clipboard as a backup.
- Decks are saved on this device (`flashcards.json` in the app's data folder). Syncing them through your account is planned.
- Windows only for now: the Android and iPhone builds are switched off until Streamline has its own app IDs there.
- Not yet: to-dos and the calendar (from Dayline), syncing flashcards, installing updates from inside the app on Windows.

# Store builds: Google Play bundle and a first iPhone build
- **Google Play:** the Android workflow now also builds a signed App Bundle (`.aab`) for Play. That build has no self-updater and doesn't ask to install apps, because Play only allows updates through Play. CI checks it with Google's bundletool (target API 36, no install permission, 16 KB-aligned native code). Download it from the run's artifacts.
- **Android 16:** the app now targets API 36 and is built with Qt 6.10. Android draws apps edge to edge now, so the app keeps clear of the status bar and gesture bar, and their icons turn dark or light to suit your background colour. Back still steps back instead of closing the app.
- **iPhone:** a new `ios` workflow builds the app with Xcode 26. Music keeps playing with the screen locked, the lock screen and Control Centre show the song with play, pause, skip and seek, and a phone call pauses it. Music goes into the app's folder (Files › On My iPhone › Paraplay). Not signed yet, so it can't be installed until there's an Apple Developer account.
- **Privacy policy, terms and account deletion on the web** (`docs/`, for GitHub Pages): both stores require them. The sign-in screen and Settings › About link to them, and an account can now be deleted from a web page as well as in the app.
- Settings › Library: **Scan again** button.
- Store builds hide the update settings in Settings › About.
- [STORES.md](STORES.md): each store's requirements, what's done, and what's left to do in the stores' consoles.

# Drop 7: friends, public profiles and a profile preview
- **Friends** (sidebar, under Playlists): Activity, Find people, and Requests. Activity shows what the people you follow are playing right now (with the song's quality), what they played earlier, and the playlists they chose to share.
- **Public profiles:** open anyone's profile from Find people, Activity or Requests: picture, banner, bio, follower counts, shared playlists, and what they're listening to. Follow with one press; a Private profile gets a "Request" instead.
- **Public or Private:** your profile page now has a Public / Private switch. A Private profile keeps its bio, banner, playlists, counts and listening hidden from everyone except people you accept. Name, @username and picture stay visible so friends can find you. Accounts start Private.
- **Preview:** on your own profile page, Edit | Preview shows exactly what a stranger sees, built from what is saved. Nothing in it can be changed.
- **Your choices:** Appear in search, Show my playlists (only playlists you mark "Share on your profile"; the button sits beside a playlist's name), Show what I'm listening to (off by default), Show follower counts.
- **A friend's playlist:** their songs arrive as text only (title, artist, album, length). Every song you also own plays from your own copy, local or on any of your servers, at its own quality. Songs you don't have are greyed out. "Play all" plays the ones you have; "Save to my playlists" makes you a copy of them. No music file is ever uploaded or sent.
- **Block and report:** the "..." button on a profile. Blocking hides you both from each other and removes any follow; undo it under Requests. Reports go to the project owner only.
- Fixed: a dropped connection no longer signs you out when your session refreshes; only a refusal from the server does.
- **Needs a one-time database step before you update:** run `supabase/social.sql` in the Supabase SQL Editor (safe to run twice). It adds the social tables and tightens the older ones so nobody can read another person's profile, playlists or songs directly. Without it, saving your profile and syncing playlists will fail.
- Needs `cmake -B build` to be run again (new files), then `cmake --build build -j`.
- Not yet: Spotify and YouTube Music, WebDAV and plain web-folder sources, a Windows build.

# Drop 6: your own servers, updates, and the carousel back at full size
- Carousels are back to the full-width Cover Flow spread (the Drop 3 size). The playlist "+" tile is gone: press "New playlist" beside the Playlists heading instead.
- Searching Albums, Artists or Playlists is lighter: when you choose a result, only that cover stays loaded (with two plain frosted layers behind it) and the rest of the carousel is unloaded. Closing the search fans the carousel back out at that cover.
- Stream from your own server: Navidrome / Subsonic and Jellyfin. Sidebar → SOURCES → "Add a server…", or Settings → Servers. Original files, no transcoding. A cloud marks streamed songs and covers.
- Sources: "All sources", "This computer" and each server filter Songs, Albums and Artists (sidebar, or the chip beside the heading once you have a server).
- Servers are saved with a one-way sign-in token (Navidrome/Subsonic) or access token (Jellyfin), never your password, in a private file. Self-signed certificates are optional and pinned the first time you accept one.
- Song lists are cached per server, refreshed on start and every 4 hours, or press "Sync now".
- Updates: Settings → Updates. Checks GitHub for a newer release (switchable), shows "Update available" in the sidebar, and "Update now" builds and installs it for you, then "Restart".
- update.sh changed: `./update.sh --here` builds and installs this folder into ~/.local; `./update.sh [--tag vX.Y.Z]` fetches a release from GitHub. It no longer unpacks a tarball from ~/Downloads.
- Public repository with the MIT licence, a build check on every push, and releases published from tags.
- Needs `cmake -B build` to be run again (new files), then `cmake --build build -j`.
- Not yet: WebDAV and plain web-folder sources; Spotify; social features. Song lyrics (.lrc) are local-file only for now.
- Note: the sign-in details live in a private file (permissions 0600) rather than the system keyring, so the player stays free of extra dependencies. A keyring option can follow.

# Drop 5: profile picture and banner
- Profile page: click the picture or the banner (or the camera buttons) to change them. Drop an image file onto the window, or paste its path and press Enter, then Upload. Remove puts the blank picture back.
- Profile pictures: PNG, JPEG or WebP. The middle square is used and shrunk to 512 px, so uploads stay small and quick.
- Banners: PNG, JPEG, WebP or GIF. GIF banners are uploaded exactly as they are (up to 3 MB and 1200 x 600). Still banners are shrunk for you.
- Your picture also shows at the top of the sidebar.
- Pictures are downloaded once and kept in the cache, so nothing is fetched again each time. A GIF only loads while the profile page is showing and only plays while the window is in front.
- Delete account now removes your stored pictures first.
- Needs a one-time database step: run supabase/images.sql in the Supabase SQL Editor (safe to run twice).

# Drop 4: profiles
- Fixed: the playlist "+" tile no longer clips off screen; carousels keep their covers inside the view, and a few items stay close together.
- Fixed: choosing a search result now moves that cover to the FRONT OF THE STACK and keeps the stack open. Click the front cover to open it, or press Esc / the magnifier to fan the carousel back out.
- Profiles: click "Guest" at the top of the sidebar. Create an account or sign in with a username and password (no email). The profile page has display name, bio, who can see you (Private by default), appear in search, show playlists, sync, save, sync now, sign out, download my data, delete account.
- Playlists sync to your database automatically a few seconds after a change (newest edit wins). Each account keeps its own playlists on this computer; guest playlists are copied into your account the first time you sign in.
- Not yet: following/finding other people, public profile pages, report and block screens, profile pictures, syncing settings and artist photos.
