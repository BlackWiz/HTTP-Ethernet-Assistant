/*
 * thingspeak.h
 *
 *  Created on: Feb 15, 2026
 *      Author: ravik
 */

#ifndef INC_THINGSPEAK_H_
#define INC_THINGSPEAK_H_

#include "main.h"

// --- Configuration ---
#define TS_API_KEY      "CRGIO6YT0XPKPCTG"
#define TS_HOST         "api.thingspeak.com"
#define TS_PORT         80

// --- Public API ---

/**
 * @brief  Initializes the ThingSpeak client state machine.
 * Call this ONCE in main() before the while(1) loop.
 */
void thingspeak_init(void);

/**
 * @brief  Triggers a data upload to the cloud.
 * Call this periodically (e.g., every 20s).
 * @param  val1 : Value for Field 1
 * @param  val2 : Value for Field 2
 */
void thingspeak_send(int val1, int val2);

#endif /* INC_THINGSPEAK_H_ */
