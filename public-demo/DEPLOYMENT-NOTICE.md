# Isolated demo deployment status

These checked-in HTML files are candidate artifacts and are not a published
site. They are built specifically for the independent public GitHub Pages
repository `bizuming1-ui/xingyi-isolated-test` (not the production repository).
Its owner must create and enable Pages separately and upload `index.html` and
`device/index.html`. Only the isolated test Supabase project is configured.
Do not use this build as a production site or enter production credentials.
The manual `Isolated public test pages` workflow checks live content and QR
only after that site exists. Real phone scanning and ESP32 operation require
separate hardware validation. Former Supabase Edge Function attempts were
removed because its gateway serves HTML as sandboxed plain text.
