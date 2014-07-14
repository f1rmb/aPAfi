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
#ifndef APAFI_H
#define APAFI_H

#include <Arduino.h>

typedef enum
{
    bandUnknown = -1,
    band160 = 0,
    band80,
    band40,
    band30_20,
    band17_15,
    band12_10,
    band6,
    bandMAX
} BAND_t;

typedef enum
{
    BUTTON_NO,
    BUTTON_SELECT
} BUTTON_t;

class aPAfi
{
    public:
        aPAfi();
        ~aPAfi();

        bool                    isInitialized();

        void                    handleEvents();
        bool                    isTXing();

        bool                    setBand(BAND_t band);
        BAND_t                  getBand();
        bool                    nextBand();

        void                    setAutoCATMode(bool value);
        bool                    getAutoCATMode();

    private:
        bool                    _eepromCheckMagic();
        void                    _eepromWriteMagic();
        void                    _eepromReset();
        void                    _eepromRestore();
        void                    _eepromBackup(uint8_t slot = 0);
        uint8_t                 _getBitsFromBand(BAND_t band);
        BAND_t                  _getBandFromADCValue(int16_t value);
        BUTTON_t                _getButtonFromAnalogValue(int16_t value);
        void                    _updateCATStatus();
        void                    _updateBAND();
        bool                    _isTempOkay();

    private:
        bool                    m_Initialized;
        // Pins
        uint8_t                 m_bandPins[bandMAX];
        uint8_t                 m_dataPins[4];
        uint16_t                m_txPin;
        uint8_t                 m_buttonPin;
        uint8_t                 m_catPin;
        uint8_t                 m_catLEDPin;
        uint8_t                 m_highTempPin;

        BAND_t                  m_currentBand;
        bool                    m_catMode;

        // Button
        unsigned long           m_lastKeyTime;
        unsigned long           m_oldTime;
        unsigned int            m_refreshRate;

        // Support repeat (unused)
        bool                    m_repeat;

        // Long press feature
        unsigned long           m_pressTime;
        bool                    m_longPress;
        BUTTON_t                m_button;
        BUTTON_t                m_oldButton;
};

#endif // APAFI_H
