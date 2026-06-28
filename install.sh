#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════
#  Breezy Dotfiles — One-Click Installer
#  Sets up a fresh EndeavorOS / Arch machine from scratch.
# ══════════════════════════════════════════════════════════════
#
#  Usage:
#    git clone https://github.com/isbunni/breezy-dotfiles.git ~/breezy-dotfiles
#    cd ~/breezy-dotfiles
#    ./install.sh
#
#  Safe to run repeatedly — it backs up existing files first.
# ══════════════════════════════════════════════════════════════

set -euo pipefail

# ── Colors ────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKUP_DIR="$HOME/.dotfiles-backup/$(date +%Y%m%d_%H%M%S)"

# ── Helpers ──────────────────────────────────────────────────

info()    { echo -e "${BLUE}ℹ${NC}  $*"; }
success() { echo -e "${GREEN}✓${NC}  $*"; }
warn()    { echo -e "${YELLOW}⚠${NC}  $*"; }
error()   { echo -e "${RED}✗${NC}  $*"; }
header()  { echo -e "\n${BOLD}${CYAN}━━━ $* ━━━${NC}\n"; }

# Back up a file/dir before overwriting it
backup() {
  local src="$1"
  [[ -e "$src" || -L "$src" ]] || return 0
  local rel="${src#$HOME/}"
  local dest="$BACKUP_DIR/$rel"
  mkdir -p "$(dirname "$dest")"
  cp -a "$src" "$dest"
  info "Backed up: ~/$rel → $BACKUP_DIR/$rel"
}

# Create a symlink, backing up any existing target
link() {
  local src="$1" dest="$2"
  backup "$dest"
  ln -sf "$src" "$dest"
  success "Linked: $dest → $src"
}

# ── Pre-flight ───────────────────────────────────────────────

header "Breezy Dotfiles Installer"

echo "This will:"
echo "  1. Install required packages (pacman)"
echo "  2. Back up existing dotfiles to $BACKUP_DIR"
echo "  3. Symlink all configs into place"
echo "  4. Set up custom scripts in ~/.local/bin/"
echo "  5. Initialize nvim plugins"
echo ""
read -rp "Continue? [Y/n] " confirm
[[ "$confirm" =~ ^[Nn] ]] && { echo "Aborted."; exit 0; }

# ── Step 1: Install packages ─────────────────────────────────

header "Installing Packages"

if command -v pacman &>/dev/null; then
  info "Detected pacman — installing from packages/arch.txt"
  # Filter out comments and blank lines, pass to pacman
  mapfile -t pkgs < <(grep -v '^#\|^$' "$REPO_DIR/packages/arch.txt")
  if sudo pacman -S --needed --noconfirm "${pkgs[@]}" 2>/dev/null; then
    success "All pacman packages installed"
  else
    warn "Some packages failed — check output above and re-run"
  fi
else
  error "pacman not found — this installer is for Arch-based distros"
  exit 1
fi

# ── Step 2: Oh My Zsh ────────────────────────────────────────

header "Setting Up Zsh"

if [[ ! -d "$HOME/.oh-my-zsh" ]]; then
  info "Installing Oh My Zsh..."
  sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" "" --unattended 2>/dev/null || true
  success "Oh My Zsh installed"
else
  info "Oh My Zsh already installed — skipping"
fi

# Set zsh as default shell
if [[ "$SHELL" != */zsh ]]; then
  info "Changing default shell to zsh..."
  chsh -s "$(which zsh)"
  success "Default shell changed to zsh (log out and back in)"
fi

# ── Step 3: Symlink dotfiles ─────────────────────────────────

header "Linking Dotfiles"

mkdir -p "$BACKUP_DIR"

# Home directory files
link "$REPO_DIR/home/.bashrc"  "$HOME/.bashrc"
link "$REPO_DIR/home/.zshrc"   "$HOME/.zshrc"

# GTK / X resources
link "$REPO_DIR/config/gtk/Xresources"     "$HOME/.Xresources"
link "$REPO_DIR/config/gtk/gtkrc-2.0"      "$HOME/.gtkrc-2.0"

# ~/.config/ entries
for dir in "$REPO_DIR/config"/*/; do
  name="$(basename "$dir")"
  # Skip nvim — handled separately below
  [[ "$name" == "nvim" || "$name" == "scripts" || "$name" == "autostart" ]] && continue
  mkdir -p "$HOME/.config/$name"
  # Link individual files/folders inside, not the parent dir
  for item in "$dir"/*; do
    target="$HOME/.config/$name/$(basename "$item")"
    link "$item" "$target"
  done
done

# ── Step 4: Custom scripts ───────────────────────────────────

header "Installing Custom Scripts"

mkdir -p "$HOME/.local/bin"
for script in "$REPO_DIR/config/scripts/"*; do
  [[ -f "$script" ]] || continue
  chmod +x "$script"
  link "$script" "$HOME/.local/bin/$(basename "$script")"
done

# Make sure ~/.local/bin is in PATH
if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
  export PATH="$HOME/.local/bin:$PATH"
  info "Added ~/.local/bin to PATH — add this to your .zshrc if not already there"
fi

# ── Step 5: Autostart ────────────────────────────────────────

header "Setting Up Autostart"

mkdir -p "$HOME/.config/autostart"
for desktop in "$REPO_DIR/config/autostart/"*.desktop; do
  [[ -f "$desktop" ]] || continue
  link "$desktop" "$HOME/.config/autostart/$(basename "$desktop")"
done

# ── Step 6: Neovim ───────────────────────────────────────────

header "Setting Up Neovim"

# Link the nvim config directory
backup "$HOME/.config/nvim"
rm -rf "$HOME/.config/nvim"
link "$REPO_DIR/config/nvim/nvim" "$HOME/.config/nvim"

# Bootstrap lazy.nvim plugins on first launch
info "Installing nvim plugins (this may take a moment)..."
nvim --headless "+Lazy! sync" +qa 2>/dev/null || warn "Nvim plugin install incomplete — open nvim and run :Lazy"

# ── Step 7: User directories ─────────────────────────────────

header "Setting Up User Directories"

xdg-user-dirs-update 2>/dev/null || true
if [[ -f "$REPO_DIR/config/gtk/../autostart" ]]; then true; fi
# Source user-dirs if present
[[ -f "$HOME/.config/user-dirs.dirs" ]] && source "$HOME/.config/user-dirs.dirs"

for dir in DESKTOP DOCUMENTS DOWNLOAD TEMPLATES PUBLICSHARE MUSIC PICTURES VIDEOS PROJECTS; do
  var="XDG_${dir}_DIR"
  val=$(printenv "$var" 2>/dev/null || echo "")
  [[ -n "$val" ]] && mkdir -p "$val"
done

# ── Step 8: Theme ────────────────────────────────────────────

header "Applying Theme"

# Apply the default theme state
if [[ -f "$HOME/.config/.theme-state" ]]; then
  theme_name=$(cat "$HOME/.config/.theme-state")
  [[ -x "$HOME/.local/bin/theme" ]] && "$HOME/.local/bin/theme" "${theme_name:-gruvbox}" 2>/dev/null || true
fi

# Load X resources
[[ -f "$HOME/.Xresources" ]] && xrdb -merge "$HOME/.Xresources" 2>/dev/null || true

# ── Done ─────────────────────────────────────────────────────

header "Installation Complete"

success "All dotfiles are in place!"
echo ""
echo -e "  ${BOLD}Backup:${NC}    $BACKUP_DIR"
echo -e "  ${BOLD}Neovim:${NC}    Run :Mason in nvim to install LSP servers"
echo -e "  ${BOLD}Next:${NC}      Log out and log back in to start i3"
echo ""
echo -e "  ${CYAN}Run again anytime to update symlinks.${NC}"
