#!/bin/bash
# ============================================================
#  Scan Learn — Teach Scan Master from manual corrections
#
#  When you manually rename an UNKNOWN_*.pdf or a wrongly-named
#  file, run this to extract the clues and update the vendor DB.
#
#  Usage:
#    scan-learn.sh <pdf_file> <VendorName>
#    scan-learn.sh UNKNOWN_20260625_104456.pdf TEC
#
#  This will OCR the file, extract phone numbers, domains,
#  and addresses, then save them as known clues for that vendor.
# ============================================================

set -uo pipefail

VENDOR_DB="$HOME/.config/scripts/scan-vendors.json"
SCANS_DIR="$HOME/OneDrive/Desktop/SCANS 2026"

if [ $# -lt 2 ]; then
  echo "Usage: scan-learn.sh <pdf_file> <VendorName>"
  echo ""
  echo "Examples:"
  echo "  scan-learn.sh UNKNOWN_20260625_104456.pdf TEC"
  echo "  scan-learn.sh SomeFile.pdf NewVendor"
  exit 1
fi

PDF="$1"
VENDOR="$2"

# Resolve path
if [ ! -f "$PDF" ] && [ -f "$SCANS_DIR/$PDF" ]; then
  PDF="$SCANS_DIR/$PDF"
fi

if [ ! -f "$PDF" ]; then
  echo "ERROR: File not found: $PDF"
  exit 1
fi

if [ ! -f "$VENDOR_DB" ]; then
  echo "ERROR: Vendor DB not found: $VENDOR_DB"
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "ERROR: jq is required. Install with: sudo apt install jq"
  exit 1
fi

echo "📖 Learning from: $(basename "$PDF")"
echo "   Vendor: $VENDOR"
echo ""

# Extract text
TEXT=$(pdftotext -layout "$PDF" - 2>/dev/null || echo "")

if [ -z "$TEXT" ]; then
  echo "⚠ No text could be extracted from this PDF."
  exit 1
fi

LEARNED=0

# Learn phone numbers
DOC_PHONES=$(echo "$TEXT" | grep -oP '\b\d{3}[-.]\d{3}[-.]\d{4}\b' | sort -u)
for phone in $DOC_PHONES; do
  EXISTING=$(jq -r ".phones[\"$phone\"] // empty" "$VENDOR_DB" 2>/dev/null)
  if [ -z "$EXISTING" ] || [ "$EXISTING" = "null" ]; then
    jq ".phones[\"$phone\"] = \"$VENDOR\"" "$VENDOR_DB" > "${VENDOR_DB}.tmp" 2>/dev/null && mv "${VENDOR_DB}.tmp" "$VENDOR_DB"
    echo "  📱 Learned phone: $phone → $VENDOR"
    LEARNED=$((LEARNED+1))
  elif [ "$EXISTING" != "$VENDOR" ]; then
    echo "  ⚠ Phone $phone already mapped to $EXISTING (not $VENDOR)"
  fi
done

# Learn domains
DOC_DOMAINS=$(echo "$TEXT" | grep -oP '[\w.-]+\.(com|net|org)' | sort -u)
for domain in $DOC_DOMAINS; do
  # Skip generic domains
  case "$domain" in
    gmail.com|yahoo.com|hotmail.com|outlook.com|autozone.com) continue ;;
  esac
  EXISTING=$(jq -r ".domains[\"$domain\"] // empty" "$VENDOR_DB" 2>/dev/null)
  if [ -z "$EXISTING" ] || [ "$EXISTING" = "null" ]; then
    jq ".domains[\"$domain\"] = \"$VENDOR\"" "$VENDOR_DB" > "${VENDOR_DB}.tmp" 2>/dev/null && mv "${VENDOR_DB}.tmp" "$VENDOR_DB"
    echo "  🌐 Learned domain: $domain → $VENDOR"
    LEARNED=$((LEARNED+1))
  elif [ "$EXISTING" != "$VENDOR" ]; then
    echo "  ⚠ Domain $domain already mapped to $EXISTING (not $VENDOR)"
  fi
done

# Learn addresses (full street address only, null-delimited to prevent splitting)
# Collapse extra whitespace for cleaner matching
DOC_ADDRESSES=$(echo "$TEXT" | grep -oiP '\d{3,4}\s+SW\s+\w+\s+\w+|\d{3,4}\s+\w+\s+(Street|St|Way|Ave|Road|Rd|Boulevard|Blvd|Drive|Dr)' | sed 's/\s\s*/ /g' | sort -u)
if [ -n "$DOC_ADDRESSES" ]; then
  while IFS= read -r addr; do
    [ -z "$addr" ] && continue
    EXISTING=$(jq -r ".addresses[\"$addr\"] // empty" "$VENDOR_DB" 2>/dev/null)
    if [ -z "$EXISTING" ] || [ "$EXISTING" = "null" ]; then
      jq ".addresses[\"$addr\"] = \"$VENDOR\"" "$VENDOR_DB" > "${VENDOR_DB}.tmp" 2>/dev/null && mv "${VENDOR_DB}.tmp" "$VENDOR_DB"
      echo "  🏠 Learned address: $addr → $VENDOR"
      LEARNED=$((LEARNED+1))
    fi
  done <<< "$DOC_ADDRESSES"
fi

# Also extract the invoice number pattern from the filename
# and learn it for validation
CURRENT_NAME=$(basename "$PDF" .pdf)
INV_PART=$(echo "$CURRENT_NAME" | sed "s/^${VENDOR}_//;s/^.*_ret_//;s/^.*_orig_//")
if [ -n "$INV_PART" ]; then
  echo "  📋 Invoice part: $INV_PART (pattern noted for $VENDOR)"
fi

echo ""
if [ "$LEARNED" -gt 0 ]; then
  echo "✅ Learned $LEARNED new clue(s) for $VENDOR."
  echo "   Future scans will use these to identify $VENDOR invoices."
else
  echo "ℹ No new clues found — everything was already known."
fi
