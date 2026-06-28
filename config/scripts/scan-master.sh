#!/bin/bash
# ============================================================
#  Scan Master v4 — McFarlands Mobile Mechanics
#  Scans from Brother ADF, OCRs, auto-labels by vendor,
#  saves to SCANS 2026 with consistent naming.
#
#  v4 changes:
#    - Context-clue learning: phone/domain/city → vendor lookups
#    - When keyword match fails, uses structural clues to guess
#    - Auto-learns new vendor clues from manual corrections
#    - Hardened vendor detection + invoice extraction
# ============================================================

set -uo pipefail

# ── Config ───────────────────────────────────────────────────
SAVE_DIR="/home/brendenb/OneDrive/Desktop/SCANS 2026"
DEVICE="brother4:net1;dev0"
RESOLUTION=300
VENDOR_DB="$HOME/.config/scripts/scan-vendors.json"

mkdir -p "$SAVE_DIR"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "============================================"
echo "  Scan Master v4 — McFarlands Mobile Mechanics"
echo "============================================"
echo ""
echo "Scanning from ADF at ${RESOLUTION}dpi..."
echo ""

# ── Step 1: Scan ─────────────────────────────────────────────
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

# scanimage returns non-zero when batch terminates early — that's OK
# as long as we got at least one page.
SCANNED=$(ls "$TMPDIR"/scan_*.tiff 2>/dev/null | wc -l)
echo "Scanned $SCANNED page(s)."
echo ""

if [ "$SCANNED" -eq 0 ]; then
  echo "ERROR: No pages were scanned. Check the scanner."
  exit 1
fi

# ── Helper Functions ─────────────────────────────────────────

# Case-insensitive text search
has() { echo "$TEXT" | grep -qi "$1"; }
has_word() { echo "$TEXT" | grep -qP "\b$1\b"; }

# Extract AutoZone invoice number — hardened against OCR garbage.
# NEVER fall back to core serial numbers (02227859060 etc).
# AutoZone invoice numbers are 11 digits starting with 02227 or 02235.
az_extract_invoice() {
  local text="$1"
  local inv=""

  # Strategy 1: "Invoice Number" label + colon + digits (clean OCR)
  inv=$(echo "$text" | grep -oP 'Invoice\s+Number\s*:\s*\K0\d{10}' | head -1)
  [ -n "$inv" ] && { echo "$inv"; return; }

  # Strategy 2: "Invoice Number" label + digits without colon (OCR skips colon)
  # e.g. "Invoice Number    102227124824" — the 1 is a stray OCR artifact
  inv=$(echo "$text" | grep -oP 'Invoice\s+Number\s+1?\s*\K0\d{10}' | head -1)
  [ -n "$inv" ] && { echo "$inv"; return; }

  # Strategy 3: "Invoice Number" label + dash/garble + digits
  # e.g. "Invoice Number     -        102227129769"
  inv=$(echo "$text" | grep -oP 'Invoice\s+Number\s*[^0-9]*\s*1?\s*\K0\d{10}' | head -1)
  [ -n "$inv" ] && { echo "$inv"; return; }

  # Strategy 4: Footer concatenation pattern
  # AutoZone footer often contains: "<invoice_11_digits><MMDDYY><letter>"
  # e.g. "02227114715061226C" → invoice=02227114715, date=061226, suffix=C
  inv=$(echo "$text" | grep -oP '\b(02227\d{6}|02235\d{6})0[1-9]\d{4}[A-Z]\b' | head -1)
  if [ -n "$inv" ]; then
    inv=${inv:0:11}  # Take first 11 chars = the invoice number
    echo "$inv"
    return
  fi

  # Strategy 5: Find 02227/02235 numbers, but EXCLUDE core serials
  # Core serials appear in the "Core Bank" / "Cores Older Than" section.
  # Build list of numbers in the Core Bank section.
  local core_section
  core_section=$(echo "$text" | sed -n '/Core Bank\|Cores Older/,/Outstanding Cores/p')
  local core_nums
  core_nums=$(echo "$core_section" | grep -oP '\b0\d{10}\b' | sort -u)

  # Find all 02227/02235 candidates
  local candidates
  candidates=$(echo "$text" | grep -oP '\b02227\d{6}\b'; echo "$text" | grep -oP '\b02235\d{6}\b')
  for c in $candidates; do
    # Skip if this number is a core serial
    if echo "$core_nums" | grep -qx "$c"; then
      continue
    fi
    # This candidate is NOT a core serial — use it
    echo "$c"
    return
  done

  # Nothing found
  echo ""
}

# Validate an extracted invoice number looks reasonable for a vendor
# Returns 0 (valid) or 1 (suspect)
validate_invoice() {
  local vendor="$1"
  local inv="$2"

  [ -z "$inv" ] && return 1

  case "$vendor" in
    AutoZone)
      # Must be 11 digits starting with 022
      [[ "$inv" =~ ^022[0-9]{8}$ ]] && return 0
      return 1
      ;;
    NAPA)
      # 6-digit number
      [[ "$inv" =~ ^[0-9]{5,7}$ ]] && return 0
      return 1
      ;;
    OReilly)
      # xxxx-xxxxxxx format or similar
      [[ "$inv" =~ ^[0-9]{4}-[0-9]{5,7}$ ]] && return 0
      return 1
      ;;
    FMI|FMP)
      # Alphanumeric, e.g. 01P163476
      [[ "$inv" =~ ^[0-9A-Za-z]{6,12}$ ]] && return 0
      return 1
      ;;
    FleetPride)
      # 9+ digit number
      [[ "$inv" =~ ^[0-9]{8,12}$ ]] && return 0
      return 1
      ;;
    TEC)
      # Alphanumeric like 11407107W
      [[ "$inv" =~ ^[0-9A-Za-z]{5,12}$ ]] && return 0
      return 1
      ;;
    SkylineFord)
      # 5-digit number like 89330
      [[ "$inv" =~ ^[0-9]{4,7}$ ]] && return 0
      return 1
      ;;
    CapitolSubaru)
      # 7-digit number
      [[ "$inv" =~ ^[0-9]{5,9}$ ]] && return 0
      return 1
      ;;
    JohnstoneSupply)
      # 4-8 digit number
      [[ "$inv" =~ ^[0-9]{4,8}$ ]] && return 0
      return 1
      ;;
    *)
      return 0  # Unknown vendor, accept anything
      ;;
  esac
}

# ── Step 2: Process each page ────────────────────────────────
PAGE_NUM=0
for SCAN in "$TMPDIR"/scan_*.tiff; do
  [ -f "$SCAN" ] || continue
  PAGE_NUM=$((PAGE_NUM + 1))

  BASENAME=$(basename "$SCAN" .tiff)
  echo "── Page $PAGE_NUM ──────────────────────────────────"

  # 2a: OCR the page
  TMPPDF="$TMPDIR/${BASENAME}.pdf"
  ocrmypdf --quiet --force-ocr --output-type pdf "$SCAN" "$TMPPDF" 2>/dev/null

  # 2b: Normalize to letter size (8.5" x 11" = 612 x 792 pts)
  LETTERPDF="$TMPDIR/${BASENAME}_letter.pdf"
  gs -dNOPAUSE -dBATCH -sDEVICE=pdfwrite \
    -dDEVICEWIDTHPOINTS=612 \
    -dDEVICEHEIGHTPOINTS=792 \
    -dFIXEDMEDIA \
    -dPDFFitPage \
    -sOutputFile="$LETTERPDF" \
    "$TMPPDF" 2>/dev/null

  # 2c: Extract text (both layout and non-layout for best matching)
  TEXT=$(pdftotext -layout "$LETTERPDF" - 2>/dev/null || echo "")
  TEXT_NOLAYOUT=$(pdftotext "$LETTERPDF" - 2>/dev/null || echo "")

  if [ -z "$TEXT" ]; then
    echo "  ⚠ No text extracted — saving as UNKNOWN"
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTFILE="$SAVE_DIR/UNKNOWN_${TIMESTAMP}.pdf"
    cp "$LETTERPDF" "$OUTFILE"
    echo "  → $(basename "$OUTFILE")"
    echo ""
    continue
  fi

  # ── Vendor Detection ──────────────────────────────────────
  # Score-based: highest score wins.

  VENDOR=""
  INVOICE_NUM=""
  SUBTYPE=""
  OUTFILE=""

  # Track best score across all vendors
  BEST_SCORE=0
  BEST_VENDOR=""

  # ─── AUTOZONE ─────────────────────────────────────────────
  AZ_SCORE=0
  if has "autozone"; then AZ_SCORE=$((AZ_SCORE+3)); fi
  if has "autozonepro"; then AZ_SCORE=$((AZ_SCORE+3)); fi
  if has "AutoZonePro.com"; then AZ_SCORE=$((AZ_SCORE+2)); fi
  if has "Commercial Invoice"; then AZ_SCORE=$((AZ_SCORE+1)); fi
  if has "Commercial Return"; then AZ_SCORE=$((AZ_SCORE+1)); fi
  # AutoZone store numbers: 02227 or 02235
  if echo "$TEXT" | grep -qP '\b0222[0-9]\b|\b0223[0-9]\b'; then AZ_SCORE=$((AZ_SCORE+2)); fi

  if [ "$AZ_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$AZ_SCORE
    BEST_VENDOR="AutoZone"
  fi

  # ─── INDUSTRIAL SOURCE (Fire/Safety Supply) ────────────────
  ISRC_SCORE=0
  if has "industrial source"; then ISRC_SCORE=$((ISRC_SCORE+4)); fi
  if has "industrialsource"; then ISRC_SCORE=$((ISRC_SCORE+4)); fi
  if has "industrial sourge"; then ISRC_SCORE=$((ISRC_SCORE+3)); fi   # OCR garble
  if has "503-763-1440"; then ISRC_SCORE=$((ISRC_SCORE+3)); fi
  if has "LPM SYSTEMS"; then ISRC_SCORE=$((ISRC_SCORE+2)); fi
  if has "Picking Ticket"; then ISRC_SCORE=$((ISRC_SCORE+1)); fi

  if [ "$ISRC_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$ISRC_SCORE
    BEST_VENDOR="IndustrialSource"
  fi

  # ─── AUTOAUTH (Vehicle Auth Service) ──────────────────────
  AAUTH_SCORE=0
  if has "autoauth"; then AAUTH_SCORE=$((AAUTH_SCORE+5)); fi
  if has "AutoAuth.com"; then AAUTH_SCORE=$((AAUTH_SCORE+4)); fi

  if [ "$AAUTH_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$AAUTH_SCORE
    BEST_VENDOR="AutoAuth"
  fi

  # ─── NAPA ─────────────────────────────────────────────────
  NAPA_SCORE=0
  if has "napa"; then NAPA_SCORE=$((NAPA_SCORE+3)); fi
  if has "lawrence.*auto parts"; then NAPA_SCORE=$((NAPA_SCORE+3)); fi
  if has "Lawrence's Auto Parts"; then NAPA_SCORE=$((NAPA_SCORE+3)); fi
  # Packing slip identifiers:
  if has "Minneapolis.*MN.*55480"; then NAPA_SCORE=$((NAPA_SCORE+3)); fi
  if echo "$TEXT" | grep -qP '\bPL\d{4}\b'; then NAPA_SCORE=$((NAPA_SCORE+2)); fi   # ProLink code PL2060
  if has "503-256-0025"; then NAPA_SCORE=$((NAPA_SCORE+2)); fi                      # NAPA Salem store phone
  if has "503.256.0025"; then NAPA_SCORE=$((NAPA_SCORE+2)); fi                      # OCR variant

  if [ "$NAPA_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$NAPA_SCORE
    BEST_VENDOR="NAPA"
  fi

  # ─── O'REILLY ─────────────────────────────────────────────
  OR_SCORE=0
  if has "o'reilly"; then OR_SCORE=$((OR_SCORE+3)); fi
  if has "oreilly"; then OR_SCORE=$((OR_SCORE+3)); fi
  if has "O'Reilly Auto Parts"; then OR_SCORE=$((OR_SCORE+3)); fi

  if [ "$OR_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$OR_SCORE
    BEST_VENDOR="OReilly"
  fi

  # ─── SKYLINE FORD ─────────────────────────────────────────
  SF_SCORE=0
  if has "skylineforddirect.com"; then SF_SCORE=$((SF_SCORE+4)); fi
  if has "skyline ford"; then SF_SCORE=$((SF_SCORE+4)); fi
  if has "SKYLINE FORD"; then SF_SCORE=$((SF_SCORE+4)); fi
  if has "Blue Oval Certified"; then SF_SCORE=$((SF_SCORE+2)); fi
  if has "blue oval certified"; then SF_SCORE=$((SF_SCORE+2)); fi
  # CDK Global copyright is often on dealer invoices
  if has "CDK Global"; then SF_SCORE=$((SF_SCORE+1)); fi

  if [ "$SF_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$SF_SCORE
    BEST_VENDOR="SkylineFord"
  fi

  # ─── TEC Equipment ─────────────
  WB_SCORE=0
  if has "tec equipment"; then WB_SCORE=$((WB_SCORE+4)); fi
  if has "TEC.*Equipment"; then WB_SCORE=$((WB_SCORE+4)); fi
  # OCR often garbles the header — WABASIH/WABASV from Wabash brand
  # TEC sells Wabash parts, so Wabash brand on invoice = TEC
  if has "wabas"; then WB_SCORE=$((WB_SCORE+3)); fi
  if has "WABAS"; then WB_SCORE=$((WB_SCORE+3)); fi
  # TEC Wilsonville address + phone
  if has "wilsonville.*or.*97070"; then WB_SCORE=$((WB_SCORE+3)); fi
  if has "7950 SW Burns Way"; then WB_SCORE=$((WB_SCORE+3)); fi
  if has "503-682-1777"; then WB_SCORE=$((WB_SCORE+3)); fi
  if has "503.682.1777"; then WB_SCORE=$((WB_SCORE+3)); fi

  if [ "$WB_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$WB_SCORE
    BEST_VENDOR="TEC"
  fi

  # ─── CDK SIMPLEPAY (Credit Card Receipts) ─────────────────
  CDK_SCORE=0
  if has "CDK SimplePay"; then CDK_SCORE=$((CDK_SCORE+5)); fi
  if has "simplepay.cdk.com"; then CDK_SCORE=$((CDK_SCORE+5)); fi
  if has "SimplePay"; then CDK_SCORE=$((CDK_SCORE+3)); fi

  if [ "$CDK_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$CDK_SCORE
    BEST_VENDOR="CDKSimplePay"
  fi

  # ─── FMI (Factory Motor Parts) ────────────────────────────
  FMI_SCORE=0
  if has "fmitrucks"; then FMI_SCORE=$((FMI_SCORE+3)); fi
  if has "fmi truck"; then FMI_SCORE=$((FMI_SCORE+3)); fi
  if has "factory motor parts"; then FMI_SCORE=$((FMI_SCORE+3)); fi
  if has "FMI"; then FMI_SCORE=$((FMI_SCORE+2)); fi

  if [ "$FMI_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$FMI_SCORE
    BEST_VENDOR="FMI"
  fi

  # ─── FMP (Factory Motor Parts — alternate branding) ───────
  FMP_SCORE=0
  if has "factory motor parts"; then FMP_SCORE=$((FMP_SCORE+3)); fi
  if echo "$TEXT" | grep -qP 'FMP[-\s]'; then FMP_SCORE=$((FMP_SCORE+2)); fi

  if [ "$FMP_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$FMP_SCORE
    BEST_VENDOR="FMP"
  fi

  # ─── FLEETPRIDE ───────────────────────────────────────────
  FP_SCORE=0
  if has "fleetpride"; then FP_SCORE=$((FP_SCORE+3)); fi
  if has "FleetPride"; then FP_SCORE=$((FP_SCORE+3)); fi

  if [ "$FP_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$FP_SCORE
    BEST_VENDOR="FleetPride"
  fi

  # ─── IMC (Parts Authority) ────────────────────────────────
  IMC_SCORE=0
  if has "imcparts"; then IMC_SCORE=$((IMC_SCORE+3)); fi
  if has "imc.*parts authority"; then IMC_SCORE=$((IMC_SCORE+3)); fi
  if has "parts authority"; then IMC_SCORE=$((IMC_SCORE+2)); fi
  if has "IMC"; then IMC_SCORE=$((IMC_SCORE+1)); fi

  if [ "$IMC_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$IMC_SCORE
    BEST_VENDOR="IMC"
  fi

  # ─── CAPITOL SUBARU ──────────────────────────────────────
  CS_SCORE=0
  if has "capitol.*subaru"; then CS_SCORE=$((CS_SCORE+3)); fi
  if has "capitolsubaru.com"; then CS_SCORE=$((CS_SCORE+3)); fi
  if has "capitol subaru"; then CS_SCORE=$((CS_SCORE+3)); fi

  if [ "$CS_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$CS_SCORE
    BEST_VENDOR="CapitolSubaru"
  fi

  # ─── AMAZON ───────────────────────────────────────────────
  AMZ_SCORE=0
  if has "amazon.com"; then AMZ_SCORE=$((AMZ_SCORE+3)); fi
  if has "amazon.ca"; then AMZ_SCORE=$((AMZ_SCORE+3)); fi
  if has_word "AMAZON"; then AMZ_SCORE=$((AMZ_SCORE+2)); fi
  if echo "$TEXT" | grep -qP '\b\d{3}-\d{7}-\d{7}\b'; then AMZ_SCORE=$((AMZ_SCORE+2)); fi

  if [ "$AMZ_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$AMZ_SCORE
    BEST_VENDOR="AMZ"
  fi

  # ─── JOHNSTONE SUPPLY ────────────────────────────────────
  JS_SCORE=0
  if has "johnstone"; then JS_SCORE=$((JS_SCORE+4)); fi
  if has "johnstone.*supply"; then JS_SCORE=$((JS_SCORE+3)); fi
  # Johnstone Supply Salem: 3080 22ND ST SE, Salem OR 97302
  if has "3080.*22ND"; then JS_SCORE=$((JS_SCORE+2)); fi
  if has "97302"; then JS_SCORE=$((JS_SCORE+1)); fi

  if [ "$JS_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$JS_SCORE
    BEST_VENDOR="JohnstoneSupply"
  fi

  # ─── 1-800-RADIATOR ───────────────────────────────────────
  RAD_SCORE=0
  if has "1-800-radiator"; then RAD_SCORE=$((RAD_SCORE+3)); fi
  if has "800.*radiator"; then RAD_SCORE=$((RAD_SCORE+2)); fi
  if has "radiator.*supply"; then RAD_SCORE=$((RAD_SCORE+2)); fi

  if [ "$RAD_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$RAD_SCORE
    BEST_VENDOR="1800Radiator"
  fi

  # ─── FLEMING FLEET ────────────────────────────────────────
  FLM_SCORE=0
  if has "fleming fleet"; then FLM_SCORE=$((FLM_SCORE+3)); fi
  if has "fleming"; then FLM_SCORE=$((FLM_SCORE+1)); fi

  if [ "$FLM_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$FLM_SCORE
    BEST_VENDOR="FlemingFleet"
  fi

  # ─── ACE / HARBOR FREIGHT ─────────────────────────────────
  ACE_SCORE=0
  if has "ace.*freight|harbor.*freight|ace.*truck"; then ACE_SCORE=$((ACE_SCORE+3)); fi
  if has "ace.*supply|ace.*parts"; then ACE_SCORE=$((ACE_SCORE+2)); fi

  if [ "$ACE_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$ACE_SCORE
    BEST_VENDOR="ACE"
  fi

  # ─── DOT (Vehicle Inspections) ────────────────────────────
  DOT_SCORE=0
  if has "dot.*inspection|vehicle.*inspection"; then DOT_SCORE=$((DOT_SCORE+3)); fi
  if has "oregon.*dot"; then DOT_SCORE=$((DOT_SCORE+2)); fi
  if has "inspection.*report|vehicle.*inspection.*report"; then DOT_SCORE=$((DOT_SCORE+2)); fi

  if [ "$DOT_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$DOT_SCORE
    BEST_VENDOR="DOT"
  fi

  # ─── TAX DOCUMENTS ────────────────────────────────────────
  TAX_SCORE=0
  if has "w-9|w9"; then TAX_SCORE=$((TAX_SCORE+3)); fi
  if has "tax.*return|1099"; then TAX_SCORE=$((TAX_SCORE+2)); fi
  if has "irs|internal revenue"; then TAX_SCORE=$((TAX_SCORE+2)); fi

  if [ "$TAX_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$TAX_SCORE
    BEST_VENDOR="TAX"
  fi

  # ─── CHECKS / BANK DOCS ───────────────────────────────────
  CHK_SCORE=0
  if has "void.*check|check.*number"; then CHK_SCORE=$((CHK_SCORE+3)); fi
  if has "account.*number|routing.*number"; then CHK_SCORE=$((CHK_SCORE+2)); fi

  if [ "$CHK_SCORE" -gt "$BEST_SCORE" ]; then
    BEST_SCORE=$CHK_SCORE
    BEST_VENDOR="BANK"
  fi

  # ── Assign vendor ─────────────────────────────────────────
  if [ "$BEST_SCORE" -ge 2 ]; then
    VENDOR="$BEST_VENDOR"
  else
    # ── Context-clue learning fallback ──────────────────────
    # When keyword matching scores < 2, use structural clues
    # from the document: phone numbers, domains, addresses, cities.
    # This catches vendors we haven't coded keywords for yet.
    VENDOR=""
    CLUE_SOURCE=""

    # 1. Phone number lookup
    if [ -z "$VENDOR" ] && [ -f "$VENDOR_DB" ]; then
      DOC_PHONES=$(echo "$TEXT" | grep -oP '\b\d{3}[-.]\d{3}[-.]\d{4}\b' | sort -u)
      for phone in $DOC_PHONES; do
        PHONE_VENDOR=$(jq -r ".phones[\"$phone\"] // empty" "$VENDOR_DB" 2>/dev/null)
        if [ -n "$PHONE_VENDOR" ] && [ "$PHONE_VENDOR" != "null" ]; then
          VENDOR="$PHONE_VENDOR"
          CLUE_SOURCE="phone:$phone"
          break
        fi
      done
    fi

    # 2. Domain lookup
    if [ -z "$VENDOR" ] && [ -f "$VENDOR_DB" ]; then
      DOC_DOMAINS=$(echo "$TEXT" | grep -oP '[\w.-]+\.(com|net|org)' | sort -u)
      for domain in $DOC_DOMAINS; do
        DOMAIN_VENDOR=$(jq -r ".domains[\"$domain\"] // empty" "$VENDOR_DB" 2>/dev/null)
        if [ -n "$DOMAIN_VENDOR" ] && [ "$DOMAIN_VENDOR" != "null" ]; then
          VENDOR="$DOMAIN_VENDOR"
          CLUE_SOURCE="domain:$domain"
          break
        fi
      done
    fi

    # 3. City/ZIP lookup
    if [ -z "$VENDOR" ] && [ -f "$VENDOR_DB" ]; then
      # Extract city + 5-digit zip pairs
      DOC_CITIES=$(echo "$TEXT" | grep -oiP '(wilsonville|keizer|newberg|portland|minneapolis|eugene|springfield|tigard|woodburn)[^0-9]*(97[0-9]{3}|55[0-9]{3})' | head -5)
      for cityzip in $DOC_CITIES; do
        CITY_KEY=$(echo "$cityzip" | sed 's/[, ]/|/g; s/||*/|/g' | sed 's/^|//;s/|$//')
        CITY_VENDOR=$(jq -r ".cities[\"$CITY_KEY\"] // empty" "$VENDOR_DB" 2>/dev/null)
        if [ -n "$CITY_VENDOR" ] && [ "$CITY_VENDOR" != "null" ]; then
          VENDOR="$CITY_VENDOR"
          CLUE_SOURCE="city:$CITY_KEY"
          break
        fi
      done
      # Also try simple city→vendor without exact key match
      if [ -z "$VENDOR" ]; then
        CITY_COUNT=$(jq '.cities | length' "$VENDOR_DB" 2>/dev/null)
        for key in $(jq -r '.cities | keys[]' "$VENDOR_DB" 2>/dev/null); do
          CITY_PART=$(echo "$key" | cut -d'|' -f1)
          if echo "$TEXT" | grep -qi "$CITY_PART"; then
            VENDOR=$(jq -r ".cities[\"$key\"]" "$VENDOR_DB" 2>/dev/null)
            CLUE_SOURCE="city:$key"
            break
          fi
        done
      fi
    fi

    # 4. Structural clues — document type heuristics
    if [ -z "$VENDOR" ]; then
      # Credit card receipt pattern
      if has "Transaction Amount" && has "Auth Code"; then
        VENDOR="CDKSimplePay"
        CLUE_SOURCE="struct:txn_receipt"
      # Parts invoice from a dealer (CDK Global/ADP)
      elif has "Parts Invoice" && (has "CDK Global" || has "Copyright.*ADP"); then
        VENDOR="DealerInvoice"
        CLUE_SOURCE="struct:cdk_parts"
      # Credit card transaction
      elif has "Transaction Type" && has "Approved"; then
        VENDOR="CCReceipt"
        CLUE_SOURCE="struct:cc_receipt"
      fi
    fi

    # 5. If still unknown, mark it
    if [ -z "$VENDOR" ]; then
      VENDOR="UNKNOWN"
    fi

    if [ -n "$CLUE_SOURCE" ]; then
      echo "  🔍 Vendor guessed from $CLUE_SOURCE (score was $BEST_SCORE)"
    fi
  fi

  # ── Invoice Number Extraction (per vendor) ────────────────

  case "$VENDOR" in
    AutoZone)
      # Determine subtype
      IS_RETURN=false
      IS_AR_CREDIT=false
      IS_LABOR=false

      if has "Commercial Return"; then IS_RETURN=true; fi
      if has "AR CREDIT"; then IS_AR_CREDIT=true; fi
      if has "COMM.LABOR" || has "COMM. LABOR"; then IS_LABOR=true; fi

      if $IS_RETURN && $IS_LABOR; then
        SUBTYPE="LaborClaim"
        RETURN_NUM=$(echo "$TEXT" | grep -oP 'Return Invoice Number\s*:\s*\K\d+' | head -1)
        if [ -z "$RETURN_NUM" ]; then
          # Fallback: first Return Invoice line with a valid AZ number
          RETURN_NUM=$(echo "$TEXT" | grep -oP 'Return Invoice Number\s*[^0-9]*\s*1?\s*\K0\d{10}' | head -1)
        fi
      elif $IS_RETURN && $IS_AR_CREDIT; then
        SUBTYPE="ARCreditReturn"
        RETURN_NUM=$(echo "$TEXT" | grep -oP 'Return Invoice Number\s*:\s*\K\d+' | head -1)
        if [ -z "$RETURN_NUM" ]; then
          RETURN_NUM=$(echo "$TEXT" | grep -oP 'Return Invoice Number\s*[^0-9]*\s*1?\s*\K0\d{10}' | head -1)
        fi
        ORIG_NUM=$(echo "$TEXT" | grep -oP 'Original Invoice Number\s*[-:]\s*\K\d+' | head -1)
        if [ -z "$ORIG_NUM" ]; then
          ORIG_NUM=$(echo "$TEXT" | grep -oP 'Original Invoice Number\s*[^0-9]*\s*1?\s*\K0\d{10}' | head -1)
        fi
      elif $IS_AR_CREDIT; then
        SUBTYPE="ARCredit"
        INVOICE_NUM=$(az_extract_invoice "$TEXT")
      else
        SUBTYPE="Invoice"
        INVOICE_NUM=$(az_extract_invoice "$TEXT")
      fi

      # Validate extracted numbers
      if [ -n "$INVOICE_NUM" ]; then
        if ! validate_invoice AutoZone "$INVOICE_NUM"; then
          echo "  ⚠ Invoice '$INVOICE_NUM' failed validation — clearing"
          INVOICE_NUM=""
        fi
      fi
      if [ -n "${RETURN_NUM:-}" ]; then
        if ! validate_invoice AutoZone "$RETURN_NUM"; then
          echo "  ⚠ Return# '$RETURN_NUM' failed validation — clearing"
          RETURN_NUM=""
        fi
      fi
      if [ -n "${ORIG_NUM:-}" ]; then
        if ! validate_invoice AutoZone "$ORIG_NUM"; then
          echo "  ⚠ Orig# '$ORIG_NUM' failed validation — clearing"
          ORIG_NUM=""
        fi
      fi

      # Build filename
      if [ -n "$SUBTYPE" ] && [ "$SUBTYPE" != "Invoice" ]; then
        if [ -n "${ORIG_NUM:-}" ] && [ -n "${RETURN_NUM:-}" ]; then
          OUTFILE="$SAVE_DIR/AutoZone_${SUBTYPE}_ret_${RETURN_NUM}_orig_${ORIG_NUM}.pdf"
        elif [ -n "${RETURN_NUM:-}" ]; then
          OUTFILE="$SAVE_DIR/AutoZone_${SUBTYPE}_ret_${RETURN_NUM}.pdf"
        elif [ -n "$INVOICE_NUM" ]; then
          OUTFILE="$SAVE_DIR/AutoZone_${SUBTYPE}_${INVOICE_NUM}.pdf"
        fi
      elif [ -n "$INVOICE_NUM" ]; then
        OUTFILE="$SAVE_DIR/AutoZone_${INVOICE_NUM}.pdf"
      fi
      ;;

    NAPA)
      # Try standard invoice number extraction
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s+Number\s*[: ]*\K[0-9]+' | head -1)
      # Packing slip: extract PO/Order number (format: 17xxx-xx)
      if [ -z "$INVOICE_NUM" ]; then
        INVOICE_NUM=$(echo "$TEXT_NOLAYOUT" | grep -oP 'Invoice\s*Number\s*[: ]*\K[0-9]+' | head -1)
      fi
      # Packing slip format: order number near ProLink code
      if [ -z "$INVOICE_NUM" ]; then
        # Look for NAPA order numbers like 17254-20
        INVOICE_NUM=$(echo "$TEXT" | grep -oP '\b17[0-9]{3}-[0-9]{2}\b' | head -1)
        if [ -n "$INVOICE_NUM" ]; then
          SUBTYPE="packslip"
        fi
      fi
      # Try NAPA invoice # from header area (5-7 digits)
      if [ -z "$INVOICE_NUM" ]; then
        INVOICE_NUM=$(echo "$TEXT" | grep -oP '\b[0-9]{5,7}\b' | head -1)
      fi

      if [ -n "$INVOICE_NUM" ]; then
        if [ -n "$SUBTYPE" ]; then
          OUTFILE="$SAVE_DIR/NAPA_${INVOICE_NUM}_${SUBTYPE}.pdf"
        else
          OUTFILE="$SAVE_DIR/NAPA_${INVOICE_NUM}.pdf"
        fi
      fi
      ;;

    OReilly)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s+\K[\d-]+' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*#?\s*\K[\d-]+' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/OReilly_${INVOICE_NUM}.pdf"
      ;;

    SkylineFord)
      # Invoice NUMBER field: 5-digit number like 89330
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'INVOICE\s+NUMBER\s+\K[0-9]{4,7}' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s+Number\s*[: ]+\K[0-9]{4,7}' | head -1)
      # Fallback: look for 5-digit standalone number after "NUMBER"
      if [ -z "$INVOICE_NUM" ]; then
        INVOICE_NUM=$(echo "$TEXT" | grep -oP 'NUMBER\s+\K[0-9]{5}\b' | head -1)
      fi
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/SkylineFord_${INVOICE_NUM}.pdf"
      ;;

    TEC)
      # Invoice NUMBER field: alphanumeric like 11407107W
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'NUMBER\s+\K[0-9A-Za-z]{5,12}' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s+Number\s*[: ]+\K[0-9A-Za-z]{5,12}' | head -1)
      # Fallback: look for invoice-like number with W suffix
      if [ -z "$INVOICE_NUM" ]; then
        INVOICE_NUM=$(echo "$TEXT" | grep -oP '\b[0-9]{7,10}[A-Z]\b' | head -1)
      fi
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/TEC_${INVOICE_NUM}.pdf"
      ;;

    CDKSimplePay)
      # Transaction receipt — use auth code + date
      AUTH_CODE=$(echo "$TEXT" | grep -oP 'Auth Code\s+\K[0-9A-Za-z]+' | head -1)
      TXN_DATE=$(echo "$TEXT" | grep -oP '([0-9]{1,2}/[0-9]{1,2}/[0-9]{2,4})' | head -1)
      # Convert date to compact format
      if [ -n "$TXN_DATE" ]; then
        TXN_COMPACT=$(echo "$TXN_DATE" | sed 's/\///g')
      else
        TXN_COMPACT=$(date +%Y%m%d)
      fi
      if [ -n "$AUTH_CODE" ]; then
        OUTFILE="$SAVE_DIR/CDKSimplePay_${AUTH_CODE}_${TXN_COMPACT}.pdf"
      else
        # Use transaction ID instead
        TXN_ID=$(echo "$TEXT" | grep -oP 'Transaction Id\s+\K[0-9a-f]+' | head -1)
        if [ -n "$TXN_ID" ]; then
          OUTFILE="$SAVE_DIR/CDKSimplePay_${TXN_ID}.pdf"
        fi
      fi
      ;;

    FMI)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice:\s*\K[\w-]+' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*\K[\w.-]+' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/FMI_${INVOICE_NUM}.pdf"
      ;;

    FMP)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'INVOICE NO\.\s*\K[\d-]+' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*(No|Number|#)\.?\s*\K[\d-]+' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/FMP_${INVOICE_NUM}.pdf"
      ;;

    FleetPride)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'INVOICE NUMBER\s*\K\d+' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*(Number|No|#)\s*:?\s*\K\d+' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/FleetPride_${INVOICE_NUM}.pdf"
      ;;

    IMC)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'INV\. NO\.\s*\K[\d-]+' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP '\b\d{3}-\d{6}\b' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/IMC_${INVOICE_NUM}.pdf"
      ;;

    CapitolSubaru)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice Number\s*\K\d+' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP '\b\d{6,8}\b' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*(Number|#|No)\s*:?\s*\K[\w-]+' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/CapitolSubaru_${INVOICE_NUM}.pdf"
      ;;

    AMZ)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP '\b\d{3}-\d{7}-\d{7}\b' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/AMZ_${INVOICE_NUM}.pdf"
      ;;

    JohnstoneSupply)
      # Invoice # near top: "Invoice # 5000" or "Invoice Number 5000"
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*(Number|#|No)\s*:?\s*\K\d+' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*(Number|#|No)\s*:?\s*\K[\w-]+' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/JohnstoneSupply_${INVOICE_NUM}.pdf"
      ;;

    1800Radiator)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Order\s*(Number|#|No)\s*:?\s*\K\d+' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*(Number|#|No)\s*:?\s*\K\d+' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/1800Radiator_${INVOICE_NUM}.pdf"
      ;;

    FlemingFleet)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*(Number|#|No)\s*:?\s*\K[\w-]+' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/FlemingFleet_${INVOICE_NUM}.pdf"
      ;;

    ACE)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Invoice\s*(Number|#|No)\s*:?\s*\K[\w-]+' | head -1)
      [ -n "$INVOICE_NUM" ] && OUTFILE="$SAVE_DIR/ACE_${INVOICE_NUM}.pdf"
      ;;

    DOT)
      INVOICE_NUM=$(echo "$TEXT" | grep -oP '(RO|Report|Invoice)\s*(Number|#|No)\s*:?\s*\K[\w-]+' | head -1)
      if [ -n "$INVOICE_NUM" ]; then
        OUTFILE="$SAVE_DIR/DOT_${INVOICE_NUM}.pdf"
      else
        TIMESTAMP=$(date +%Y%m%d_%H%M%S)
        OUTFILE="$SAVE_DIR/DOT_${TIMESTAMP}.pdf"
      fi
      ;;

    TAX)
      TIMESTAMP=$(date +%Y%m%d_%H%M%S)
      OUTFILE="$SAVE_DIR/TAX_${TIMESTAMP}.pdf"
      ;;

    BANK)
      TIMESTAMP=$(date +%Y%m%d_%H%M%S)
      OUTFILE="$SAVE_DIR/BANK_${TIMESTAMP}.pdf"
      ;;

    AutoAuth)
      # Online order — use order ID like rqd2js36
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Order\s*#\s*\K[\w-]+' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Order\s+Number[:\s]*\K[\w-]+' | head -1)
      if [ -n "$INVOICE_NUM" ]; then
        OUTFILE="$SAVE_DIR/AutoAuth_${INVOICE_NUM}.pdf"
      fi
      ;;

    IndustrialSource)
      # Picking ticket — order number like 0003103783-00
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'Order[^0-9]*\K\d{5,10}-\d{2}' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP '\d{9}-\d{2}' | head -1)
      if [ -n "$INVOICE_NUM" ]; then
        OUTFILE="$SAVE_DIR/IndustrialSource_${INVOICE_NUM}.pdf"
      fi
      ;;

    CDKSimplePay|CCReceipt)
      # Transaction receipt — use auth code + date
      AUTH_CODE=$(echo "$TEXT" | grep -oP 'Auth Code\s+\K[0-9A-Za-z]+' | head -1)
      TXN_DATE=$(echo "$TEXT" | grep -oP '([0-9]{1,2}/[0-9]{1,2}/[0-9]{2,4})' | head -1)
      if [ -n "$TXN_DATE" ]; then
        TXN_COMPACT=$(echo "$TXN_DATE" | sed 's/\///g')
      else
        TXN_COMPACT=$(date +%Y%m%d)
      fi
      if [ -n "$AUTH_CODE" ]; then
        OUTFILE="$SAVE_DIR/CDKSimplePay_${AUTH_CODE}_${TXN_COMPACT}.pdf"
        VENDOR="CDKSimplePay"
      else
        TXN_ID=$(echo "$TEXT" | grep -oP 'Transaction Id\s+\K[0-9a-f]+' | head -1)
        if [ -n "$TXN_ID" ]; then
          OUTFILE="$SAVE_DIR/CDKSimplePay_${TXN_ID}.pdf"
          VENDOR="CDKSimplePay"
        fi
      fi
      ;;

    DealerInvoice)
      # Generic dealer parts invoice — try to extract invoice number
      INVOICE_NUM=$(echo "$TEXT" | grep -oP 'INVOICE\s+NUMBER\s+\K[0-9]{4,7}' | head -1)
      [ -z "$INVOICE_NUM" ] && INVOICE_NUM=$(echo "$TEXT" | grep -oP 'NUMBER\s+\K[0-9A-Za-z]{5,12}' | head -1)
      if [ -n "$INVOICE_NUM" ]; then
        OUTFILE="$SAVE_DIR/Dealer_${INVOICE_NUM}.pdf"
      fi
      ;;

    UNKNOWN)
      TIMESTAMP=$(date +%Y%m%d_%H%M%S)
      OUTFILE="$SAVE_DIR/UNKNOWN_${TIMESTAMP}.pdf"
      ;;
  esac

  # ── Fallback if no OUTFILE set ────────────────────────────
  if [ -z "$OUTFILE" ]; then
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTFILE="$SAVE_DIR/${VENDOR}_UNKNOWN_${TIMESTAMP}.pdf"
    echo "  ⚠ No invoice number extracted"
  fi

  # ── Auto-learn: save discovered clues to vendor DB ───────
  # When we guessed from context clues and got a known vendor,
  # record any new phone/domain clues we found so future scans
  # can match faster.
  CLUE_SOURCE="${CLUE_SOURCE:-}"
  if [ -n "$CLUE_SOURCE" ] && [ "$VENDOR" != "UNKNOWN" ] && [ -f "$VENDOR_DB" ] && command -v jq >/dev/null 2>&1; then
    # Learn new phone numbers
    DOC_PHONES=$(echo "$TEXT" | grep -oP '\b\d{3}[-.]\d{3}[-.]\d{4}\b' | sort -u)
    for phone in $DOC_PHONES; do
      EXISTING=$(jq -r ".phones[\"$phone\"] // empty" "$VENDOR_DB" 2>/dev/null)
      if [ -z "$EXISTING" ] || [ "$EXISTING" = "null" ]; then
        # New phone → add to DB
        jq ".phones[\"$phone\"] = \"$VENDOR\"" "$VENDOR_DB" > "${VENDOR_DB}.tmp" 2>/dev/null && mv "${VENDOR_DB}.tmp" "$VENDOR_DB"
        echo "  📖 Learned: $phone → $VENDOR"
      fi
    done

    # Learn new domains
    DOC_DOMAINS=$(echo "$TEXT" | grep -oP '[\w.-]+\.(com|net|org)' | sort -u)
    for domain in $DOC_DOMAINS; do
      EXISTING=$(jq -r ".domains[\"$domain\"] // empty" "$VENDOR_DB" 2>/dev/null)
      if [ -z "$EXISTING" ] || [ "$EXISTING" = "null" ]; then
        jq ".domains[\"$domain\"] = \"$VENDOR\"" "$VENDOR_DB" > "${VENDOR_DB}.tmp" 2>/dev/null && mv "${VENDOR_DB}.tmp" "$VENDOR_DB"
        echo "  📖 Learned: $domain → $VENDOR"
      fi
    done
  fi

  # ── Save ──────────────────────────────────────────────────
  # Handle duplicate filenames by appending a counter
  FINAL="$OUTFILE"
  COUNTER=1
  while [ -f "$FINAL" ]; do
    EXT="${OUTFILE##*.}"
    BASE="${OUTFILE%.*}"
    FINAL="${BASE}_${COUNTER}.${EXT}"
    COUNTER=$((COUNTER + 1))
  done

  cp "$LETTERPDF" "$FINAL"
  echo "  Vendor: $VENDOR (score: ${BEST_SCORE})"
  if [ -n "$SUBTYPE" ]; then
    echo "  Subtype: $SUBTYPE"
  fi
  echo "  → $(basename "$FINAL")"
  echo ""
done

# ── Summary ──────────────────────────────────────────────────
TOTAL=$(ls "$SAVE_DIR"/*.pdf 2>/dev/null | wc -l)
echo "============================================"
echo "  Done! $PAGE_NUM page(s) processed."
echo "  Total files in SCANS 2026: $TOTAL"
echo "============================================"
