/*
Copyright (c) 2017 Theo Arends.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef USE_I2C_SLAVE
#ifdef USE_WION_POWER

byte wion_power_data[16],count;

uint16_t wion_readPower(void)
{

  count=0;

  // Request 16 bytes
  Wire.requestFrom(0, 16);

  while(count<16)
    if (Wire.available())
      wion_power_data[count++]=Wire.read();
    else
      yield(); 
  return count;
}


/*********************************************************************************************\
 * Presentation
\*********************************************************************************************/

void wion_mqttPresent(char* svalue, uint16_t ssvalue, uint8_t* djson)
{

  uint16_t l = wion_readPower();
  if (l != 16) {
    snprintf_P(svalue, ssvalue, PSTR("%s, \"wion\":{\"shortRead\":%d}"), svalue, l);
  } else {
    snprintf_P(svalue, ssvalue, PSTR("%s, \"wion\":{\"rawdata\":%X%X%X%X%X%X%X%X%X%X%X%X%X%X%X%X}"), svalue, wion_power_data[0], wion_power_data[1], wion_power_data[2], wion_power_data[3], wion_power_data[4], wion_power_data[5], wion_power_data[6], wion_power_data[7], wion_power_data[8], wion_power_data[9], wion_power_data[10], wion_power_data[11], wion_power_data[12], wion_power_data[13], wion_power_data[14], wion_power_data[15] );
  }
  *djson = 1;
#ifdef USE_DOMOTICZ
  domoticz_sensor5(l);
#endif  // USE_DOMOTICZ
}

#ifdef USE_WEBSERVER
String wion_webPresent()
{
  String page = "";
  // do not read from the sensor as it resets the counts
  // this means that no data will be shown unless telemetry is enabled
  //sprintf(page, "<tr><td> \"wion\":{\"rawdata\":%X%X%X%X%X%X%X%X%X%X%X%X%X%X%X%X}</td></tr>", wion_power_data[0], wion_power_data[1], wion_power_data[2], wion_power_data[3], wion_power_data[4], wion_power_data[5], wion_power_data[6], wion_power_data[7], wion_power_data[8], wion_power_data[9], wion_power_data[10], wion_power_data[11], wion_power_data[12], wion_power_data[13], wion_power_data[14], wion_power_data[15] );
  return page;
}
#endif  // USE_WEBSERVER
#endif  // USE_WION_POWER
#endif  // USE_I2C_SLAVE


