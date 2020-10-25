/*
 *  © 2020, Chris Harlow. All rights reserved.
 *  
 *  This file is part of Asbelos DCC API
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
/**********************************************************************

DCC++ BASE STATION supports Sensor inputs that can be connected to any Arduino Pin
not in use by this program.  Sensors can be of any type (infrared, magentic, mechanical...).
The only requirement is that when "activated" the Sensor must force the specified Arduino
Pin LOW (i.e. to ground), and when not activated, this Pin should remain HIGH (e.g. 5V),
or be allowed to float HIGH if use of the Arduino Pin's internal pull-up resistor is specified.

To ensure proper voltage levels, some part of the Sensor circuitry
MUST be tied back to the same ground as used by the Arduino.

The Sensor code below utilizes exponential smoothing to "de-bounce" spikes generated by
mechanical switches and transistors.  This avoids the need to create smoothing circuitry
for each sensor.  You may need to change these parameters through trial and error for your specific sensors.

To have this sketch monitor one or more Arduino pins for sensor triggers, first define/edit/delete
sensor definitions using the following variation of the "S" command:

  <S ID PIN PULLUP>:           creates a new sensor ID, with specified PIN and PULLUP
                               if sensor ID already exists, it is updated with specificed PIN and PULLUP
                               returns: <O> if successful and <X> if unsuccessful (e.g. out of memory)

  <S ID>:                      deletes definition of sensor ID
                               returns: <O> if successful and <X> if unsuccessful (e.g. ID does not exist)

  <S>:                         lists all defined sensors
                               returns: <Q ID PIN PULLUP> for each defined sensor or <X> if no sensors defined

where

  ID: the numeric ID (0-32767) of the sensor
  PIN: the arduino pin number the sensor is connected to
  PULLUP: 1=use internal pull-up resistor for PIN, 0=don't use internal pull-up resistor for PIN

Once all sensors have been properly defined, use the <E> command to store their definitions to EEPROM.
If you later make edits/additions/deletions to the sensor definitions, you must invoke the <E> command if you want those
new definitions updated in the EEPROM.  You can also clear everything stored in the EEPROM by invoking the <e> command.

All sensors defined as per above are repeatedly and sequentially checked within the main loop of this sketch.
If a Sensor Pin is found to have transitioned from one state to another, one of the following serial messages are generated:

  <Q ID>     - for transition of Sensor ID from HIGH state to LOW state (i.e. the sensor is triggered)
  <q ID>     - for transition of Sensor ID from LOW state to HIGH state (i.e. the sensor is no longer triggered)

Depending on whether the physical sensor is acting as an "event-trigger" or a "detection-sensor," you may
decide to ignore the <q ID> return and only react to <Q ID> triggers.

**********************************************************************/

#include "StringFormatter.h"
#include "Sensors.h"
#include "EEStore.h"


///////////////////////////////////////////////////////////////////////////////
//
// checks one defined sensors and prints _changed_ sensor state
// to stream unless stream is NULL in which case only internal
// state is updated. Then advances to next sensor which will
// be checked att next invocation.
//
///////////////////////////////////////////////////////////////////////////////

void Sensor::checkAll(Print *stream){

  if (firstSensor == NULL) return;
  if (readingSensor == NULL) readingSensor=firstSensor;
  readingSensor->signal=readingSensor->signal*(1.0-SENSOR_DECAY)+digitalRead(readingSensor->data.pin)*SENSOR_DECAY;
  
  if(!readingSensor->active && readingSensor->signal<0.5){
    readingSensor->active=true;
    if (stream != NULL) StringFormatter::send(stream, F("<Q %d>"), readingSensor->data.snum);
  } else if(readingSensor->active && readingSensor->signal>0.9){
    readingSensor->active=false;
    if (stream != NULL) StringFormatter::send(stream, F("<q %d>"), readingSensor->data.snum);
  }
  readingSensor=readingSensor->nextSensor;
} // Sensor::checkAll

///////////////////////////////////////////////////////////////////////////////
//
// prints all sensor states to stream
//
///////////////////////////////////////////////////////////////////////////////

void Sensor::printAll(Print *stream){

  for(Sensor * tt=firstSensor;tt!=NULL;tt=tt->nextSensor){
    if (stream != NULL)
      StringFormatter::send(stream, F("<%c %d>"), tt->active ? 'Q' : 'q', tt->data.snum);
  } // loop over all sensors
} // Sensor::printAll

///////////////////////////////////////////////////////////////////////////////

Sensor *Sensor::create(int snum, int pin, int pullUp){
  Sensor *tt;

  if(firstSensor==NULL){
    firstSensor=(Sensor *)calloc(1,sizeof(Sensor));
    tt=firstSensor;
  } else if((tt=get(snum))==NULL){
    tt=firstSensor;
    while(tt->nextSensor!=NULL)
      tt=tt->nextSensor;
    tt->nextSensor=(Sensor *)calloc(1,sizeof(Sensor));
    tt=tt->nextSensor;
  }

  if(tt==NULL) return tt;       // problem allocating memory

  tt->data.snum=snum;
  tt->data.pin=pin;
  tt->data.pullUp=(pullUp==0?LOW:HIGH);
  tt->active=false;
  tt->signal=1;
  pinMode(pin,INPUT);         // set mode to input
  digitalWrite(pin,pullUp);   // don't use Arduino's internal pull-up resistors for external infrared sensors --- each sensor must have its own 1K external pull-up resistor

  return tt;

}

///////////////////////////////////////////////////////////////////////////////

Sensor* Sensor::get(int n){
  Sensor *tt;
  for(tt=firstSensor;tt!=NULL && tt->data.snum!=n;tt=tt->nextSensor);
  return tt ;
}
///////////////////////////////////////////////////////////////////////////////

bool Sensor::remove(int n){
  Sensor *tt,*pp=NULL;

  for(tt=firstSensor;tt!=NULL && tt->data.snum!=n;pp=tt,tt=tt->nextSensor);

  if (tt==NULL)  return false;
  
  if(tt==firstSensor)
    firstSensor=tt->nextSensor;
  else
    pp->nextSensor=tt->nextSensor;

  if (readingSensor==tt) readingSensor=tt->nextSensor;
  free(tt);

  return true;
}

///////////////////////////////////////////////////////////////////////////////

void Sensor::load(){
  struct SensorData data;
  Sensor *tt;

  for(int i=0;i<EEStore::eeStore->data.nSensors;i++){
    EEPROM.get(EEStore::pointer(),data);
    tt=create(data.snum,data.pin,data.pullUp);
    EEStore::advance(sizeof(tt->data));
  }
}

///////////////////////////////////////////////////////////////////////////////

void Sensor::store(){
  Sensor *tt;

  tt=firstSensor;
  EEStore::eeStore->data.nSensors=0;

  while(tt!=NULL){
    EEPROM.put(EEStore::pointer(),tt->data);
    EEStore::advance(sizeof(tt->data));
    tt=tt->nextSensor;
    EEStore::eeStore->data.nSensors++;
  }
}

///////////////////////////////////////////////////////////////////////////////

Sensor *Sensor::firstSensor=NULL;
Sensor *Sensor::readingSensor=NULL;
