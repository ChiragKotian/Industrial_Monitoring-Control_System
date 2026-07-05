#ifndef NODE_SECRETS_H
#define NODE_SECRETS_H

// AES-128 requires exactly a 16-byte (128-bit) key.
const unsigned char AES_KEY[] = "DEMO_16CHARACTER"; // 16 characters exactly

// Initialization Vector (IV) - Also 16 bytes
const unsigned char AES_IV_BASE[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#endif
