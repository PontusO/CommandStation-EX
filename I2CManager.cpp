/*
 *  © 2023, Neil McKechnie
 *  © 2022 Paul M Antoine
 *  All rights reserved.
 *
 *  This file is part of CommandStation-EX
 *
 *  This is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  It is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CommandStation.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdarg.h>
#include "I2CManager.h"
#include "DIAG.h"

// Include target-specific portions of I2CManager class
#if defined(I2C_USE_WIRE) 
#include "I2CManager_Wire.h"
#elif defined(ARDUINO_ARCH_AVR)
#include "I2CManager_NonBlocking.h"
#include "I2CManager_AVR.h"       // Uno/Nano/Mega2560
#elif defined(ARDUINO_ARCH_MEGAAVR) 
#include "I2CManager_NonBlocking.h"
#include "I2CManager_Mega4809.h"  // NanoEvery/UnoWifi
#elif defined(ARDUINO_ARCH_SAMD)
#include "I2CManager_NonBlocking.h"
#include "I2CManager_SAMD.h"      // SAMD21 for now... SAMD51 as well later
#else
#define I2C_USE_WIRE
#include "I2CManager_Wire.h"      // Other platforms
#endif


// If not already initialised, initialise I2C
void I2CManagerClass::begin(void) {
  if (!_beginCompleted) {
    _beginCompleted = true;
    _initialise();

    // Check for short-circuits on I2C
    if (!digitalRead(SDA))
      DIAG(F("WARNING: Possible short-circuit on I2C SDA line"));
    if (!digitalRead(SCL))
      DIAG(F("WARNING: Possible short-circuit on I2C SCL line"));

    // Probe and list devices.  Use standard mode 
    //  (clock speed 100kHz) for best device compatibility.
    _setClock(100000);
    unsigned long originalTimeout = timeout;
    setTimeout(1000);       // use 1ms timeout for probes
    bool found = false;
    for (byte addr=1; addr<127; addr++) {
      if (exists(addr)) {
        found = true; 
        DIAG(F("I2C Device found at x%x"), addr);
      }
    }
    if (!found) DIAG(F("No I2C Devices found"));
    _setClock(_clockSpeed);
    setTimeout(originalTimeout);      // set timeout back to original
  }
}

// Set clock speed to the lowest requested one. If none requested,
//  the Wire default is 100kHz.
void I2CManagerClass::setClock(uint32_t speed) {
  if (speed < _clockSpeed && !_clockSpeedFixed) {
    _clockSpeed = speed;
    DIAG(F("I2C clock speed set to %l Hz"), _clockSpeed);
  }
  _setClock(_clockSpeed);
}

// Force clock speed to that specified.
void I2CManagerClass::forceClock(uint32_t speed) {
  _clockSpeed = speed;
  _clockSpeedFixed = true;
  _setClock(_clockSpeed);
  DIAG(F("I2C clock speed forced to %l Hz"), _clockSpeed);
}

// Check if specified I2C address is responding (blocking operation)
// Returns I2C_STATUS_OK (0) if OK, or error code.
// Suppress retries.  If it doesn't respond first time it's out of the running.
uint8_t I2CManagerClass::checkAddress(uint8_t address) {
  I2CRB rb;
  rb.setWriteParams(address, NULL, 0);
  rb.suppressRetries(true);
  queueRequest(&rb);
  return rb.wait();
}


/***************************************************************************
 *  Write a transmission to I2C using a list of data (blocking operation)
 ***************************************************************************/
uint8_t I2CManagerClass::write(uint8_t address, uint8_t nBytes, ...) {
  uint8_t buffer[nBytes];
  va_list args;
  va_start(args, nBytes);
  for (uint8_t i=0; i<nBytes; i++)
    buffer[i] = va_arg(args, int);
  va_end(args);
  return write(address, buffer, nBytes);
}

/***************************************************************************
 *  Initiate a write to an I2C device (blocking operation)
 ***************************************************************************/
uint8_t I2CManagerClass::write(uint8_t i2cAddress, const uint8_t writeBuffer[], uint8_t writeLen) {
  I2CRB req;
  uint8_t status = write(i2cAddress, writeBuffer, writeLen, &req);
  return finishRB(&req, status);
}

/***************************************************************************
 *  Initiate a write from PROGMEM (flash) to an I2C device (blocking operation)
 ***************************************************************************/
uint8_t I2CManagerClass::write_P(uint8_t i2cAddress, const uint8_t * data, uint8_t dataLen) {
  I2CRB req;
  uint8_t status = write_P(i2cAddress, data, dataLen, &req);
  return finishRB(&req, status);
}

/***************************************************************************
 *  Initiate a write (optional) followed by a read from the I2C device (blocking operation)
 ***************************************************************************/
uint8_t I2CManagerClass::read(uint8_t i2cAddress, uint8_t *readBuffer, uint8_t readLen, 
    const uint8_t *writeBuffer, uint8_t writeLen)
{
  I2CRB req;
  uint8_t status = read(i2cAddress, readBuffer, readLen, writeBuffer, writeLen, &req);
  return finishRB(&req, status);
}

/***************************************************************************
 *  Overload of read() to allow command to be specified as a series of bytes (blocking operation)
 ***************************************************************************/
uint8_t I2CManagerClass::read(uint8_t address, uint8_t readBuffer[], uint8_t readSize, 
                                  uint8_t writeSize, ...) {
  va_list args;
  // Copy the series of bytes into an array.
  va_start(args, writeSize);
  uint8_t writeBuffer[writeSize];
  for (uint8_t i=0; i<writeSize; i++)
    writeBuffer[i] = va_arg(args, int);
  va_end(args);
  return read(address, readBuffer, readSize, writeBuffer, writeSize);
}

/***************************************************************************
 * Finish off request block by posting status, etc. (blocking operation)
 ***************************************************************************/
uint8_t I2CManagerClass::finishRB(I2CRB *rb, uint8_t status) {
  if ((status == I2C_STATUS_OK) && rb)
    status = rb->wait();
  return status;
}

/***************************************************************************
 * Get a message corresponding to the error status
 ***************************************************************************/
const FSH *I2CManagerClass::getErrorMessage(uint8_t status) {
  switch (status) {
    case I2C_STATUS_OK: return F("OK");
    case I2C_STATUS_TRUNCATED: return F("Transmission truncated");
    case I2C_STATUS_NEGATIVE_ACKNOWLEDGE: return F("No response from device (address NAK)");
    case I2C_STATUS_TRANSMIT_ERROR: return F("Transmit error (data NAK)");
    case I2C_STATUS_OTHER_TWI_ERROR: return F("Other Wire/TWI error");
    case I2C_STATUS_TIMEOUT: return F("Timeout");
    case I2C_STATUS_ARBITRATION_LOST: return F("Arbitration lost");
    case I2C_STATUS_BUS_ERROR: return F("I2C bus error");
    case I2C_STATUS_UNEXPECTED_ERROR: return F("Unexpected error");
    case I2C_STATUS_PENDING: return F("Request pending");
    default: return F("Error code not recognised");
  }
}

/***************************************************************************
 *  Declare singleton class instance.
 ***************************************************************************/
I2CManagerClass I2CManager = I2CManagerClass();

// Default timeout 100ms on I2C request block completion.
// A full 32-byte transmission takes about 8ms at 100kHz,
// so this value allows lots of headroom.  
// It can be modified by calling I2CManager.setTimeout() function.
// When retries are enabled, the timeout applies to each
// try, and failure from timeout does not get retried.
unsigned long I2CManagerClass::timeout = 100000UL;


/////////////////////////////////////////////////////////////////////////////
// Helper functions associated with I2C Request Block
/////////////////////////////////////////////////////////////////////////////

/***************************************************************************
 *  Block waiting for request to complete, and return completion status.
 *  Timeout monitoring is performed in the I2CManager.loop() function.
 ***************************************************************************/
uint8_t I2CRB::wait() {
  while (status==I2C_STATUS_PENDING) {
    I2CManager.loop();
  };
  return status;
}

/***************************************************************************
 *  Check whether request is still in progress.
 *  Timeout monitoring is performed in the I2CManager.loop() function.
 ***************************************************************************/
bool I2CRB::isBusy() {
  if (status==I2C_STATUS_PENDING) {
    I2CManager.loop();
    return true;
  } else
    return false;
}

/***************************************************************************
 *  Helper functions to fill the I2CRequest structure with parameters.
 ***************************************************************************/
void I2CRB::setReadParams(uint8_t i2cAddress, uint8_t *readBuffer, uint8_t readLen) {
  this->i2cAddress = i2cAddress;
  this->writeLen = 0;
  this->readBuffer = readBuffer;
  this->readLen = readLen;
  this->operation = OPERATION_READ;
  this->status = I2C_STATUS_OK;
}

void I2CRB::setRequestParams(uint8_t i2cAddress, uint8_t *readBuffer, uint8_t readLen, 
    const uint8_t *writeBuffer, uint8_t writeLen) {
  this->i2cAddress = i2cAddress;
  this->writeBuffer = writeBuffer;
  this->writeLen = writeLen;
  this->readBuffer = readBuffer;
  this->readLen = readLen;
  this->operation = OPERATION_REQUEST;
  this->status = I2C_STATUS_OK;
}

void I2CRB::setWriteParams(uint8_t i2cAddress, const uint8_t *writeBuffer, uint8_t writeLen) {
  this->i2cAddress = i2cAddress;
  this->writeBuffer = writeBuffer;
  this->writeLen = writeLen;
  this->readLen = 0;
  this->operation = OPERATION_SEND;
  this->status = I2C_STATUS_OK;
}

void I2CRB::suppressRetries(bool suppress) {
  if (suppress)
    this->operation |= OPERATION_NORETRY;
  else
    this->operation &= ~OPERATION_NORETRY;
}
