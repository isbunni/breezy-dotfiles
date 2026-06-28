<div align="center">

# 🌊 Breezy Dotfiles

*One command. Full desktop.*

A complete EndeavorOS / Arch + i3 desktop environment — reproducible from scratch.

</div>

## What's Inside

| Component | Choice |
|---|---|
| **Window Manager** | i3-gaps |
| **Terminal** | Kitty |
| **Shell** | Zsh + Oh My Zsh |
| **Prompt** | Starship |
| **Editor** | Neovim (full Lua config) |
| **Bar** | Polybar |
| **Launcher** | Rofi |
| **Notifications** | Dunst |
| **Compositor** | Picom |
| **Theme Switcher** | Custom script (syncs nvim + kitty + starship) |
| **File Scanner** | Brother ADF invoice scanner with OCR |

## One-Line Install

```bash
git clone https://github.com/isbunni/breezy-dotfiles.git ~/breezy-dotfiles && cd ~/breezy-dotfiles && ./install.sh
```

That's it. The installer handles everything:
1. Installs all pacman packages
2. Installs Oh My Zsh
3. Backs up existing dotfiles
4. Symlinks everything into place
5. Installs custom scripts to `~/.local/bin/`
6. Bootstraps Neovim plugins
7. Sets up user directories
8. Applies your theme

## Manual Install

If you want to pick and choose:

```bash
# Just packages
sudo pacman -S --needed - < packages/arch.txt

# Just symlinks (run from repo root)
./install.sh
# Or do it manually:
ln -sf "$(pwd)/home/.zshrc" ~/.zshrc
ln -sf "$(pwd)/config/i3/config" ~/.config/i3/config
# ... etc
```

## Post-Install

After installing, do these once inside Neovim:

```
:Mason          → Install LSP servers (lua_ls, clangd)
:Lazy           → Verify all plugins are installed
```

Then **log out and log back in** to start i3.

## Structure

```
breezy-dotfiles/
├── install.sh              ← One-click installer (run this)
├── packages/
│   └── arch.txt            ← All pacman packages
├── home/
│   ├── .zshrc              ← Shell config, aliases, PATH
│   └── .bashrc             ← Bash fallback
├── config/
│   ├── i3/                 ← Window manager config
│   ├── kitty/              ← Terminal config
│   ├── polybar/            ← Status bar
│   ├── rofi/               ← App launcher + powermenu
│   ├── dunst/              ← Notification daemon
│   ├── picom/              ← Compositor (transparency, rounded corners)
│   ├── starship/           ← Shell prompt + theme presets
│   ├── fastfetch/          ← System info display
│   ├── gtk/                ← GTK 2/3/4 + Xresources theming
│   ├── xsettingsd/         ← GTK live settings
│   ├── nwg-look/           ← GTK theme exporter config
│   ├── nvim/               ← Full Neovim config (separate repo)
│   ├── scripts/            ← Custom scripts (theme switcher, invoice scanner)
│   └── autostart/          ← i3 autostart entries
└── README.md
```

## Keybindings (i3)

> `Mod` = `Alt` (Mod1). `Mod4` = Super/Windows key.

### Essentials
| Keys | Action |
|---|---|
| `Mod+Enter` | Open terminal (kitty) |
| `Mod+Space` | Open app launcher (rofi drun) |
| `Mod+d` | Open dmenu launcher |
| `Mod+q` | Kill focused window |
| `Mod+Shift+e` | Exit i3 (with confirmation) |
| `Mod+Shift+r` | Restart i3 (preserves layout) |
| `Mod+Shift+c` | Reload i3 config |

### Navigation
| Keys | Action |
|---|---|
| `Mod+h/j/k/l` | Focus left / down / up / right |
| `Mod+Shift+h/j/k/l` | Move window left / down / up / right |
| `Mod+1`–`Mod+0` | Switch to workspace 1–10 |
| `Mod+Shift+1`–`Mod+0` | Move window to workspace 1–10 |
| `Mod+a` | Focus parent container |
| `Mod+Shift+f` | Focus mode toggle (tiling/floating) |

### Layout
| Keys | Action |
|---|---|
| `Mod+f` | Fullscreen toggle |
| `Mod+s` | Stacking layout |
| `Mod+w` | Tabbed layout |
| `Mod+e` | Toggle split layout |
| `Mod+v` | Split vertical |
| `Mod+Shift+b` | Split horizontal |
| `Mod+Shift+Space` | Toggle floating |
| `Mod+r` | Resize mode (h/j/k/l or arrows, Enter/Esc to exit) |

### Media & Hardware
| Keys | Action |
|---|---|
| `XF86AudioRaiseVolume` | Volume up (+10%) |
| `XF86AudioLowerVolume` | Volume down (-10%) |
| `XF86AudioMute` | Mute toggle |
| `XF86AudioMicMute` | Mic mute toggle |
| `XF86MonBrightnessUp` | Brightness up (+10%) |
| `XF86MonBrightnessDown` | Brightness down (-10%) |
| `Mod4+v` | Toggle clipboard manager (copyq) |

### McFarlands Shop
| Keys | Action |
|---|---|
| `Mod+;` | Open Telegram on workspace 5 |
| `Mod+Space` | Quick-launch any app via rofi |

### Startup Applications
| Workspace | Application |
|---|---|
| 1 | Firefox |
| 2 | Thunderbird |
| 3 | LibreOffice Calc (shop spreadsheets) |
| 4 | Thunar (SCANS folder) + Calculator |
| 5 | Telegram Desktop |
| 10 | Sophie startup brief (kitty) |

**Always running:** Polybar, Picom, CopyQ, OneDrive (onedriver), PulseAudio

## Keybindings (Neovim)

See the [Neovim config README](config/nvim/nvim/README.md) for the full list.

## Theme Switcher

Four themes, synced across Neovim + Kitty + Starship:

```bash
theme gruvbox      # Warm retro
theme onedark      # Clean Atom-style
theme tokyonight   # Deep blue
theme catppuccin   # Pastel mocha
theme cycle        # Cycle to next
```

Or in Neovim: `<leader>tt` to cycle, `<leader>tg/to/tn/tc` to jump directly.

## Dependencies

| Package | Required For |
|---|---|
| `i3-wm` | Window manager |
| `kitty` | Terminal emulator |
| `zsh` + `oh-my-zsh-git` | Shell |
| `starship` | Shell prompt |
| `neovim` | Editor |
| `polybar` | Status bar |
| `rofi` | App launcher |
| `picom` | Compositor |
| `dunst` | Notifications |
| `ttf-jetbrains-mono-nerd` | Font (icons + text) |
| `networkmanager` | WiFi |
| `pavucontrol` | Audio |
| `brightnessctl` | Screen brightness |
| `copyq` | Clipboard manager |
| `redshift` | Blue light filter |
| `gcc`, `gdb`, `clang` | C/C++ dev + Treesitter |
| `nodejs`, `npm` | LSP servers, formatters |
| `ripgrep`, `fd`, `bat` | Telescope search |

See `packages/arch.txt` for the complete list.

## Updating

```bash
cd ~/breezy-dotfiles
git pull
./install.sh          # Re-run to pick up any new symlinks
```

## Notes

- Existing dotfiles are backed up to `~/.dotfiles-backup/<timestamp>/` before anything is overwritten
- The Neovim config is a submodule pointing to [isbunni/neovim-config](https://github.com/isbunni/neovim-config)
- Custom scripts in `config/scripts/` are symlinked to `~/.local/bin/` — make sure that's in your PATH

---

<div align="center">

**Made by [isbunni](https://github.com/isbunni)** 🌊

</div>
