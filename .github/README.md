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

The new features are in development; see [releases](https://github.com/Eyad-Alqaysi/hopflow/releases)
for what has shipped. They need Hopflow on both computers. A Hopflow computer can still connect to
a stock Deskflow, Input Leap, Barrier or Synergy 1 computer, with the new features turned off.

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
