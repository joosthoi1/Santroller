#include <avr/io.h>
#include <avr/pgmspace.h>
#include <inttypes.h>
#include <stdint.h>

#include "Arduino.h"
#include "config.h"
#include "pins_define.h"
#include "util.h"

uint16_t adcReading[ADC_COUNT];
uint8_t analogPins[ADC_COUNT] = ADC_PINS;
bool first = true;
#if CONSOLE_TYPE == PC_XINPUT
int16_t adc(uint8_t analogIndex, int16_t offset, float multiplier, uint16_t deadzone) {
    int32_t val = (adcReading[analogIndex] - 512) * 64;
    val -= offset;
    val *= multiplier;
    if (val > INT16_MAX) val = INT16_MAX;
    if (val < INT16_MIN) val = INT16_MIN;
    if (val < deadzone && val > -deadzone) val = 0;
    return (int16_t)val >> 8;
}

uint16_t adc_trigger(uint8_t analogIndex, int16_t offset, float multiplier, uint16_t deadzone) {
    uint32_t val = adcReading[analogIndex] * 64;
    val -= offset;
    val *= multiplier;
    if (val > INT16_MAX) val = UINT16_MAX;
    if (val < INT16_MIN) val = 0;
    if (val < deadzone) val = 0;
    return (uint16_t)val >> 8;
}
#else
uint8_t adc(uint8_t analogIndex, int16_t offset, float multiplier, uint16_t deadzone) {
    deadzone >>= 8;
    offset >>= 8;
    int16_t val = (adcReading[analogIndex] - 512) >> 2;
    val -= offset;
    val *= multiplier;
    if (val > INT8_MAX) val = INT8_MAX;
    if (val < INT8_MIN) val = INT8_MIN;
    if (val < (uint8_t)deadzone && val > -(uint8_t)deadzone) val = 0;
    return ((int8_t)val) + INT8_MAX;
}

uint8_t adc_trigger(uint8_t analogIndex, int16_t offset, float multiplier, uint16_t deadzone) {
    deadzone >>= 8;
    offset >>= 8;
    uint32_t val = adcReading[analogIndex] >> 2;
    val -= offset;
    val *= multiplier;
    if (val > INT16_MAX) val = UINT16_MAX;
    if (val < INT16_MIN) val = 0;
    if (val < deadzone) val = 0;
    return (uint16_t)val >> 8;
}

#endif
int lastAnalogValue[NUM_ANALOG_INPUTS];
uint8_t lastDigitalValue[PORT_COUNT];
const uint16_t PROGMEM ports[PORT_COUNT] = PORTS;
const uint8_t PROGMEM skip_mask[PORT_COUNT] = SKIP_MASK_AVR;
const uint8_t PROGMEM analog_mask[PORT_COUNT] = ANALOG_MASK;
int currentAnalog = 0;
void tickAnalog(void) {
#if ADC_COUNT != 0
    if (!first) {
        if (bit_is_set(ADCSRA, ADSC)) return;
        uint8_t low, high;
        low = ADCL;
        high = ADCH;
        uint16_t data = (high << 8) | low;
        adcReading[currentAnalog] = data;
        currentAnalog++;
        if (currentAnalog == ADC_COUNT) {
            currentAnalog = 0;
        }
    }
    first = false;
    uint8_t pin = analogPins[currentAnalog];

#if defined(ADCSRB) && defined(MUX5)
    // the MUX5 bit of ADCSRB selects whether we're reading from channels
    // 0 to 7 (MUX5 low) or 8 to 15 (MUX5 high).
    ADCSRB = (ADCSRB & ~(1 << MUX5)) | (((pin >> 3) & 0x01) << MUX5);
#endif

    // set the analog reference (high two bits of ADMUX) and select the
    // channel (low 4 bits).  this also sets ADLAR (left-adjust result)
    // to 0 (the default).

    ADMUX = (1 << 6) | (pin & 0x07);

    sbi(ADCSRA, ADSC);
#endif
}

void stopReading(void) {
    while (bit_is_set(ADCSRA, ADSC))
        ;
    first = true;
}

void initPins(void) {
    PIN_INIT;
    tickAnalog();
}

void initDetectionDigital(void) {
    stopReading();
    for (int i = 0; i < PORT_COUNT; i++) {
        volatile uint8_t* port = ((volatile uint8_t*)(pgm_read_word(ports + i)));
        // ddr is one memory address above port
        volatile uint8_t* ddr = port - 1;
        uint8_t masked = (pgm_read_word(skip_mask + i));
        // Then write it to mask (zeros set input, and the mask already sets bits we aren't skipping to 0)
        *port |= masked;
        // And write it inverted to ddr (ones set pullup)
        *ddr &= ~masked;
        lastDigitalValue[i] = *port;
    }
}

void initDetectionAnalog(void) {
    stopReading();
    for (int i = 0; i < NUM_ANALOG_INPUTS; i++) {
        pinMode(i, INPUT_PULLUP);
        lastAnalogValue[i] = analogRead(i);
    }
}
int detectAnalog() {
    for (int i = 0; i < NUM_ANALOG_INPUTS; i++) {
        if (abs(analogRead(i) - lastAnalogValue[i]) > 30) {
            initPins();
            return i;
        }
    }
    initPins();
    return 0;
}

void detectDigital(uint8_t* mask, uint8_t* pin) {
    for (int i = 0; i < PORT_COUNT; i++) {
        volatile uint8_t* port = ((volatile uint8_t*)(pgm_read_word(ports + (i))));
        // Zero out any bits that are skipped
        uint8_t calc_mask = (*port ^ lastDigitalValue[i]) & ~pgm_read_word(skip_mask + i);
        if (calc_mask) {
            *pin = i;
            *mask = calc_mask;
            break;
        }
    }
    initPins();
}

void tickPins(void) {
    tickAnalog();
}