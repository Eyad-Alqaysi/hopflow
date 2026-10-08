<p align="center">
  <img src="../src/apps/res/icons/deskflow-light/apps/64/io.github.eyad_alqaysi.hopflow.svg" width="96" alt="Hopflow icon">
</p>

<h1 align="center">Hopflow</h1>

<p align="center">
  One keyboard and mouse for all your computers, with file transfer and Mac-friendly shortcuts.<br>
  <strong>Built on top of <a href="https://github.com/deskflow/deskflow">Deskflow</a>.</strong>
</p>

---

Hopflow is a free, open-source software KVM. Move your mouse past the edge of one screen and it
continues on the next computer on your network. Your keyboard and clipboard follow it.

Hopflow is a fork of [Deskflow](https://github.com/deskflow/deskflow), the open-source keyboard and
mouse sharing app (the open-source core of Synergy). Everything Deskflow does, Hopflow does too.
Hopflow adds a few features Deskflow has chosen not to include.

## What Hopflow adds

| Feature | Deskflow | Hopflow |
|---|:-:|:-:|
| Shared mouse and keyboard (Windows, macOS, Linux) | ✅ | ✅ |
| Clipboard sync (text, HTML, images) | ✅ | ✅ |
| TLS encryption | ✅ | ✅ |
| **Automatic Ctrl ↔ Cmd swap between Mac and Windows** | Manual per screen | ✅ |
| **Copy files on one computer, paste on another** | ❌ | ✅ |
| **Drag files across the screen edge** | ❌ | ✅ |
| **Show a screen (with sound) in a window on the other computer** | ❌ | ✅ |

The new features need Hopflow on both computers. A Hopflow computer still connects to a stock
Deskflow, Input Leap, Barrier or Synergy 1 computer; the new features are simply off for that one.

### Ctrl ↔ Cmd

When the cursor moves between a Mac and a Windows or Linux computer, Hopflow swaps Ctrl and Cmd for
you, so Ctrl+C on Windows is Cmd+C on the Mac and the other way round. Turn it off under
**Server settings → Swap Ctrl and Cmd automatically**. A computer whose Ctrl or Super mapping you
changed yourself keeps your mapping.

### Copy and paste files

Copy files or folders in Finder or Explorer, move the cursor to another computer, and paste. Hopflow
sends the files when the cursor arrives. They are staged in `Downloads/Hopflow/.clipboard` and put on
that computer's clipboard, so a normal paste copies them where you want them.

### Drag files across the edge

Drag files from Finder or Explorer past the edge of the screen. The drag carries on to the next
computer, and when you let go the files are saved to `Downloads/Hopflow` and shown in
Finder or Explorer. On Windows the Hopflow window must be running (it can be minimized to the tray);
it is what detects the drag.

### Share your screen into a window

Show one computer's screen, with its sound, in a window on the other. For example, to share your
Mac's screen on Discord from your Windows PC:

1. On the Mac, choose **Share Screen…** from the Hopflow menu or tray icon.
2. Pick the PC, a resolution (720p, 1080p or native) and a frame rate (30, 60 or the display's own),
   then click **Share**.
3. A window called **Hopflow – <your Mac> screen** opens on the PC. In Discord, share that window.
   Double-click it for full screen.

It works the other way round too. Sharing uses its own encrypted connection on TCP port 24802, so
it never slows down the mouse and keyboard. Computers that are already paired trust each other;
anyone else has to be allowed first, after you compare fingerprints. Video is H.264 and sound AAC,
both encoded by the computer's own hardware or system codecs, and the stream lowers its quality by
itself when the network can't keep up.

- **macOS:** Hopflow needs **Screen Recording** permission (Privacy & Security). Sharing the screen
  needs macOS 12.3 or later; sharing sound needs macOS 13 or later.
- **Windows:** the lock screen and UAC prompts show as black, as Windows hides them from capture.
- When sharing from the server, pick the client by name, or type its IP address.

### Limits and safety

- Transfers go over the same connection as everything else, encrypted with TLS when TLS is on.
- The server decides what is sent where. Received files are checked: no absolute paths, `..`,
  drive letters or reserved Windows names, and they only appear once every byte has arrived.
- Transfers above the size limit (2 GB by default) are refused, and so is anything that does not
  fit on the disk. Change the limit or turn file transfer off under **Server settings**.
- Copying and dragging files works on macOS and Windows. Linux can receive dragged files, but
  cannot paste copied files or start a transfer yet.

## Install

Download the latest build from [Releases](https://github.com/Eyad-Alqaysi/hopflow/releases):

- **macOS:** `hopflow-*-macos-arm64.dmg` (Apple Silicon) or `-x86_64.dmg` (Intel).
- **Windows:** `hopflow-*-win-x64.msi` or `-win-arm64.msi`.
- **Linux:** `hopflow-*.deb` (Ubuntu), or build from source.

Builds are not code-signed yet:

- **macOS:** after copying `Hopflow.app` to Applications, run `xattr -c /Applications/Hopflow.app`,
  or right-click the app and choose **Open**. Then allow **Accessibility** (Privacy & Security) for
  both `Hopflow` and the `deskflow-core` process, and allow **Local Network** if asked.
- **Windows:** if SmartScreen warns you, click **More info → Run anyway**.

## Build from source

Same as Deskflow: CMake 3.24+, a C++20 compiler, Qt 6.7+ and OpenSSL 3.

```sh
# macOS
brew install cmake ninja qt openssl@3
cmake -S. -Bbuild -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix openssl@3)"
cmake --build build
```

For Windows and Linux, follow Deskflow's [build guide](https://github.com/deskflow/deskflow/wiki/Building).
The steps are identical.

## Credits

Hopflow exists because of Deskflow and everyone who built it and its predecessors: Synergy, Barrier
and Input Leap. See [NOTICE](../NOTICE) for copyright holders and
[Deskflow's contributors](https://github.com/deskflow/deskflow/graphs/contributors).

Hopflow is an independent fork. It is not affiliated with or endorsed by the Deskflow project.
Please report Hopflow bugs [here](https://github.com/Eyad-Alqaysi/hopflow/issues), not to Deskflow.

## License

[GPL-2.0-only with the OpenSSL exception](../LICENSE), the same as Deskflow.
