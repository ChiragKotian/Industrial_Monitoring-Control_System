#ifndef NODE_SECRETS_H
#define NODE_SECRETS_H

// AES-128 requires exactly a 16-byte (128-bit) key.
const unsigned char AES_KEY[] = "HPCL_HACKATHON_K"; // 16 characters exactly

// Initialization Vector (IV) - Also 16 bytes
const unsigned char AES_IV_BASE[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

#endif