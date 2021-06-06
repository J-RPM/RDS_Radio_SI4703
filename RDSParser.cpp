///////////////////////////////////////////////////////////////////////////////////////////////////
///                        **** Modified by: J_RPM  http://j-rpm.com ****                       ///
///////////////////////////////////////////////////////////////////////////////////////////////////
/// \file RDSParser.cpp
/// \brief RDS Parser class implementation.
///
/// \author Matthias Hertel, http://www.mathertel.de
/// \copyright Copyright (c) 2014 by Matthias Hertel.\n
/// This work is licensed under a BSD style license.\n
/// See http://www.mathertel.de/License.aspx
///
/// \details
///
/// More documentation and source code is available at http://www.mathertel.de/Arduino
///
/// ChangeLog see RDSParser.h.

#include "RDSParser.h"

#define DEBUG_FUNC0(fn)          { Serial.print(fn); Serial.println(F("()")); }
uint8_t convertRDS(uint8_t txt);

/// Setup the RDS object and initialize private variables to 0.
RDSParser::RDSParser() {
  memset(this, 0, sizeof(RDSParser));
} // RDSParser()


void RDSParser::init() {
  strcpy(_PSName1, "........");
  strcpy(_PSName2, _PSName1);
  strcpy(programServiceName, "        ");
  strcpy(rdsTpTa, "--");
  memset(_RDSText, 0, sizeof(_RDSText));
  _lastTextIDX = 0;
} // init()

// Add by: J_RPM
void RDSParser::attachPiCallback(receivePiFunction newFunction)
{
  _sendPi = newFunction;
} // attachPiCallback

// Add by: J_RPM
void RDSParser::attachPtyCallback(receivePtyFunction newFunction)
{
  _sendPty = newFunction;
} // attachPtyCallback

// Add by: J_RPM
void RDSParser::attachTpTaCallback(receiveTpTaFunction newFunction)
{
  _sendTpTa = newFunction;
} // attachTpTaCallback

// Add by: J_RPM
void RDSParser::attachGroupCallback(receiveGroupFunction newFunction)
{
  _sendGroup = newFunction;
} // attachGroupCallback

// Add by: J_RPM
void RDSParser::attachVariantCallback(receiveVariantFunction newFunction)
{
  _sendVariant = newFunction;
} // attachVariantCallback

void RDSParser::attachServicenNameCallback(receiveServicenNameFunction newFunction)
{
  _sendServiceName = newFunction;
} // attachServicenNameCallback

void RDSParser::attachTextCallback(receiveTextFunction newFunction)
{
  _sendText = newFunction;
} // attachTextCallback

void RDSParser::attachTimeCallback(receiveTimeFunction newFunction)
{
  _sendTime = newFunction;
} // attachTimeCallback


void RDSParser::processData(uint16_t block1, uint16_t block2, uint16_t block3, uint16_t block4)
{
  // DEBUG_FUNC0("process");
  uint8_t  idx; // index of rdsText
  char c1, c2;
  char *p;

  uint16_t mins; ///< RDS time in minutes
  uint8_t off;   ///< RDS time offset and sign

   //Serial.print(F(">")); Serial.print(block1, HEX); Serial.print(F(" ")); Serial.print(block2, HEX); Serial.print(F(" ")); Serial.print(block3, HEX); Serial.print(F(" ")); Serial.println(block4, HEX);

  if (block1 == 0) {
    // reset all the RDS info.
    init();
    // Send out empty data
    if (_sendServiceName) _sendServiceName(programServiceName);
    if (_sendText)        _sendText("");
    
    // Add by: J_RPM
    if (_sendPi) _sendPi("");
    if (_sendGroup) _sendGroup("");
    if (_sendVariant) _sendVariant("");
    if (_sendPty) _sendPty("");
    if (_sendTpTa) _sendTpTa("--");
    return;
  } // if


  ////////// Add by: J_RPM /////////////
  rdsGroup = ((block2 & 0xF000) >> 12);
  if (_sendGroup) _sendGroup(rdsGroup);
  rdsVariant = 10 + ((block2 & 0x0800) >> 11);
  if (_sendVariant) _sendVariant(rdsVariant);
  //////////////////////////////////////


  // Analyzing Groups
  rdsGroupType = 0x0A | ((block2 & 0xF000) >> 8) | ((block2 & 0x0800) >> 11);


  switch (rdsGroupType) {
  case 0x0A:
  case 0x0B:
    ////// Add by: J_RPM ///////
    
    // PI //
    rdsPI=block1;
    if (_sendPi) _sendPi(rdsPI);

    // TP/TA //
    rdsTpTa[0] = char('0'+((block2 & 0x0400) >> 10));
    rdsTpTa[1] = char('0'+((block2 & 0x0010) >> 4));
    rdsTpTa[2] = '\0';
    if (_sendTpTa) _sendTpTa(rdsTpTa);
    
    // PTY //
    rdsPty = (block2 & 0x03E0) >> 5;
    if (_sendPty) _sendPty(rdsPty);
    
    ////////////////////////////

    // The data received is part of the Service Station Name 
    idx = 2 * (block2 & 0x0003);

    // new data is 2 chars from block 4
    c1 = block4 >> 8;
    c2 = block4 & 0x00FF;

    // check that the data was received successfully twice
    // before publishing the station name

    if ((_PSName1[idx] == c1) && (_PSName1[idx + 1] == c2)) {
      // retrieved the text a second time: store to _PSName2
      _PSName2[idx] = c1;
      _PSName2[idx + 1] = c2;
      _PSName2[8] = '\0';

      if ((idx == 6) && strcmp(_PSName1, _PSName2) == 0) {
        if (strcmp(_PSName2, programServiceName) != 0) {
          // publish station name
          strcpy(programServiceName, _PSName2);

          ////// Add by: J_RPM ///////
          bool outPS = true;
          String testPS = String(programServiceName);
          for (int i=0; i<=8; i=i+2){
            if (testPS.substring(i, i+2)== "..") outPS=false;
          }
          ////////////////////////////
          
          if (_sendServiceName && outPS)
            _sendServiceName(programServiceName);
        } // if
      } // if
    } // if

    if ((_PSName1[idx] != c1) || (_PSName1[idx + 1] != c2)) {
      _PSName1[idx] = c1;
      _PSName1[idx + 1] = c2;
      _PSName1[8] = '\0';
      // Serial.println(_PSName1);
    } // if
    break;

  case 0x2A:
    // The data received is part of the RDS Text.
    _textAB = (block2 & 0x0010);
    idx = 4 * (block2 & 0x000F);

    if (idx < _lastTextIDX) {
      // the existing text might be complete because the index is starting at the beginning again.
      // now send it to the possible listener.
      //Serial.print("RT -> "); Serial.println(_RDSText);
      if (_sendText)
        _sendText(_RDSText);
    }
    _lastTextIDX = idx;

    if (_textAB != _last_textAB) {
      // when this bit is toggled the whole buffer should be cleared.
      _last_textAB = _textAB;
      _lastTextIDX = 0;  // Add by: J_RPM
      memset(_RDSText, 0, sizeof(_RDSText));
    } // if

    // new data is 2 chars from block 3
    _RDSText[idx] = (convertRDS(block3 >> 8)); idx++;
    _RDSText[idx] = (convertRDS(block3 & 0x00FF)); idx++;

    // new data is 2 chars from block 4
    _RDSText[idx] = (convertRDS(block4 >> 8)); idx++;
    _RDSText[idx] = (convertRDS(block4 & 0x00FF)); idx++;

    break;

  case 0x4A:
    // Clock time and date
    off = (block4)& 0x3F; // 6 bits
    mins = (block4 >> 6) & 0x3F; // 6 bits
    mins += 60 * (((block3 & 0x0001) << 4) | ((block4 >> 12) & 0x0F));

    // adjust offset
    if (off & 0x20) {
      mins -= 30 * (off & 0x1F);
    } else {
      mins += 30 * (off & 0x1F);
    }

    if ((_sendTime) && (mins != _lastRDSMinutes)) {
      _lastRDSMinutes = mins;
      _sendTime(mins / 60, mins % 60);
    } // if
    break;

  case 0x6A:
    // IH
    break;

  case 0x8A:
    // TMC
    break;

  case 0xAA:
    // PTYN
    break;

  case 0xCA:
    // EWS
    break;

  case 0xEA:
    // EON
    break;

  default:
    break;
  }
} // processData()
/////////////////////////////
//  G0-RDS >>> CGRAM LCD   //
/////////////////////////////
uint8_t convertRDS(uint8_t txt){
  switch (txt) {
    case 0x80:
      return 0x08;  //á
    case 0x82:
      return 0x09;  //é
    case 0x84:
      return 0x0A;  //í
    case 0x86:
      return 0x0B;  //ó
    case 0x88:
      return 0x0C;  //ú
    case 0x8A:
      return 0x0D;  //Ñ >> ñ
    case 0x9A:
      return 0x0D;  //ñ
    case 0x8B:
      return 0x0E;  //Ç >> ç
    case 0x9B:
      return 0x0E;  //ç 
    case 0xD9:
      return 0x0F;  //Ü >> ü
    case 0x99:
      return 0x0F;  //ü
    case 0xC0:
      return 0x41;  //Á >> A
    case 0xC2:
      return 0x45;  //É >> E
    case 0xC4:
      return 0x49;  //Í >> I
    case 0xC6:
      return 0x4F;  //Ó >> O
    case 0xC8:
      return 0x55;  //Ú >> U
    default:
      return txt;
  }
}
///////////////////////////////////
// End.
