/*
  Copyright (C) 2014 F1RMB, Daniel Caujolle-Bert <f1rmb.daniel@gmail.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <EEPROM.h>
#include "aPAfi.h"

#define D0      0x08
#define D1      0x04
#define D2      0x02
#define D3      0x01

static const int16_t    BUTTON_ADC_V                = 793;  // ADC value
static const int16_t    DEFAULT_ADC_TOLERANCE       = 10;   // +/- tolerance
static const uint16_t   DEFAULT_LONGPRESS_THRESHOLD = 2000; // ms
static const uint16_t   CRITICAL_TEMPERATURE        = 45;   // Temperature's Alarm Threshold

// EEPROM data offsets
static const int        EEPROM_ADDR_MAGIC           = 0; // 0xDEAD
static const int        EEPROM_ADDR_BAND            = 4;
static const int        EEPROM_ADDR_CATAUTO         = 5;

/*
    // FT-817ND Band->Volt table:
    // -------------------------
    //
    // Band     Volt
    // -------+------+
    //  160m    0.33
    //  80m     0.66
    //  40m     1.0
    //  30m     1.3
    //  20m     1.6
    //  17m     2.0
    //  15m     2.3
    //  12m     2.7
    //  10m     3.0
    //  6m      3.3
    //  2m†     3.7
    //  70cm†   4.0
    //
    // †: Unsupported
*/
/*
    // QRG    ADV(5V)   ADC(13.8V)   BAND
    // -----+---------+------------+-----
    // 1.8:    69       65           160m
    // 3.6:    147      140          80m
    // 7:      218      207          40m
    // 10:     284      269          30m
    // 14:     356      337          20m
    // 18:     434      411          17m
    // 21:     506      479          15m
    // 24:     566      536          12m
    // 28:     638      605          10m
    // 50:     718      680          6m
*/
/*
    LPF 4 bits encoding table
    -------------------------

       | 160   80   40   30   20   17   15   12   10   6    NA
    ---+------------------------------------------------------
    D0 |  L    H    L    H    L    H    L    H    L    H    H
    D1 |  L    L    H    H    L    L    H    H    L    L    H
    D2 |  L    L    L    L    H    H    H    H    L    L    H
    D3 |  L    L    L    L    L    L    L    L    H    H    H

*/
static const struct
{
    int16_t     ADCv;
    BAND_t      band;
    uint8_t     bits;
} bands[] PROGMEM =
{
    {   65, band160     , 0x0               },
    {  140, band80      , D0                },
    {  207, band40      , D1                },
    {  269, band30_20   , D0 | D1           },
    {  337, band30_20   , D2                },
    {  411, band17_15   , D0 | D2           },
    {  479, band17_15   , D1 | D2           },
    {  536, band12_10   , D0 | D1 | D2      },
    {  605, band12_10   , D3                },
    {  680, band6       , D0 | D3           },
    {    0, bandUnknown , D0 | D1 | D2 | D3 }
};


aPAfi::aPAfi() :
    m_Initialized(false),
    m_bandPins{6, 5, 4, 3, 2, 0, 1},
    m_dataPins{11, 10, 9, 8},
    m_txPin(12),
    m_buttonPin(A6),
    m_catPin(A7),
    m_catLEDPin(7),
    m_highTempPin(13),
    m_catMode(true),
    m_lastKeyTime(0), m_oldTime(0),
    m_refreshRate(100), m_repeat(false), m_pressTime(0), m_longPress(false),
    m_button(BUTTON_NO), m_oldButton(BUTTON_SELECT)
{
    // Init pins
    for (uint8_t i = 0; i < bandMAX; i++)
    {
        pinMode(m_bandPins[i], OUTPUT);
        digitalWrite(m_bandPins[i], LOW);
    }

    for (uint8_t i = 0; i < 4; i++)
    {
        pinMode(m_dataPins[i], OUTPUT);
        digitalWrite(m_dataPins[i], HIGH);
    }

    pinMode(m_txPin, INPUT);
    pinMode(m_catLEDPin, OUTPUT);
    pinMode(m_highTempPin, OUTPUT);

    m_Initialized = true;

    // Select button is pressed on startup: EEPROM RESET
    if (_getButtonFromAnalogValue((analogRead(m_buttonPin))) == BUTTON_SELECT)
    {
        _eepromReset();
        _eepromRestore();

        // Blink
        for (uint8_t i = 0; i < 10; i++)
        {
            digitalWrite(m_catLEDPin, !digitalRead(m_catLEDPin));
            digitalWrite(m_highTempPin, !digitalRead(m_highTempPin));
            delay(200);
        }
    }
    else
        _eepromRestore();

    // Ensure CAT connector is plugged
    if (m_catMode)
    {
        int     v = analogRead(m_catPin);
        uint8_t c = 0;

        delay(5);

        for (uint8_t i = 0; i < 5; i++)
        {
            int w = analogRead(m_catPin);

            if((w <= (v - DEFAULT_ADC_TOLERANCE)) || (w >= (v + DEFAULT_ADC_TOLERANCE)))
                c++;

            delay(5);
        }

        // ADC pin is floating, no cable plugged (wild guess)
        if (c > 3)
            m_catMode = false;

    }

    // Set default band
    if (!m_catMode)
        setBand(m_currentBand);
    else
    {
        BAND_t band = _getBandFromADCValue(analogRead(m_catPin));

        if ((band != bandUnknown) && (m_currentBand != band))
        {
            // Switch to band
            setBand(band);
        }
    }

    // Update CAT Automode PIN
    _updateCATStatus();
}

aPAfi::~aPAfi()
{
    //dtor
}

/////////////////////////////////
//
// Magic numbers stuff
//
bool aPAfi::_eepromCheckMagic()
{
    if ((EEPROM.read(EEPROM_ADDR_MAGIC) == 0xD) && (EEPROM.read(EEPROM_ADDR_MAGIC + 1) == 0xE)
        && (EEPROM.read(EEPROM_ADDR_MAGIC + 2) == 0xA) && (EEPROM.read(EEPROM_ADDR_MAGIC + 3) == 0xD))
            return true;

    return false;
}

void aPAfi::_eepromWriteMagic()
{
    // Magic numbers
    EEPROM.write(EEPROM_ADDR_MAGIC,     0xD);
    EEPROM.write(EEPROM_ADDR_MAGIC + 1, 0xE);
    EEPROM.write(EEPROM_ADDR_MAGIC + 2, 0xA);
    EEPROM.write(EEPROM_ADDR_MAGIC + 3, 0xD);
}

//
// Reset config into EEPROM
//
void aPAfi::_eepromReset()
{
    if (!_eepromCheckMagic())
        _eepromWriteMagic();

    EEPROM.write(EEPROM_ADDR_BAND, static_cast<uint8_t>(band160));
    EEPROM.write(EEPROM_ADDR_CATAUTO, m_catMode ? 1 : 0);
}

//
// Restore config from EEPROM
//
void aPAfi::_eepromRestore()
{
    if (!_eepromCheckMagic())
    {
        _eepromReset();
        return;
    }

    m_currentBand = static_cast<BAND_t>(EEPROM.read(EEPROM_ADDR_BAND));
    m_catMode     = EEPROM.read(EEPROM_ADDR_CATAUTO) == 1 ? true : false;
}

//
// Backup config into EEPROM
//
void aPAfi::_eepromBackup(uint8_t slot)
{
    if (!_eepromCheckMagic())
        _eepromWriteMagic();

    if (slot == 0 || slot == 1)
        EEPROM.write(EEPROM_ADDR_BAND, static_cast<uint8_t>(m_currentBand));

    if (slot == 0 || slot == 2)
        EEPROM.write(EEPROM_ADDR_CATAUTO, m_catMode ? 1 : 0);
}

uint8_t aPAfi::_getBitsFromBand(BAND_t band)
{
    for (size_t i = 0; static_cast<int16_t>(pgm_read_word(&bands[i].band)) != bandUnknown; i++)
    {
        if (static_cast<int16_t>(pgm_read_word(&bands[i].band)) == band)
            return (static_cast<uint8_t>(pgm_read_word(&bands[i].bits)));
    }

    return (0x0);
}

BAND_t aPAfi::_getBandFromADCValue(int16_t value)
{
    //Serial.print("CAT ADC: ");
    //Serial.println(value, DEC);
    for (size_t i = 0; static_cast<int16_t>(pgm_read_word(&bands[i].band)) != bandUnknown; i++)
    {
        if ((value > static_cast<int16_t>(pgm_read_word(&bands[i].ADCv) - DEFAULT_ADC_TOLERANCE)) && (value < static_cast<int16_t>(pgm_read_word(&bands[i].ADCv) + DEFAULT_ADC_TOLERANCE)))
            return (static_cast<BAND_t>(pgm_read_byte(&bands[i].band)));
    }

    return (bandUnknown);
}

BUTTON_t aPAfi::_getButtonFromAnalogValue(int16_t value)
{
    if ((value > (BUTTON_ADC_V - DEFAULT_ADC_TOLERANCE)) && (value < (BUTTON_ADC_V + DEFAULT_ADC_TOLERANCE)))
        return (BUTTON_SELECT);

    return (BUTTON_NO);
}

bool aPAfi::_isTempOkay()
{
    // Switch to 1.1V reference
    analogReference(INTERNAL);

    // Flush values
    for (uint8_t i = 0; i < 2; i++)
    {
        (void) analogRead(A5);
        delay(5);
    }

    int16_t tempA = analogRead(A5);
    uint16_t tC   = static_cast<uint16_t>(tempA / 9.31);

    // Switch back to default reference
    analogReference(DEFAULT);

    // Flush values
    for (uint8_t i = 0; i < 2; i++)
    {
        (void) analogRead(A5);
        delay(5);
    }

    bool safe = (tC < CRITICAL_TEMPERATURE);

    // Reflect temperature checking.
    digitalWrite(m_highTempPin, safe ? LOW : HIGH);

    return (safe);
}

void aPAfi::handleEvents()
{
    if (!m_Initialized)
        return;

    if ((!(millis() % 100)) && !_isTempOkay())
        return;

    if (m_catMode)
    {
        BAND_t band = _getBandFromADCValue(analogRead(m_catPin));

        if ((band != bandUnknown) && (m_currentBand != band))
        {
            // Switch to band
            setBand(band);
        }
    }

    if (m_lastKeyTime == 0)
        m_lastKeyTime = millis();

    if (millis() > (m_oldTime + m_refreshRate))
    {
        int16_t v = analogRead(m_buttonPin);
        m_button = _getButtonFromAnalogValue(v);

        m_oldTime       = millis();

        if (m_button == BUTTON_SELECT)
        {
            unsigned long pressTimeStart = 0, pressTimeEnd = 0;

            if (m_repeat)
                delay(m_refreshRate);
            else
            {
                pressTimeStart = millis();

                // Wait for key release, till DEFAULT_LONGPRESS_THRESHOLD max time.
                while (_getButtonFromAnalogValue(analogRead(m_buttonPin)) != BUTTON_NO)
                {
                    if ((millis() - pressTimeStart) > DEFAULT_LONGPRESS_THRESHOLD)
                        break;
                }

                pressTimeEnd = millis();
            }


            if (m_button == m_oldButton)
                return;

            m_longPress = m_repeat ? false : ((pressTimeEnd - pressTimeStart) > DEFAULT_LONGPRESS_THRESHOLD ? true : false);
            m_oldButton    = m_button;

            if (m_longPress)
            {
                m_catMode = !m_catMode;
                _updateCATStatus();
            }
            else
            {
                if (!m_catMode)
                    nextBand();
            }

            return;
        }
    }


    m_longPress = false;
    m_oldButton = BUTTON_NO;
}

void aPAfi::_updateCATStatus()
{
    digitalWrite(m_catLEDPin, m_catMode ? LOW : HIGH);

    _eepromBackup(2);
    // TEST digitalWrite(m_highTempPin, m_catMode ? LOW : HIGH);
}

void aPAfi::_updateBAND()
{
    for (uint8_t i = 0; i < bandMAX; i++)
    {
        if (i == m_currentBand)
        {
            if (digitalRead(m_bandPins[i]) != HIGH)
                digitalWrite(m_bandPins[i], HIGH);
        }
        else
        {
            if (digitalRead(m_bandPins[i]) != LOW)
                digitalWrite(m_bandPins[i], LOW);
        }
    }


    // Updata datapins
    uint8_t bits = _getBitsFromBand(m_currentBand);

    if (bits != 0xF)
    {
        digitalWrite(m_dataPins[0], (bits & D0) ? HIGH : LOW);
        digitalWrite(m_dataPins[1], (bits & D1) ? HIGH : LOW);
        digitalWrite(m_dataPins[2], (bits & D2) ? HIGH : LOW);
        digitalWrite(m_dataPins[3], (bits & D3) ? HIGH : LOW);
    }

    _eepromBackup();
}

bool aPAfi::isTXing()
{
    if (!m_Initialized)
        return (false);

    return (digitalRead(m_txPin) == HIGH);
}

bool aPAfi::nextBand()
{
    if (!m_Initialized)
        return (false);

    int16_t band = m_currentBand + 1;

    if (band >= bandMAX)
        band = band160;

    return (setBand((BAND_t)band));
}

bool aPAfi::setBand(BAND_t band)
{
    if (!m_Initialized || isTXing())
        return (false);

    switch (band)
    {
        case band160:
        case band80:
        case band40:
        case band30_20:
        case band17_15:
        case band12_10:
        case band6:
            m_currentBand = band;
            break;

        case bandUnknown:
        default:
            return (false);
    }

    _updateBAND();

    return (true);
}

BAND_t aPAfi::getBand()
{
    if (!m_Initialized)
        return (bandUnknown);

    return (m_currentBand);

}

bool aPAfi::isInitialized()
{
    return (m_Initialized);
}

void aPAfi::setAutoCATMode(bool value)
{
    if (!m_Initialized)
        return;

    m_catMode = value;
    _updateCATStatus();
}

bool aPAfi::getAutoCATMode()
{
    return (m_catMode);
}
