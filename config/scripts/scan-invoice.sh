#!/bin/bash
# ============================================================
#  Scan Invoice — AutoZone Only (Quick Scan)
#  Scans from Brother ADF, OCRs, auto-labels AutoZone invoices.
# ============================================================

set -uo pipefail

SAVE_DIR="/home/brendenb/OneDrive/Desktop/SCANS 2026"
DEVICE="brother4:net1;dev0"
RESOLUTION=300

mkdir -p "$SAVE_DIR"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "============================================"
echo "  Scan Invoice — AutoZone Only"
echo "============================================"
echo ""

scanimage \
  --device-name="$DEVICE" \
  --resolution=$RESOLUTION \
  --format=tiff \
  --batch="$TMPDIR/scan_%03d.tiff" \
  --batch-count=99 \
  --source="Automatic Document Feeder(left aligned)" \
  -x 215.9 \
  -y 279.4 \
  2>/dev/null

SCANNED=$(ls "$TMPDIR"/scan_*.tiff 2>/dev/null | wc -l)
echo "Scanned $SCANNED page(s)."
echo ""

if [ "$SCANNED" -eq 0 ]; then
  echo "ERROR: No pages were scanned."
  exit 1
fi

PAGE_NUM=0
for SCAN in "$TMPDIR"/scan_*.tiff; do
  [ -f "$SCAN" ] || continue
  PAGE_NUM=$((PAGE_NUM + 1))

  BASENAME=$(basename "$SCAN" .tiff)
  echo "── Page $PAGE_NUM ──────────────────────────────────"

  TMPPDF="$TMPDIR/${BASENAME}.pdf"
  ocrmypdf --quiet --force-ocr --output-type pdf "$SCAN" "$TMPPDF" 2>/dev/null

  LETTERPDF="$TMPDIR/${BASENAME}_letter.pdf"
  gs -dNOPAUSE -dBATCH -sDEVICE=pdfwrite \
    -dDEVICEWIDTHPOINTS=612 \
    -dDEVICEHEIGHTPOINTS=792 \
    -dFIXEDMEDIA \
    -dPDFFitPage \
    -sOutputFile="$LETTERPDF" \
    "$TMPPDF" 2>/dev/null

  TEXT=$(pdftotext -layout "$LETTERPDF" - 2>/dev/null || echo "")

  if [ -z "$TEXT" ]; then
    echo "  ⚠ No text — saving as UNKNOWN"
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTFILE="$SAVE_DIR/UNKNOWN_${TIMESTAMP}.pdf"
    cp "$LETTERPDF" "$OUTFILE"
    echo "  → $(basename "$OUTFILE")"
    echo ""
    continue
  fi

  # Auto-detect: is this actually AutoZone?
  IS_AZ=false
  if echo "$TEXT" | grep -qi "autozone\|autozonepro\|AutoZonePro.com"; then
    IS_AZ=true
  fi
  if echo "$TEXT" | grep -qP '\b0\d{10}\b'; then
    IS_AZ=true
  fi

  if ! $IS_AZ; then
    echo "  ⚠ Not recognized as AutoZone — saving as UNKNOWN"
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTFILE="$SAVE_DIR/UNKNOWN_${TIMESTAMP}.pdf"
    cp "$LETTERPDF" "$OUTFILE"
    echo "  → $(basename "$OUTFILE")"
    echo ""
    continue
  fi

  # Determine subtype
  IS_RETURN=false
  IS_AR_CREDIT=false
  IS_LABOR=false

  echo "$TEXT" | grep -q "Commercial Return" && IS_RETURN=true
  echo "$TEXT" | grep -qi "AR CREDIT" && IS_AR_CREDIT=true
  echo "$TEXT" | grep -qP "COMM\.?\s*LABOR" && IS_LABOR=true

  OUTFILE=""

  if $IS_RETURN && $IS_LABOR; then
    RETURN_NUM=$(echo "$TEXT" | grep -oP 'Return Invoice Number\s*:\s*\K\d+' | head -1)
    [ -z "$RETURN_NUM" ] && RETURN_NUM=$(echo "$TEXT" | grep -oP '\b0\d{10}\b' | head -1)
    [ -n "$RETURN_NUM" ] && OUTFILE="$SAVE_DIR/AutoZone_LaborClaim_ret_${RETURN_NUM}.pdf"

  elif $IS_RETURN && $IS_AR_CREDIT; then
    RETURN_NUM=$(echo "$TEXT" | grep -oP 'Return Invoice Number\s*:\s*\K\d+' | head -1)
    [ -z "$RETURN_NUM" ] && RETURN_NUM=$(echo "$TEXT" | grep -oP '\b0\d{10}\b' | head -1)
    ORIG_NUM=$(echo "$TEXT" | grep -oP 'Original Invoice Number\s*[-:]\s*\K\d+' | head -1)
    if [ -n "$RETURN_NUM" ] && [ -n "$ORIG_NUM" ]; then
      OUTFILE="$SAVE_DIR/AutoZone_ARCreditReturn_ret_${RETURN_NUM}_orig_${ORIG_NUM}.pdf"
    elif [ -n "$RETURN_NUM" ]; then
      OUTFILE="$SAVE_DIR/AutoZone_ARCreditReturn_ret_${RETURN_NUM}.pdf"
    fi

  elif $IS_AR_CREDIT; then
    INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice Number\s*:\s*\K\d+' | head -1)
    [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP '\b0\d{10}\b' | head -1)
    [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/AutoZone_ARCredit_${INVOICE_NUM}.pdf"

  else
    INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice Number\s*:\s*\K\d+' | head -1)
    [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP ':\s*\K\d{11}' | head -1)
    [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP '\b0\d{10}\b' | head -1)
    [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/AutoZone_${INVOICE_NUM}.pdf"
  fi

  if [ -z "$OUTFILE" ]; then
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTFILE="$SAVE_DIR/AutoZone_UNKNOWN_${TIMESTAMP}.pdf"
  fi

  FINAL="$OUTFILE"
  COUNTER=1
  while [ -f "$FINAL" ]; do
    EXT="${OUTFILE##*.}"
    BASE="${OUTFILE%.*}"
    FINAL="${BASE}_${COUNTER}.${EXT}"
    COUNTER=$((COUNTER + 1))
  done

  cp "$LETTERPDF" "$FINAL"
  echo "  → $(basename "$FINAL")"
  echo ""
done

echo "============================================"
echo "  Done! $PAGE_NUM page(s) processed."
echo "============================================"
