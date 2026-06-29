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

# ── Step 4b: Scan Master C++ ────────────────────────────────

header "Building Scan Master C++"

SCAN_REPO="https://github.com/isbunni/scan-master-cpp.git"
SCAN_SRC_DIR="$HOME/breezy-dotfiles/scan-master-cpp"
SCAN_INSTALL_DIR="$HOME/.config/scripts/scan-master-cpp"

# Clone or update the standalone repo
if [[ -d "$SCAN_SRC_DIR/.git" ]]; then
  info "Updating scan-master-cpp..."
  cd "$SCAN_SRC_DIR" && git pull && cd "$REPO_DIR"
else
  info "Cloning scan-master-cpp..."
  git clone "$SCAN_REPO" "$SCAN_SRC_DIR" 2>/dev/null || {
    warn "Could not clone scan-master-cpp — check your internet connection"
    warn "  git clone $SCAN_REPO $SCAN_SRC_DIR"
  }
fi

if [[ -d "$SCAN_SRC_DIR" ]]; then
  info "Building scan-master-cpp..."
  mkdir -p "$SCAN_SRC_DIR/build"
  cd "$SCAN_SRC_DIR/build"
  cmake .. -DCMAKE_BUILD_TYPE=Release 2>/dev/null
  make -j$(nproc) 2>/dev/null
  if [[ $? -eq 0 ]]; then
    mkdir -p "$SCAN_INSTALL_DIR"
    cp -f scan-master scan-invoice scan-learn "$SCAN_INSTALL_DIR/"
    cp -rf "$SCAN_SRC_DIR/vendors" "$SCAN_INSTALL_DIR/"
    chmod +x "$SCAN_INSTALL_DIR"/scan-master "$SCAN_INSTALL_DIR"/scan-invoice "$SCAN_INSTALL_DIR"/scan-learn
    success "Scan Master C++ installed to $SCAN_INSTALL_DIR"
  else
    warn "Scan Master C++ build failed — check dependencies"
  fi
  cd "$REPO_DIR"
fi

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

# ── Step 5b: Firefox ───────────────────────────────────────

header "Setting Up Firefox"

# Find the default-release profile (skip the lock profile)
for profile_dir in "$HOME"/.config/mozilla/firefox/*/; do
  case "$(basename "$profile_dir")" in
    *.default-release)
      mkdir -p "$profile_dir"
      backup "$profile_dir/user.js"
      link "$REPO_DIR/config/firefox/user.js" "$profile_dir/user.js"
      ;;
  esac
done

# ── Step 6: OneDrive ────────────────────────────────────────

header "Setting Up OneDrive"

if ! command -v onedriver &>/dev/null; then
  info "Installing onedriver (AUR)..."
  if command -v yay &>/dev/null; then
    yay -S --noconfirm onedriver 2>/dev/null || warn "onedriver install failed — install manually with: yay -S onedriver"
  else
    warn "yay not found — install onedriver manually: yay -S onedriver"
  fi
else
  info "onedriver already installed — skipping"
fi

# Create OneDrive mount point
mkdir -p "$HOME/OneDrive"

# Add onedriver to i3 autostart if not already present
if [[ -f "$HOME/.config/i3/config" ]] && ! grep -q "onedriver" "$HOME/.config/i3/config"; then
  echo "exec --no-startup-id onedriver ~/OneDrive" >> "$HOME/.config/i3/config"
  success "Added onedriver to i3 autostart"
fi

info "OneDrive: After login, run: onedriver ~/OneDrive"
info "Then sign in with your Microsoft account in the browser prompt."

# ── Step 7: OpenClaw ────────────────────────────────────────

header "Setting Up OpenClaw"

if ! command -v openclaw &>/dev/null; then
  info "Installing OpenClaw via npm..."
  npm install -g openclaw 2>/dev/null || warn "OpenClaw install failed — install manually with: npm install -g openclaw"
  success "OpenClaw installed"
else
  info "OpenClaw already installed — skipping"
fi

if [[ -f "$HOME/.openclaw/completions/openclaw.zsh" ]]; then
  info "OpenClaw zsh completions available"
fi

info "OpenClaw: Run 'openclaw setup' to configure your instance"

# ── Step 8: Neovim ───────────────────────────────────────────

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
