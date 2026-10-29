# Isolated device credential vault (NOT production)

Operators can view the active device ID and, after entering the administrator
password, save an existing device secret or recover a previously saved one. A new device secret is automatically generated during provisioning; the browser
then attempts an encrypted backup and verifies it by decrypting. If either step
is interrupted, the backup must not be assumed to exist. Existing devices need
their secret imported once from local configuration; it cannot be reconstructed
from the database hash. An already leaked secret stays
compromised after backup; rotation requires a separate carefully verified
process, including handling unsynchronized device counts. This feature does
not rotate credentials, bind a device, or fix ESP32 connectivity.

The browser encrypts the 64-byte ASCII secret with AES-256-GCM, random 16-byte
salt and 12-byte nonce, using PBKDF2-HMAC-SHA256 with 600,000 iterations over
the current administrator password, and authenticates the device ID as
additional data. It sends the administrator password and existing secret to the
server over HTTPS on import so the server can authorize and compare the secret
with its existing hash. SQL persists only the encrypted envelope, not a second
plaintext secret. On retrieval, SQL checks the existing administrator rate
limit and returns only the encrypted envelope; decryption is in the browser.
The clear secret and password are never written to browser storage by this
feature. Clipboard and screen exposure remain user risks.

This is not a zero-knowledge system: the administrator password and secret are
sent to Supabase during import, and a server-side privileged party could
intercept them. Anyone who obtains an envelope can try passwords offline;
rate limiting does not protect an offline attack. Use a strong unique admin
password and trusted browser/device. Resetting or changing the admin password
without re-encrypting with the old password makes the old envelope unreadable.
No password-change workflow is implemented. Do not rely on this as the sole
backup, and do not put credentials into chat, screenshots, or Git.

This migration is limited to the isolated test project. Production deployment,
key rotation, secret logging policy, security review, password migration, and
real device/browser verification remain outstanding.
