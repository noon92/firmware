#pragma once
#include "AES.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include <Arduino.h>

extern concurrency::Lock *cryptLock;

struct CryptoKey {
    uint8_t bytes[32];

    /// # of bytes, or -1 to mean "invalid key - do not use"
    int8_t length;
};

/**
 * see docs/software/crypto.md for details.
 *
 */

#define MAX_BLOCKSIZE 256

class CryptoEngine
{
  protected:
    /** Our per packet nonce */
    uint8_t nonce[16] = {0};

    CryptoKey key = {};
#if !(MESHTASTIC_EXCLUDE_PKI)
    uint8_t private_key[32] = {0};
#endif

  public:
#if !(MESHTASTIC_EXCLUDE_PKI)
    uint8_t public_key[32] = {0};
#endif

    virtual ~CryptoEngine() {}
#if !(MESHTASTIC_EXCLUDE_PKI)
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN)
    virtual void generateKeyPair(uint8_t *pubKey, uint8_t *privKey);
#endif
    void clearKeys();
    void setPrivateKey(uint8_t *_private_key);
    virtual void encryptCurve25519(uint32_t toNode, uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes,
                                   uint8_t *bytesOut);
    virtual bool decryptCurve25519(uint32_t fromNode, uint64_t packetNum, size_t numBytes, uint8_t *bytes, uint8_t *bytesOut);
    virtual void setDHKey(uint32_t nodeNum);
    virtual void hash(uint8_t *bytes, size_t numBytes);

    virtual void aesSetKey(const uint8_t *key, size_t key_len);

    virtual void aesEncrypt(uint8_t *in, uint8_t *out);
    AESSmall256 *aes = NULL;

#endif

    /**
     * Set the key used for encrypt, decrypt.
     *
     * As a special case: If all bytes are zero, we assume _no encryption_ and send all data in cleartext.
     *
     * @param numBytes must be 16 (AES128), 32 (AES256) or 0 (no crypt)
     * @param bytes a _static_ buffer that will remain valid for the life of this crypto instance (i.e. this class will cache the
     * provided pointer)
     */
    virtual void setKey(const CryptoKey &k);

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);
    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);

  protected:
    /**
     * Init our 128 bit nonce for a new packet
     *
     * The NONCE is constructed by concatenating (from MSB to LSB):
     * a 64 bit packet number (stored in little endian order)
     * a 32 bit sending node number (stored in little endian order)
     * a 32 bit block counter (starts at zero)
     */
    void initNonce(uint32_t fromNode, uint64_t packetId);
};

extern CryptoEngine *crypto;