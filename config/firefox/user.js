// Breezy Firefox — OEM Vertical Tabs (no Sidebery)
// Place in ~/.mozilla/firefox/<profile>/user.js

// ── Enable the new sidebar + vertical tabs UI ───────────────
user_pref("sidebar.revamp", true);
user_pref("sidebar.verticalTabs", true);

// ── Hide the horizontal tab strip ───────────────────────────
// userChrome.css no longer needed — Firefox handles this natively
// when verticalTabs is true.
