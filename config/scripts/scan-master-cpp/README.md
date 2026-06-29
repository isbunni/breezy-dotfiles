# Scan Master C++

Refactored from the bash `scan-master.sh` to C++20 with a data-driven registry pattern.

## Architecture

```
scan-master (binary) — main entry point, orchestrates the pipeline
├── Pipeline: scanimage → ocrmypdf → gs → pdftotext
├── VendorRegistry: loads vendor profiles from vendors/*.json
├── VendorDetector: scores each vendor against document text
├── InvoiceExtractor: per-vendor regex-based invoice extraction
├── FileNamer: generates output filenames from templates
└── ContextLearner: learns phone/domain/city → vendor mappings
```

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Dependencies

- C++20 compiler (GCC 14+ or Clang 18+)
- CMake 3.20+
- nlohmann/json (bundled in third_party/)
- System tools: `scanimage`, `ocrmypdf`, `gs`, `pdftotext`

## Usage

```bash
# Full scan (all vendors)
./build/scan-master

# AutoZone quick-scan
./build/scan-invoice

# Teach from a corrected file
./build/scan-learn UNKNOWN_20260625_104456.pdf TEC
```

## Adding a Vendor

Drop a new `.json` file in `vendors/`. No code changes needed.

```json
{
  "name": "NewVendor",
  "keywords": [
    { "pattern": "newvendor.com", "weight": 3 },
    { "pattern": "some keyword", "weight": 2 }
  ],
  "context": {
    "phones": { "503-555-1234": "NewVendor" },
    "domains": { "newvendor.com": "NewVendor" }
  },
  "invoice_extraction": {
    "patterns": ["Invoice\\s*#?\\s*(\\d+)"],
    "validation_regex": "^\\d{5,10}$"
  },
  "file_naming": {
    "normal_template": "{vendor}_{invoice}.pdf",
    "unknown_template": "{vendor}_UNKNOWN_{date}_{time}.pdf"
  },
  "default_subtype": "Invoice"
}
```

## Key Differences from Bash Version

| | Bash | C++ |
|---|---|---|
| Vendor config | Hardcoded in if/else | External JSON files |
| Error handling | 2>/dev/null | Exceptions + result types |
| Detection | Manual scoring blocks | Data-driven scoring engine |
| Learning | Runtime jq mutations | Structured JSON updates |
| Testing | None | Unit test scaffolding |
| Regex engine | grep -oP (Perl) | std::regex (ECMAScript) |

## Notes

- The `\K` regex feature (Perl) used in the bash version is replaced with capture groups in C++ since `std::regex` doesn't support `\K`.
- All external tool calls have proper timeouts and error capture.
- Temp directories use RAII — no manual cleanup needed.
