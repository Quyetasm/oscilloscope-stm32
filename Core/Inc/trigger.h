/**
 * @file    trigger.h
 * @brief   Software level trigger (rising/falling edge) over a sample buffer.
 */
#ifndef TRIGGER_H
#define TRIGGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pre-trigger margin: samples kept before the trigger point. */
#define TRIG_PRE_SAMPLES   10

/** Default AUTO-mode timeout in milliseconds. */
#define TRIG_AUTO_TIMEOUT_MS   100U

/** @brief Trigger acquisition modes. */
typedef enum {
    TRIG_AUTO,    /**< Always refresh, even without a trigger.   */
    TRIG_NORMAL,  /**< Wait for a trigger; otherwise hold frame. */
    TRIG_SINGLE   /**< Single capture, then stop.                */
} TriggerMode_t;

/** @brief Trigger edge selection. */
typedef enum {
    TRIG_RISING,
    TRIG_FALLING
} TriggerEdge_t;

/** @brief Trigger configuration and status. */
typedef struct {
    TriggerMode_t mode;
    TriggerEdge_t edge;
    uint16_t      level_raw;    /**< Level in ADC counts (0..4095). */
    float         level_volts;  /**< Level in volts.                */
    int32_t       position;     /**< Trigger point in the buffer.   */
    uint8_t       triggered;    /**< Set when a trigger was found.  */
    uint32_t      timeout_ms;   /**< AUTO-mode timeout.             */
} Trigger_t;

extern Trigger_t trigger;

/** @brief Initialise the trigger to sensible defaults. */
void Trigger_Init(void);

/**
 * @brief  Search a buffer for the first qualifying edge crossing.
 * @param  buf sample buffer (ADC counts).
 * @param  len number of samples in @p buf.
 * @return index of the trigger point, or -1 if none was found.
 *
 * Scans from index TRIG_PRE_SAMPLES so there is room to draw "before trigger".
 */
int32_t Trigger_Find(const uint16_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* TRIGGER_H */
