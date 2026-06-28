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

| Keys | Action |
|---|---|
| `Mod+Enter` | Open terminal (kitty) |
| `Mod+d` | Open app launcher (rofi) |
| `Mod+Shift+e` | Exit i3 |
| `Mod+Shift+r` | Restart i3 |
| `Mod+1-0` | Switch workspace |
| `Mod+Shift+1-0` | Move window to workspace |
| `Mod+h/j/k/l` | Focus left/down/up/right |
| `Mod+Shift+h/j/k/l` | Move window |
| `Mod+f` | Fullscreen toggle |
| `Mod+Shift+q` | Kill window |

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
