# Streamline on iPhone, iPad and Mac

Every change to the app builds both Apple versions on GitHub's Macs (`.github/workflows/apple.yml`) and puts them on
the **apple-latest** release:

| File | For | Signed by |
|---|---|---|
| `Streamline-ios.ipa` | iPhone and iPad, iOS 17 or newer | nobody: you sign it for your own devices with your Apple ID (below) |
| `Streamline-mac.dmg` | Macs with Apple silicon (M1 or newer), macOS 13 or newer | ad hoc (not by Apple), so macOS asks once before opening it |

Getting onto the App Store needs Apple's Developer Program (US$99 a year; Apple only waives it for nonprofits, schools
and governments, not individuals). Until then, these are free.

## iPhone and iPad (free Apple ID)

Apple lets anyone install apps they sign with a free Apple ID on their own devices, with limits: **the app has to be
re-signed every 7 days**, at most 3 such apps per device, and 3 devices. A sideloading tool does the signing:

- **SideStore** (sidestore.io): set up once with a computer, then it refreshes Streamline from the phone itself.
- **AltStore** (altstore.io): AltServer on a Windows PC or Mac refreshes it over Wi-Fi while both are on.
- **Sideloadly** (sideloadly.io): Windows or Mac; install and refresh by plugging the phone in.

With Sideloadly, for example:

1. Download `Streamline-ios.ipa` from the apple-latest release.
2. On Windows, install iTunes and iCloud from Apple's website (not the Microsoft Store versions). Plug the iPhone in and
   tap Trust on it.
3. Open Sideloadly, drop the .ipa on it, type your Apple ID and press Start. (A spare Apple ID is fine.)
4. On the iPhone: **Settings › General › VPN & Device Management**, tap your Apple ID and Trust. On iOS 16 or newer also
   turn on **Settings › Privacy & Security › Developer Mode** and restart when asked.
5. Every 7 days, refresh it the same way (or let SideStore/AltStore do it). A newer .ipa installed over the top keeps
   your pods, flashcards and settings.

## Mac

1. Download `Streamline-mac.dmg`, open it and drag Streamline into Applications.
2. The first time: open Streamline, then go to **System Settings › Privacy & Security**, scroll down and press
   **Open Anyway** (on macOS 14 and older, right-click the app and choose Open). After that it opens normally.
   If macOS says the app is damaged, run `xattr -dr com.apple.quarantine /Applications/Streamline.app` in Terminal.
3. New builds: download the new .dmg and replace the app. (There's no in-app updater on Apple's devices.)

Intel Macs aren't built for yet.

## What's different on Apple's devices

- **Music on an iPhone or iPad** goes in the app's own folder: Files › On My iPhone › Streamline (or drag it there in
  Finder with the phone plugged in), then Settings › Library › Scan again. Music servers work as everywhere else.
- **Pods on an iPhone or iPad:** New pod › **From Files…** picks a folder anywhere in Files (iCloud Drive, OneDrive,
  On My iPhone). Streamline keeps a copy in its own folder, because Apple only lets an app read its own; pick the folder
  again to bring the copy up to date. "Move the files into topic folders" moves files in the copy.
- **Pods on a Mac** read any folder; macOS asks once before Streamline reads Documents, Desktop or Downloads.
- **The AI** runs on the processor, as on other devices. The writer model needs about 1 GB of memory.
- **Not tried on a real iPhone yet.** The builds are checked on GitHub's Macs: the Mac app is opened there and pictures
  are taken of its window and of the phone layout at an iPhone's and an iPad's size. The iPhone app itself can't run in
  Apple's simulator, because Qt's ready-made iOS libraries are for real devices only. So expect a round of fixes after
  the first install: the Files picker, playing in the background, the lock-screen controls.

## The App Store, if you join the Developer Program later

The iPhone app is already built the App Store's way: Xcode 26 and the iOS 26 SDK, iOS 17 minimum, a privacy manifest,
the app icon, background audio, no self-updater, no downloaded code (the AI models are data), standard encryption only.

1. Join the Apple Developer Program as an individual.
2. In **Certificates, Identifiers & Profiles**: register the bundle id `io.github.whozdgt.streamline`, make an
   **Apple Distribution** certificate (export it with its key as a .p12 from Keychain Access) and an **App Store**
   provisioning profile for the bundle id.
3. In **App Store Connect**: create the app (bundle id above), and under Users and Access › Integrations make an
   **App Store Connect API key** with the App Manager role.
4. Add these repository secrets (Settings › Secrets and variables › Actions):

   | Secret | What |
   |---|---|
   | `IOS_DIST_CERT_P12_BASE64` | the .p12, base64 (`base64 -i cert.p12`) |
   | `IOS_DIST_CERT_PASSWORD` | the .p12's password |
   | `IOS_PROFILE_BASE64` | the .mobileprovision, base64 |
   | `ASC_KEY_P8_BASE64` | the API key's .p8 file, base64 |
   | `ASC_KEY_ID` | the key's id |
   | `ASC_ISSUER_ID` | the issuer id shown above the keys |

   The next push builds, signs and uploads the app (the **appstore** job); it appears in TestFlight after Apple's
   processing.
5. Before submitting for review: turn on **GitHub Pages** for this repository (Settings › Pages › Deploy from a branch ›
   `main`, `/docs`), so the privacy policy, terms and account-deletion pages the app links to are online at
   `https://whozdgt.github.io/streamline/`; the App Privacy answers (account id, profile
   pictures, playlists and synced to-dos and flashcards, all for app functionality, none for tracking; documents and
   the AI stay on the device), screenshots for 6.9" iPhone and 13" iPad, a review account for the social features, and
   age rating 13+.
6. **Mac App Store** is a separate submission and needs more work: the Mac app has to run in Apple's sandbox, which means
   asking for folders through the system's picker instead of browsing the disk. Not done yet.

**Licensing:** Qt is linked into the iPhone app statically. Streamline stays open source with its whole build public, so
anyone can rebuild it with their own Qt, which is how many Qt apps meet Qt's LGPL on the App Store; the alternative is a
commercial Qt licence.
