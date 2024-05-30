
#include "wii.h"

#include <string.h>

#include "Arduino.h"
#include "config.h"
#include "controllers.h"
#include "io.h"
uint8_t wiiBytes;
#ifdef INPUT_WII
uint8_t wiiPointer = 0;
bool hiRes = false;
bool encrypted = false;
bool verifyData(const uint8_t* dataIn, uint8_t dataSize) {
    uint8_t orCheck = 0x00;   // Check if data is zeroed (bad connection)
    uint8_t andCheck = 0xFF;  // Check if data is maxed (bad init)

    for (int i = 0; i < dataSize; i++) {
        orCheck |= dataIn[i];
        andCheck &= dataIn[i];
    }

    if (orCheck == 0x00 || andCheck == 0xFF) {
        return false;  // No data or bad data
    }

    return true;
}
uint16_t readExtID(void) {
    uint8_t data[WII_ID_LEN];
    memset(data, 0, sizeof(data));
    twi_readFromPointerSlow(WII_TWI_PORT, WII_ADDR, WII_READ_ID, WII_ID_LEN, data);
    delayMicroseconds(200);
    if (!verifyData(data, sizeof(data))) {
        return WII_NOT_INITIALISED;
    }
    return data[0] << 8 | data[5];
}
void initWiiExt(void) {
    wiiControllerType = readExtID();
    if (wiiControllerType == WII_NOT_INITIALISED) {
        // Send packets needed to initialise a controller
        if (!twi_writeSingleToPointer(WII_TWI_PORT, WII_ADDR, 0xF0, 0x55)) {
            return;
        }
        delayMicroseconds(10);
        twi_writeSingleToPointer(WII_TWI_PORT, WII_ADDR, 0xFB, 0x00);
        delayMicroseconds(10);
        wiiControllerType = readExtID();
        delayMicroseconds(10);
    }
    if (wiiControllerType == WII_UBISOFT_DRAWSOME_TABLET) {
        // Drawsome tablet needs some additional init
        twi_writeSingleToPointer(WII_TWI_PORT, WII_ADDR, 0xFB, 0x01);
        delayMicroseconds(10);
        twi_writeSingleToPointer(WII_TWI_PORT, WII_ADDR, 0xF0, 0x55);
        delayMicroseconds(10);
    }
    wiiPointer = 0;
    wiiBytes = 6;
    hiRes = false;
    encrypted = false;
    if (wiiControllerType == WII_CLASSIC_CONTROLLER ||
        wiiControllerType == WII_CLASSIC_CONTROLLER_PRO) {
        // Enable high-res mode (try a few times, sometimes the controller doesnt
        // pick it up)
        for (int i = 0; i < 3; i++) {
            twi_writeSingleToPointer(WII_TWI_PORT, WII_ADDR, WII_SET_RES_MODE, WII_HIGHRES_MODE);
            delayMicroseconds(200);
        }

        // Some controllers support high res mode, some dont. Some require it, some
        // dont. When a controller goes into high res mode, its ID will change,
        // so check.

        uint8_t id[WII_ID_LEN];
        twi_readFromPointerSlow(WII_TWI_PORT, WII_ADDR, WII_READ_ID, WII_ID_LEN, id);
        delayMicroseconds(200);
        if (id[4] == WII_HIGHRES_MODE) {
            hiRes = true;
            wiiBytes = 8;
        } else {
            hiRes = false;
        }
    } else if (wiiControllerType == WII_TAIKO_NO_TATSUJIN_CONTROLLER) {
        // We can cheat a little with these controllers, as most of the bytes that
        // get read back are constant. Hence we start at 0x5 instead of 0x0.
        wiiPointer = 5;
        wiiBytes = 1;
    } 
    delayMicroseconds(200);
    uint8_t data[8] = {0};
    twi_readFromPointerSlow(WII_TWI_PORT, WII_ADDR, wiiPointer, wiiBytes, data);
    uint8_t orCheck = 0x00;   
    for (int i = 0; i < wiiBytes; i++) {
        orCheck |= data[i];
    }
    // Controller requires encryption (nyko frontman)
    if (orCheck == 0) {
        delayMicroseconds(200);
        // Enable encryption
        twi_writeSingleToPointer(WII_TWI_PORT, WII_ADDR, WII_ENCRYPTION_ID, 0xAA);
        delayMicroseconds(200);
        // Write zeroed key in blocks
        uint8_t key[6] = {0};
        twi_writeToPointer(WII_TWI_PORT, WII_ADDR, WII_ENCRYPTION_KEY_ID, 6, key);
        delayMicroseconds(200);
        twi_writeToPointer(WII_TWI_PORT, WII_ADDR, WII_ENCRYPTION_KEY_ID_2, 6, key);
        delayMicroseconds(200);
        twi_writeToPointer(WII_TWI_PORT, WII_ADDR, WII_ENCRYPTION_KEY_ID_3, 4, key);
        delayMicroseconds(200);
        encrypted = true;
    }
    
}
bool initialised = false;
bool lastWiiEuphoriaLed = false;
bool wiiDataValid() {
    return initialised;
}
uint8_t* tickWii() {
    static uint8_t data[8];
    memset(data, 0, sizeof(data));
    if (wiiControllerType == WII_NOT_INITIALISED ||
        wiiControllerType == WII_NO_EXTENSION ||
        !twi_readFromPointerSlow(WII_TWI_PORT, WII_ADDR, wiiPointer, wiiBytes, data) ||
        !verifyData(data, wiiBytes)) {
        initialised = false;
        initWiiExt();
        return NULL;
    }
    if (encrypted) {
        // https://github.com/TheNathannator/WiitarThing/commit/1b81d187dbedaa7bec43c5e38bc17f6eedffaa6a
        // Nyko frontman requires using a different decryption transform seed
        uint8_t decrypted = (uint8_t)(((data[4] ^ 0x97) + 0x97) & 0xFF);
        bool useCommonValue = (data[4] != 0xFF) && ((decrypted & 0xAB) == 0xAB);

        for (int i = 0; i < 6; i++)
        {
            if (useCommonValue)
            {
                data[i] = (uint8_t)(((data[i] ^ 0x97) + 0x97) & 0xFF);
            }
            else
            {
                data[i] = (uint8_t)(((data[i] ^ 0x4D) + 0x4D) & 0xFF);
            }
        }
    }
#if DEVICE_TYPE == DJ_HERO_TURNTABLE
    // Update the led if it changes
    if (wiiControllerType == WII_DJ_HERO_TURNTABLE) {
        if (lastWiiEuphoriaLed != lastEuphoriaLed) {
            lastWiiEuphoriaLed = lastEuphoriaLed;
            twi_writeSingleToPointer(WII_TWI_PORT, WII_ADDR, WII_DJ_EUPHORIA, lastWiiEuphoriaLed);
        }
    }
#endif
    initialised = true;
    return data;
}
#endif