# UCraft
A lightweight **Minecraft server implementation written in C**, designed for machines with limited resources.

<p align="center">
  <img src="screenshot.png" alt="UCraft terrain generation" width="640" />
</p>
<p align="center"><em>Procedurally generated terrain</em></p>

UCraft implements just the bare essentials of the Minecraft protocol: clients can join,
explore a procedurally generated world, chat, and break and place blocks. It provides the
foundation for **very primitive Minecraft gameplay** — not a full server replacement.

Currently supports **Minecraft 26.1.2** clients.

## Features

- **Procedural terrain generation** — an explorable world generated on demand as players move around
- **Primitive Minecraft gameplay** — walk around, chat with other players, and break/place blocks
- **Online-mode encryption** — authenticated logins through mbedTLS, or offline mode without it
- **Tiny footprint** — ~46K-byte binary without authentication (~70K with), and ~50K bytes of memory on average (for a single player)

## Building

The server was built and tested on Linux. On Windows, MSVC is required and on macOS, Xcode Command Line Tools and CMake are required.

```bash
sudo apt install git build-essential cmake make
git clone https://github.com/vimpop/UCraft/
cd UCraft && mkdir build && cd build
cmake ..
make
```
This produces an executable named `UCraft` in `build/src`.

Pre-built binaries are also available as artifacts of the [GitHub Actions](https://github.com/vimpop/UCraft/actions) workflow runs.

## Running

```bash
./src/UCraft
[INFO]: Listening on *:25565
[INFO]: UCraft server started!
```

Then connect to `localhost:25565` with a Minecraft **26.1.2** client.

## Implementations

- **Lightbulb** (BL602 MCU) — implementation details [here](https://github.com/vimpop/UCraft-bl602)
- **Samsung C410W Printer** (ARM BE MCU) - implementation details [here](https://github.com/vimpop/UCraft-printer)
- **Italian ISP Router** (Broadcom BCM63138) - implementation details [here](https://github.com/ZioCook/UCraft/tree/be2b5235e73acf73aa5cbaa9e456819cb0f58850) by [ZioCook](https://github.com/ZioCook)

*Your implementation could also be here! (feel free to open up a issue with your implementation)*

## Credits

- [Bixilon](https://bixilon.de/en) for help with major parts of the Minecraft protocol
- [wiki.vg](https://wiki.vg/Main_Page) for documenting Minecraft's protocol

## License

[MIT](LICENSE.txt)
