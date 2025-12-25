# lolelffs Encryption Implementation

## Overview

lolelffs now supports transparent per-block encryption with AES-256-XTS, providing data-at-rest protection for filesystem contents.

## Features

### Encryption Algorithms
- **AES-256-XTS**: Industry-standard disk encryption (fully implemented and tested)
- **ChaCha20-Poly1305**: Authenticated encryption (experimental - tag storage needs work)

### Key Derivation
- **PBKDF2-HMAC-SHA256**: Password-based key derivation
- Configurable iterations (default: 100,000)
- Random 32-byte salt per filesystem

### Architecture
- **Per-block encryption**: Each 4KB block encrypted independently
- **Block number as IV/tweak**: Deterministic IV from logical block number
- **Master key encryption**: Filesystem master key encrypted with user-derived key
- **Compress-then-encrypt**: Standard pipeline (compression before encryption)

## Usage

### Creating an Encrypted Filesystem

```bash
# Create encrypted filesystem with AES-256-XTS
lolelffs mkfs /path/to/image.img --encrypt --password "YourPassword"

# Specify encryption algorithm
lolelffs mkfs image.img --encrypt --password "pass" --algo aes-256-xts

# Specify PBKDF2 iterations (higher = more secure but slower)
lolelffs mkfs image.img --encrypt --password "pass" --iterations 200000
```

### Working with Encrypted Files

```bash
# Write encrypted data
echo "secret data" | lolelffs write --image image.img /file.txt --password "pass"

# Read encrypted data
lolelffs cat --image image.img /file.txt --password "pass"

# Copy file to encrypted filesystem
lolelffs cp --image image.img /host/file.txt /dest.txt --password "pass"

# List directory (no password needed for metadata)
lolelffs ls --image image.img /
```

### Password Management

```bash
# Unlock filesystem (validates password)
lolelffs unlock --image image.img --password "pass"

# Attempting to access encrypted data without password fails
lolelffs cat --image image.img /file.txt
# Error: Filesystem is encrypted, please provide --password

# Wrong password produces garbled data (failed decryption)
lolelffs cat --image image.img /file.txt --password "wrong"
# Output: binary garbage
```

## On-Disk Format

### Superblock Encryption Fields (104 bytes)
```c
struct lolelffs_sb_info {
    // ... existing fields ...

    // Encryption fields
    uint32_t enc_enabled;           // 0=disabled, 1=enabled
    uint32_t enc_default_algo;      // LOLELFFS_ENC_AES256_XTS, etc.
    uint32_t enc_kdf_algo;          // LOLELFFS_KDF_PBKDF2
    uint32_t enc_kdf_iterations;    // PBKDF2 iteration count
    uint32_t enc_kdf_memory;        // Reserved for Argon2
    uint32_t enc_kdf_parallelism;   // Reserved for Argon2
    uint8_t  enc_salt[32];          // Random salt for KDF
    uint8_t  enc_master_key[32];    // Encrypted master key
    uint32_t enc_features;          // Reserved
    uint32_t reserved[3];           // Alignment
};
```

### Extent Structure (24 bytes)
```c
struct lolelffs_extent {
    uint32_t ee_block;      // Logical block number
    uint32_t ee_len;        // Number of blocks
    uint32_t ee_start;      // Physical block number
    uint16_t ee_comp_algo;  // Compression algorithm
    uint8_t  ee_enc_algo;   // Encryption algorithm
    uint8_t  ee_reserved;   // Reserved
    uint16_t ee_flags;      // LOLELFFS_EXT_ENCRYPTED, etc.
    uint16_t ee_reserved2;  // Reserved
    uint32_t ee_meta;       // Metadata block pointer
};
```

### Encryption Constants
```c
// Encryption algorithms
#define LOLELFFS_ENC_NONE           0
#define LOLELFFS_ENC_AES256_XTS     1
#define LOLELFFS_ENC_CHACHA20_POLY  2

// Key derivation functions
#define LOLELFFS_KDF_NONE           0
#define LOLELFFS_KDF_ARGON2ID       1
#define LOLELFFS_KDF_PBKDF2         2

// Extent flags
#define LOLELFFS_EXT_ENCRYPTED      0x0002
```

## Implementation Status

### ✅ Completed

**Rust Userspace Tools:**
- [x] Master key encryption/decryption (AES-256-ECB for key wrap)
- [x] PBKDF2-HMAC-SHA256 key derivation
- [x] AES-256-XTS block encryption/decryption
- [x] Encrypt-then-decrypt pipeline in file I/O
- [x] Password support in CLI commands (mkfs, write, cat, cp)
- [x] Unlock command for password validation
- [x] Compress-then-encrypt pipeline

**Kernel Module:**
- [x] Encryption infrastructure (encrypt.c/h)
- [x] AES-256-XTS using Linux crypto API
- [x] PBKDF2 key derivation
- [x] Read path with decrypt-then-decompress
- [x] Encryption state management (locked/unlocked)

### 🚧 In Progress / Future Work

- [ ] ChaCha20-Poly1305 tag storage (needs 16 extra bytes per block)
- [ ] Kernel write path encryption (requires kernel 6.2+ folio APIs)
- [ ] ioctl for unlocking encrypted filesystems in kernel
- [ ] Argon2id support (memory-hard KDF)
- [ ] Encrypted directory names
- [ ] Encrypted extended attributes

## Security Considerations

### What's Protected
- ✅ File data (encrypted at rest)
- ✅ Master key (encrypted with user password)
- ✅ Protects against offline disk analysis

### What's NOT Protected
- ❌ Filenames/directory structure (stored in plaintext)
- ❌ File sizes (visible in inode)
- ❌ Access patterns (observable during use)
- ❌ Against attacks while filesystem is unlocked

### Best Practices
1. **Use strong passwords**: Minimum 12 characters, mixed case, numbers, symbols
2. **Increase PBKDF2 iterations**: For sensitive data, use 200,000+ iterations
3. **Secure password entry**: Avoid `--password` on command line (use interactive prompt)
4. **Lock when not in use**: Unmount or ensure tools exit after use
5. **Backup encrypted images**: Encrypted filesystem protects your backups too

## Performance Impact

### Encryption Overhead
- **AES-256-XTS**: ~5-10% CPU overhead (hardware-accelerated on modern CPUs)
- **PBKDF2 unlock**: 0.5-2 seconds depending on iterations
- **Memory**: +32 bytes per filesystem (master key)

### Combined Compression + Encryption
- Compress first (reduces data size)
- Then encrypt (maintains security)
- Net effect: Faster I/O from less disk activity, slightly more CPU

## Example Workflows

### Secure Document Storage
```bash
# Create encrypted filesystem
dd if=/dev/zero of=documents.img bs=1M count=100
lolelffs mkfs documents.img --encrypt --password "SecurePass123!"

# Store sensitive documents
lolelffs cp --image documents.img contract.pdf / --password "SecurePass123!"
lolelffs cp --image documents.img taxes.xlsx / --password "SecurePass123!"

# Verify encryption
strings documents.img | grep -i "confidential" || echo "Data is encrypted ✓"

# Access later
lolelffs cat --image documents.img /contract.pdf --password "SecurePass123!" > contract.pdf
```

### Encrypted Backups
```bash
# Create encrypted backup of important files
lolelffs mkfs backup.img --encrypt --password "BackupKey456"
lolelffs cp --image backup.img /etc/important.conf /backups/ --password "BackupKey456"

# Store backup.img on untrusted storage
# Data remains encrypted at rest
```

## Testing

Run the comprehensive test suite:
```bash
./test_encryption.sh
```

Expected output:
- ✓ Filesystem creation with encryption enabled
- ✓ File write with correct password succeeds
- ✓ File read with correct password succeeds
- ✓ File read with wrong password produces garbage
- ✓ File read without password shows error
- ✓ Data not visible in plaintext on disk
- ✓ Multiple files work correctly
- ✓ Large files (100KB+) with compression + encryption
- ✓ All tests pass

## Technical Details

### Key Derivation Flow
```
User Password → PBKDF2-HMAC-SHA256(password, salt, iterations) → User Key (32 bytes)
Random Master Key (32 bytes) → AES-256-ECB(master_key, user_key) → Encrypted Master Key
                                                                   ↓
                                                      Stored in Superblock
```

### Encryption Flow (Write)
```
Plaintext Data → Compress (if enabled) → Encrypt (AES-XTS with block# as IV) → Write to Disk
```

### Decryption Flow (Read)
```
Disk Data → Decrypt (AES-XTS with block# as IV) → Decompress (if compressed) → Plaintext Data
```

### Block-Level Encryption
Each 4KB block is encrypted independently:
- **IV/Tweak**: Derived from logical block number (deterministic)
- **Key**: Same master key for all blocks
- **Mode**: XTS mode (designed for disk encryption)

## Limitations

1. **Metadata not encrypted**: Filenames, sizes, timestamps visible
2. **Single password**: All files use same master key
3. **No key rotation**: Changing password requires recreating filesystem
4. **No forward secrecy**: Compromised key decrypts all historical data
5. **ChaCha20 incomplete**: Authentication tag doesn't fit in 4KB blocks

## Future Enhancements

1. **Encrypted filenames**: Store directory entries encrypted
2. **Multiple keys**: Per-file or per-directory keys
3. **Key rotation**: Change encryption without recreating filesystem
4. **Hardware key support**: Use TPM or hardware security modules
5. **Integrity verification**: Authenticated encryption for all blocks
6. **Encrypted swap**: Protect data in swap space

## References

- AES-XTS: IEEE P1619/D16 standard
- PBKDF2: RFC 2898
- Linux crypto API: Documentation/crypto/
- dm-crypt: Similar disk encryption in Linux
