/*
 Copyright (C) AC SOFTWARE SP. Z O.O.
 
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
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#define CUSTOMSUPLA_CPP

#include <Arduino.h>
#include "IEEE754tools.h"
#include "CustomSupla.h"
#include "srpc.h"
#include "log.h"

#define RS_STOP_DELAY   500
#define RS_START_DELAY  1000

#define RS_RELAY_OFF   0
#define RS_RELAY_UP    2
#define RS_RELAY_DOWN  1

#define RS_DIRECTION_NONE   0
#define RS_DIRECTION_UP     2
#define RS_DIRECTION_DOWN   1

#ifdef ARDUINO_ARCH_ESP8266
ETSTimer esp_timer;

void esp_timer_cb(void *timer_arg) {
    CustomSupla.onTimer();
}
#else
ISR(TIMER1_COMPA_vect){
    CustomSupla.onTimer();
}
#endif

_supla_int_t supla_arduino_data_read(void *buf, _supla_int_t count, void *sdc) {
    return ((CustomSuplaClass*)sdc)->getCallbacks().tcp_read(buf, count);
}

_supla_int_t supla_arduino_data_write(void *buf, _supla_int_t count, void *sdc) {
    
    _supla_int_t r = ((CustomSuplaClass*)sdc)->getCallbacks().tcp_write(buf, count);
    if ( r > 0 ) {
        ((CustomSuplaClass*)sdc)->onSent();
    }
    return r;
}

void float2DoublePacked(float number, byte* bar, int byteOrder=LSBFIRST)  
{
    _FLOATCONV fl;
    fl.f = number;
    _DBLCONV dbl;
    dbl.p.s = fl.p.s;
    dbl.p.e = fl.p.e-127 +1023;  // exponent adjust
    dbl.p.m = fl.p.m;

#ifdef IEEE754_ENABLE_MSB
    if (byteOrder == LSBFIRST)
    {
#endif
        for (int i=0; i<8; i++)
        {
            bar[i] = dbl.b[i];
        }
#ifdef IEEE754_ENABLE_MSB
    }
    else
    {
        for (int i=0; i<8; i++)
        {
            bar[i] = dbl.b[7-i];
        }
    }
#endif
}

void CustomSuplaClass::status(int status, const char *msg) {
    
    if ( impl_arduino_status != NULL ) {
        impl_arduino_status(status, msg);
    } else {
        supla_log(LOG_DEBUG, "%s", msg);
    }
    
}

void supla_arduino_on_remote_call_received(void *_srpc, unsigned _supla_int_t rr_id, unsigned _supla_int_t call_type, void *_sdc, unsigned char proto_version) {

	TsrpcReceivedData rd;
	char result;

	((CustomSuplaClass*)_sdc)->onResponse();

	if ( SUPLA_RESULT_TRUE == ( result = srpc_getdata(_srpc, &rd, 0)) ) {
		
		switch(rd.call_type) {
		case SUPLA_SDC_CALL_VERSIONERROR:
			((CustomSuplaClass*)_sdc)->onVersionError(rd.data.sdc_version_error);
			break;
		case SUPLA_SD_CALL_REGISTER_DEVICE_RESULT:
			((CustomSuplaClass*)_sdc)->onRegisterResult(rd.data.sd_register_device_result);
			break;
		case SUPLA_SD_CALL_CHANNEL_SET_VALUE:
			((CustomSuplaClass*)_sdc)->channelSetValue(rd.data.sd_channel_new_value);
			break;
		case SUPLA_SDC_CALL_SET_ACTIVITY_TIMEOUT_RESULT:
			((CustomSuplaClass*)_sdc)->channelSetActivityTimeoutResult(rd.data.sdc_set_activity_timeout_result);
			break;
		}

		srpc_rd_free(&rd);

	} else if ( result == SUPLA_RESULT_DATA_ERROR ) {

        supla_log(LOG_DEBUG, "DATA ERROR!");
	}
	
}


CustomSuplaClass::CustomSuplaClass() {

	char a;
	srpc = NULL;
	registered = 0;
	last_iterate_time = 0;
    wait_for_iterate = 0;
	channel_pin = NULL;
    roller_shutter = NULL;
    rs_count = 0;
	
	impl_arduino_digitalRead = NULL;
	impl_arduino_digitalWrite = NULL;
    
    impl_rs_save_position = NULL;
    impl_rs_load_position = NULL;
    impl_rs_save_settings = NULL;
    impl_rs_load_settings = NULL;
    
    impl_arduino_timer = NULL;
	
	memset(&Params, 0, sizeof(CustomSuplaParams));
	
	for(a=0;a<6;a++)
		Params.mac[a] = a;
	
	Params.cb = supla_arduino_get_callbacks();
}

CustomSuplaClass::~CustomSuplaClass() {
	if ( channel_pin != NULL ) {
		free(channel_pin);
		channel_pin = NULL;
	}
    
    if ( roller_shutter != NULL ) {
        free(roller_shutter);
        roller_shutter = NULL;
    }
    
    rs_count = 0;
	
}

int CustomSuplaClass::suplaDigitalRead(int channelNumber, uint8_t pin) {
	
	if ( impl_arduino_digitalRead != NULL )
		return impl_arduino_digitalRead(channelNumber, pin);
	
	return digitalRead(pin);
}

bool CustomSuplaClass::suplaDigitalRead_isHI(int channelNumber, uint8_t pin) {
    
    return suplaDigitalRead(channelNumber, pin) == ( channel_pin[channelNumber].hiIsLo ? LOW : HIGH );
}

void CustomSuplaClass::suplaDigitalWrite(int channelNumber, uint8_t pin, uint8_t val) {
	
	if ( impl_arduino_digitalWrite != NULL )
		 return impl_arduino_digitalWrite(channelNumber, pin, val);

	
	digitalWrite(pin, val);
	
}

void CustomSuplaClass::suplaDigitalWrite_setHI(int channelNumber, uint8_t pin, bool hi) {
    
    if ( channel_pin[channelNumber].hiIsLo ) {
        hi = hi ? LOW : HIGH;
    }
    
    suplaDigitalWrite(channelNumber, pin, hi);
}

void CustomSuplaClass::setDigitalReadFuncImpl(_impl_arduino_digitalRead impl_arduino_digitalRead) {
	
	this->impl_arduino_digitalRead = impl_arduino_digitalRead;
	
}

void CustomSuplaClass::setDigitalWriteFuncImpl(_impl_arduino_digitalWrite impl_arduino_digitalWrite) {
	
	this->impl_arduino_digitalWrite = impl_arduino_digitalWrite;	
	
}

void CustomSuplaClass::setStatusFuncImpl(_impl_arduino_status impl_arduino_status) {
    
    this->impl_arduino_status = impl_arduino_status;
}

void CustomSuplaClass::setTimerFuncImpl(_impl_arduino_timer impl_arduino_timer) {
    
    this->impl_arduino_timer = impl_arduino_timer;
}

bool CustomSuplaClass::isInitialized(bool msg) {
	if ( srpc != NULL ) {
		
		if ( msg )
            status(STATUS_ALREADY_INITIALIZED, "CustomSupla is already initialized");
		
		return true;
	}
	
	return false;
}

bool CustomSuplaClass::begin(IPAddress *local_ip, char GUID[SUPLA_GUID_SIZE], uint8_t mac[6], const char *Server,
	                         int LocationID, const char *LocationPWD) {

	unsigned char a;
	if ( isInitialized(true) ) return false;
	
	if ( Params.cb.tcp_read == NULL
	     || Params.cb.tcp_write == NULL
	     || Params.cb.eth_setup == NULL
	     || Params.cb.svr_connected == NULL
	     || Params.cb.svr_connect == NULL
	     || Params.cb.svr_disconnect == NULL ) {
		
        status(STATUS_CB_NOT_ASSIGNED, "Callbacks not assigned!");
		return false;
	}
	
	if ( local_ip ) {
		Params.local_ip = *local_ip;
		Params.use_local_ip = true;
	} else {
		Params.use_local_ip = false;
	}
	
	memcpy(Params.reg_dev.GUID, GUID, SUPLA_GUID_SIZE);
	memcpy(Params.mac, mac, 6);
	Params.reg_dev.LocationID = LocationID;
	setString(Params.reg_dev.LocationPWD, LocationPWD, SUPLA_LOCATION_PWD_MAXSIZE);
    setString(Params.reg_dev.ServerName, Server, SUPLA_SERVER_NAME_MAXSIZE);
	
	for(a=0;a<SUPLA_GUID_SIZE;a++)
		if ( Params.reg_dev.GUID[a] != 0 ) break;
	
	if ( a == SUPLA_GUID_SIZE ) {
		status(STATUS_INVALID_GUID, "Invalid GUID");
		return false;
	}
	
	if ( Params.reg_dev.ServerName[0] == NULL ) {
		status(STATUS_UNKNOWN_SERVER_ADDRESS, "Unknown server address");
		return false;
	}
	
	if ( Params.reg_dev.LocationID == 0 ) {
		status(STATUS_UNKNOWN_LOCATION_ID, "Unknown LocationID");
		return false;
	}
	
    if ( strnlen(Params.reg_dev.Name, SUPLA_DEVICE_NAME_MAXSIZE) == 0 ) {
        setString(Params.reg_dev.Name, "ARDUINO", SUPLA_DEVICE_NAME_MAXSIZE);
    }
	
	setString(Params.reg_dev.SoftVer, "1.6.2C", SUPLA_SOFTVER_MAXSIZE);
	
	Params.cb.eth_setup(Params.mac, Params.use_local_ip ? &Params.local_ip : NULL);

	TsrpcParams srpc_params;
	srpc_params_init(&srpc_params);
	srpc_params.data_read = &supla_arduino_data_read;
	srpc_params.data_write = &supla_arduino_data_write;
	srpc_params.on_remote_call_received = &supla_arduino_on_remote_call_received;
	srpc_params.user_params = this;
	
	srpc = srpc_init(&srpc_params);
	
    if ( rs_count > 0 || impl_arduino_timer ) {
        
        for(int a=0;a<rs_count;a++) {
            rs_load_settings(&roller_shutter[a]);
            rs_load_position(&roller_shutter[a]);
            
            Params.reg_dev.channels[roller_shutter[a].channel_number].value[0] = (roller_shutter[a].position-100)/100;
        }
        
        #ifdef ARDUINO_ARCH_ESP8266
                os_timer_disarm(&esp_timer);
                os_timer_setfn(&esp_timer, (os_timer_func_t *)esp_timer_cb, NULL);
                os_timer_arm(&esp_timer, 10, 1);
        #else
                cli(); // disable interrupts
                TCCR1A = 0;// set entire TCCR1A register to 0
                TCCR1B = 0;// same for TCCR1B
                TCNT1  = 0;//initialize counter value to 0
                // set compare match register for 1hz increments
                OCR1A = 155;// (16*10^6) / (100*1024) - 1 (must be <65536) == 155.25
                // turn on CTC mode
                TCCR1B |= (1 << WGM12);
                // Set CS12 and CS10 bits for 1024 prescaler
                TCCR1B |= (1 << CS12) | (1 << CS10);
                // enable timer compare interrupt
                TIMSK1 |= (1 << OCIE1A);
                sei(); // enable interrupts
        #endif
    }
    
    for(a=0;a<Params.reg_dev.channel_count;a++) {
        begin_thermometer(&channel_pin[a], &Params.reg_dev.channels[a], a);
    }
    
    status(STATUS_INITIALIZED, "CustomSupla initialized");
    return true;
}

bool CustomSuplaClass::begin(char GUID[SUPLA_GUID_SIZE], uint8_t mac[6], const char *Server,
	                         int LocationID, const char *LocationPWD) {
	
	return begin(NULL, GUID, mac, Server, LocationID, LocationPWD);
}

void CustomSuplaClass::begin_thermometer(SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel, int channel_number) {
    
    if ( channel->Type == SUPLA_CHANNELTYPE_THERMOMETERDS18B20
        && Params.cb.get_temperature != NULL ) {
        
        pin->last_val_dbl1 = Params.cb.get_temperature(channel_number, pin->last_val_dbl1);
        channelSetDoubleValue(channel_number, pin->last_val_dbl1);

    } else if ( channel->Type == SUPLA_CHANNELTYPE_PRESSURESENSOR && Params.cb.get_pressure != NULL ){

        pin->last_val_dbl1 = Params.cb.get_pressure(channel_number, pin->last_val_dbl1);
        channelSetDoubleValue(channel_number, pin->last_val_dbl1);

	} else if ( channel->Type == SUPLA_CHANNELTYPE_WEIGHTSENSOR && Params.cb.get_weight != NULL ){

        pin->last_val_dbl1 = Params.cb.get_weight(channel_number, pin->last_val_dbl1);
        channelSetDoubleValue(channel_number, pin->last_val_dbl1);
		
	} else if ( channel->Type == SUPLA_CHANNELTYPE_WINDSENSOR && Params.cb.get_wind != NULL ){

        pin->last_val_dbl1 = Params.cb.get_wind(channel_number, pin->last_val_dbl1);
        channelSetDoubleValue(channel_number, pin->last_val_dbl1);
	
	} else if ( channel->Type == SUPLA_CHANNELTYPE_RAINSENSOR && Params.cb.get_rain != NULL ){

        pin->last_val_dbl1 = Params.cb.get_rain(channel_number, pin->last_val_dbl1);
        channelSetDoubleValue(channel_number, pin->last_val_dbl1);
			
    } else if ( ( channel->Type == SUPLA_CHANNELTYPE_DHT11
                 || channel->Type == SUPLA_CHANNELTYPE_DHT22
                 || channel->Type == SUPLA_CHANNELTYPE_AM2302 )
               && Params.cb.get_temperature_and_humidity != NULL ) {

        Params.cb.get_temperature_and_humidity(channel_number, &pin->last_val_dbl1, &pin->last_val_dbl2);
        channelSetTempAndHumidityValue(channel_number, pin->last_val_dbl1, pin->last_val_dbl2);
    }
    
};

void CustomSuplaClass::setName(const char *Name) {
	
	if ( isInitialized(true) ) return;
	setString(Params.reg_dev.Name, Name, SUPLA_DEVICE_NAME_MAXSIZE);
}

int CustomSuplaClass::addChannel(int pin1, int pin2, bool hiIsLo, bool bistable) {
	if ( isInitialized(true) ) return -1;
	
	if ( Params.reg_dev.channel_count >= SUPLA_CHANNELMAXCOUNT ) {
		status(STATUS_CHANNEL_LIMIT_EXCEEDED, "Channel limit exceeded");
		return -1;
	}

    
	if ( bistable && ( pin1 == 0 || pin2 == 0 ) )
		bistable = false;
	
    // !!! Channel number is always equal to channel array idx Params.reg_dev.channels[idx]
	Params.reg_dev.channels[Params.reg_dev.channel_count].Number = Params.reg_dev.channel_count;
	channel_pin = (SuplaChannelPin*)realloc(channel_pin, sizeof(SuplaChannelPin)*(Params.reg_dev.channel_count+1));
	channel_pin[Params.reg_dev.channel_count].pin1 = pin1; 
	channel_pin[Params.reg_dev.channel_count].pin2 = pin2; 
	channel_pin[Params.reg_dev.channel_count].hiIsLo = hiIsLo;
	channel_pin[Params.reg_dev.channel_count].bistable = bistable;
	channel_pin[Params.reg_dev.channel_count].time_left = 0;
	channel_pin[Params.reg_dev.channel_count].vc_time = 0;
	channel_pin[Params.reg_dev.channel_count].bi_time_left = 0;
	channel_pin[Params.reg_dev.channel_count].last_val = suplaDigitalRead(Params.reg_dev.channel_count, bistable ? pin2 : pin1);
	
	Params.reg_dev.channel_count++;
	
	return Params.reg_dev.channel_count-1;
}

int CustomSuplaClass::addRelay(int relayPin1, int relayPin2, bool hiIsLo, bool bistable, _supla_int_t functions) {
	
	int c = addChannel(relayPin1, relayPin2, hiIsLo, bistable);
	if ( c == -1 ) return -1;
	
	uint8_t _HI = hiIsLo ? LOW : HIGH;
	uint8_t _LO = hiIsLo ? HIGH : LOW;
	
	Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_RELAY;
	Params.reg_dev.channels[c].FuncList = functions;
	
	if ( relayPin1 != 0 ) {
		pinMode(relayPin1, OUTPUT); 
		suplaDigitalWrite(Params.reg_dev.channels[c].Number, relayPin1, hiIsLo ? HIGH : LOW); 
		
		if ( bistable == false )
			Params.reg_dev.channels[c].value[0] = suplaDigitalRead(Params.reg_dev.channels[c].Number, relayPin1) == _HI ? 1 : 0;
	}

	if ( relayPin2 != 0 )
	  if ( bistable ) {
		  
		  pinMode(relayPin2, INPUT); 
		  Params.reg_dev.channels[c].value[0] = suplaDigitalRead(Params.reg_dev.channels[c].Number, relayPin2) == HIGH ? 1 : 0;
		  
	  } else {
		  pinMode(relayPin2, OUTPUT); 
		  suplaDigitalWrite(Params.reg_dev.channels[c].Number, relayPin2, hiIsLo ? HIGH : LOW); 
			
		  if ( Params.reg_dev.channels[c].value[0] == 0
				&& suplaDigitalRead(Params.reg_dev.channels[c].Number, relayPin2) == _HI )
			Params.reg_dev.channels[c].value[0] = 2;
	}

	
	return c;
}

bool CustomSuplaClass::addRelay(int relayPin, bool hiIsLo) {
	return addRelay(relayPin, 0, hiIsLo, false, SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEGATEWAYLOCK
                              | SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEGATE
                              | SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEGARAGEDOOR
                              | SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEDOORLOCK
                              | SUPLA_BIT_RELAYFUNC_POWERSWITCH
                              | SUPLA_BIT_RELAYFUNC_LIGHTSWITCH
                              | SUPLA_BIT_RELAYFUNC_STAIRCASETIMER ) > -1;
}

bool CustomSuplaClass::addRelay(int relayPin1) {
	return addRelay(relayPin1, false) > -1;
}

bool CustomSuplaClass::addRollerShutterRelays(int relayPin1, int relayPin2, bool hiIsLo) {
	int channel_number = addRelay(relayPin1, relayPin2, hiIsLo, false, SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEROLLERSHUTTER);
    
    if ( channel_number > -1 ) {
        
        roller_shutter = (CustomSuplaRollerShutter*)realloc(roller_shutter, sizeof(CustomSuplaRollerShutter)*(rs_count+1));
        memset(&roller_shutter[rs_count], 0, sizeof(CustomSuplaRollerShutter));
        
        roller_shutter[rs_count].channel_number = channel_number;
        roller_shutter[rs_count].position = 0;
        Params.reg_dev.channels[channel_number].value[0] = -1;
        
        rs_count++;
    
        return true;
    }
    
    return false;
}

bool CustomSuplaClass::addRollerShutterRelays(int relayPin1, int relayPin2) {
	return addRollerShutterRelays(relayPin1, relayPin2, false);
}

void CustomSuplaClass::setRollerShutterButtons(int channel_number, int btnUpPin, int btnDownPin) {
    CustomSuplaRollerShutter *rs = rsByChannelNumber(channel_number);
    if ( rs ) {
        if ( btnUpPin > 0 ) {
             pinMode(btnUpPin, INPUT_PULLUP);
        }
        rs->btnUp.pin = btnUpPin;
        rs->btnUp.value = 1;
        
        if ( btnDownPin > 0 ) {
            pinMode(btnDownPin, INPUT_PULLUP);
        }
        rs->btnDown.pin = btnDownPin;
        rs->btnDown.value = 1;
    }
}

bool CustomSuplaClass::addSensorNO(int sensorPin, bool pullUp) {
	
	int c = addChannel(sensorPin, 0, false, false);
	if ( c == -1 ) return false; 
	
	Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_SENSORNO;
	pinMode(sensorPin, INPUT); 
	suplaDigitalWrite(Params.reg_dev.channels[c].Number, sensorPin, pullUp ? HIGH : LOW);
	
	Params.reg_dev.channels[c].value[0] = suplaDigitalRead(Params.reg_dev.channels[c].Number, sensorPin) == HIGH ? 1 : 0;
	return true;
}

void CustomSuplaClass::setDoubleValue(char value[SUPLA_CHANNELVALUE_SIZE], double v) {
	
	if ( sizeof(double) == 8 ) {
		memcpy(value, &v, 8);
	} else if ( sizeof(double) == 4 ) {
		float2DoublePacked(v, (byte*)value);
	}

}

void CustomSuplaClass::channelSetDoubleValue(int channelNum, double value) {
	setDoubleValue(Params.reg_dev.channels[channelNum].value, value);
}

void CustomSuplaClass::channelSetTempAndHumidityValue(int channelNum, double temp, double humidity) {
	
	long t = temp*1000.00;
	long h = humidity*1000.00;
	
	memcpy(Params.reg_dev.channels[channelNum].value, &t, 4);
	memcpy(&Params.reg_dev.channels[channelNum].value[4], &h, 4);
}

void CustomSuplaClass::setRGBWvalue(int channelNum, char value[SUPLA_CHANNELVALUE_SIZE]) {
	
	if ( Params.cb.get_rgbw_value ) {
		
		unsigned char red = 0;
		unsigned char green = 0xFF;
		unsigned char blue = 0;
		unsigned char color_brightness = 0;
		unsigned char brightness = 0;
		
		Params.cb.get_rgbw_value(Params.reg_dev.channels[channelNum].Number, &red, &green, &blue, &color_brightness, &brightness);
		
		value[0] = brightness;
		value[1] = color_brightness;
		
		value[2] = blue;
		value[3] = green;
		value[4] = red;
	}
	
}

bool CustomSuplaClass::addDS18B20Thermometer(void) {
	
	int c = addChannel(0, 0, false, false);
	if ( c == -1 ) return false; 
	
	Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_THERMOMETERDS18B20;
	channel_pin[c].last_val_dbl1 = -275;
    
	channelSetDoubleValue(c, channel_pin[c].last_val_dbl1);

}

bool CustomSuplaClass::addDHT(int Type) {
	
	int c = addChannel(0, 0, false, false);
	if ( c == -1 ) return false; 
	
	Params.reg_dev.channels[c].Type = Type;
	channel_pin[c].last_val_dbl1 = -275;	
	channel_pin[c].last_val_dbl2 = -1;	
	
	channelSetTempAndHumidityValue(c, channel_pin[c].last_val_dbl1, channel_pin[c].last_val_dbl2);
	
}

bool CustomSuplaClass::addDHT11(void) {
	return addDHT(SUPLA_CHANNELTYPE_DHT11);
}

bool CustomSuplaClass::addDHT22(void) {
	return addDHT(SUPLA_CHANNELTYPE_DHT22);
}

bool CustomSuplaClass::addAM2302(void) {
	return addDHT(SUPLA_CHANNELTYPE_AM2302);
}

bool CustomSuplaClass::addRgbControllerAndDimmer(void) {
	
	int c = addChannel(0, 0, false, false);
	if ( c == -1 ) return false; 
	
	Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_DIMMERANDRGBLED;
	setRGBWvalue(c, Params.reg_dev.channels[c].value);
}

bool CustomSuplaClass::addRgbController(void) {
	
	int c = addChannel(0, 0, false, false);
	if ( c == -1 ) return false; 
	
	Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_RGBLEDCONTROLLER;
	setRGBWvalue(c, Params.reg_dev.channels[c].value);
}

bool CustomSuplaClass::addDimmer(void) {
	
	int c = addChannel(0, 0, false, false);
	if ( c == -1 ) return false; 
	
	Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_DIMMER;
	setRGBWvalue(c, Params.reg_dev.channels[c].value);
}

bool CustomSuplaClass::addSensorNO(int sensorPin) {
	return addSensorNO(sensorPin, false);
}

bool CustomSuplaClass::addDistanceSensor(void) {

    int c = addChannel(0, 0, false, false);
    if ( c == -1 ) return false;
    
    Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_DISTANCESENSOR;
    channel_pin[c].last_val_dbl1 = -1;
    channelSetDoubleValue(c, channel_pin[c].last_val_dbl1);
    
}

bool CustomSuplaClass::addPressureSensor(void) {
    
    int c = addChannel(0, 0, false, false);
    if ( c == -1 ) return false; 
	
    Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_PRESSURESENSOR;
    channel_pin[c].last_val_dbl1 = -1;
    channelSetDoubleValue(c, channel_pin[c].last_val_dbl1);
    
}

bool CustomSuplaClass::addWeightSensor(void) {
    
    int c = addChannel(0, 0, false, false);
    if ( c == -1 ) return false; 
	
    Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_WEIGHTSENSOR;
    channel_pin[c].last_val_dbl1 = -1;
    channelSetDoubleValue(c, channel_pin[c].last_val_dbl1);
    
}

bool CustomSuplaClass::addWindSensor(void) {
    
    int c = addChannel(0, 0, false, false);
    if ( c == -1 ) return false; 
	
    Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_WINDSENSOR;
    channel_pin[c].last_val_dbl1 = -1;
    channelSetDoubleValue(c, channel_pin[c].last_val_dbl1);
    
}

bool CustomSuplaClass::addRainSensor(void) {
    
    int c = addChannel(0, 0, false, false);
    if ( c == -1 ) return false; 
	
    Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_RAINSENSOR;
    channel_pin[c].last_val_dbl1 = -1;
    channelSetDoubleValue(c, channel_pin[c].last_val_dbl1);
    
}

CustomSuplaCallbacks CustomSuplaClass::getCallbacks(void) {
	return Params.cb;
}


void CustomSuplaClass::setString(char *dst, const char *src, int max_size) {
	
	if ( src == NULL ) {
		dst[0] = 0;
		return;
	}
	
	int size = strlen(src);
	
	if ( size+1 > max_size )
		size = max_size-1;
	
	memcpy(dst, src, size);
}

void CustomSuplaClass::setTemperatureCallback(_cb_arduino_get_double get_temperature) {
	 Params.cb.get_temperature = get_temperature;
}

void CustomSuplaClass::setPressureCallback(_cb_arduino_get_double get_pressure) {
    Params.cb.get_pressure = get_pressure;
}

void CustomSuplaClass::setWeightCallback(_cb_arduino_get_double get_weight) {
    Params.cb.get_weight = get_weight;
}

void CustomSuplaClass::setWindCallback(_cb_arduino_get_double get_wind) {
    Params.cb.get_wind = get_wind;
}

void CustomSuplaClass::setRainCallback(_cb_arduino_get_double get_rain) {
    Params.cb.get_rain = get_rain;
}

void CustomSuplaClass::setTemperatureHumidityCallback(_cb_arduino_get_temperature_and_humidity get_temperature_and_humidity) {
	Params.cb.get_temperature_and_humidity = get_temperature_and_humidity;
}

void CustomSuplaClass::setRGBWCallbacks(_cb_arduino_get_rgbw_value get_rgbw_value, _cb_arduino_set_rgbw_value set_rgbw_value) {
	Params.cb.get_rgbw_value = get_rgbw_value;
	Params.cb.set_rgbw_value = set_rgbw_value;
}

void CustomSuplaClass::setDistanceCallback(_cb_arduino_get_double get_distance) {
    Params.cb.get_distance = get_distance;
}

void CustomSuplaClass::setRollerShutterFuncImpl(_impl_rs_save_position impl_rs_save_position,
                                                 _impl_rs_load_position impl_rs_load_position,
                                                 _impl_rs_save_settings impl_rs_save_settings,
                                                 _impl_rs_load_settings impl_rs_load_settings) {
    
    this->impl_rs_save_position = impl_rs_save_position;
    this->impl_rs_load_position = impl_rs_load_position;
    this->impl_rs_save_settings = impl_rs_save_settings;
    this->impl_rs_load_settings = impl_rs_load_settings;
    
    
}

void CustomSuplaClass::iterate_relay(SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel, unsigned long time_diff, int channel_number) {
    
    if ( pin->bi_time_left != 0 ) {
        if ( time_diff >= pin->bi_time_left ) {
            
            suplaDigitalWrite(channel->Number, pin->pin1, pin->hiIsLo ? HIGH : LOW);
            pin->bi_time_left = 0;
            
        } else if ( pin->bi_time_left > 0 ) {
            pin->bi_time_left-=time_diff;
        }
    }
    
    if ( pin->time_left != 0 ) {
        if ( time_diff >= pin->time_left ) {
            
            pin->time_left = 0;
            
            if ( channel->Type == SUPLA_CHANNELTYPE_RELAY )
                channelSetValue(channel_number, 0, 0);
            
        } else if ( pin->time_left > 0 ) {
            pin->time_left-=time_diff;
        }
    }
    
    if ( channel->Type == SUPLA_CHANNELTYPE_RELAY
        && pin->bistable    ) {
        
        if ( time_diff >= pin->vc_time ) {
            
            pin->vc_time-=time_diff;
            
        } else {
            
            uint8_t val = suplaDigitalRead(channel->Number, pin->pin2);
            
            if ( val != pin->last_val ) {
                
                pin->last_val = val;
                pin->vc_time = 200;
                
                channelValueChanged(channel->Number, val == HIGH ? 1 : 0);
                
            }
        }
        
    }

    
}

void CustomSuplaClass::iterate_sensor(SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel, unsigned long time_diff, int channel_number) {
    
    if ( channel->Type == SUPLA_CHANNELTYPE_SENSORNO ) {
        
        uint8_t val = suplaDigitalRead(channel->Number, pin->pin1);
        
        if ( val != pin->last_val ) {
            
            pin->last_val = val;
            Params.reg_dev.channels[channel->Number].value[0] = val;
            
            if ( pin->time_left <= 0 ) {
                pin->time_left = 100;
                channelValueChanged(channel->Number, val == HIGH ? 1 : 0);
            }
            
        }
        
    } else if ( channel->Type == SUPLA_CHANNELTYPE_DISTANCESENSOR
        && Params.cb.get_distance != NULL ) {
        
        if ( pin->time_left <= 0 ) {
            
            if ( pin->time_left <= 0 ) {
                
                pin->time_left = 1000;
                
                double val = Params.cb.get_distance(channel_number, pin->last_val_dbl1);
                
                if ( val != pin->last_val_dbl1 ) {
                    
                    pin->last_val_dbl1 = val;
                    channelDoubleValueChanged(channel_number, val);
                }
                
            }
            
        }
        
    }
    
};

void CustomSuplaClass::iterate_thermometer(SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel, unsigned long time_diff, int channel_number) {
    
    if ( channel->Type == SUPLA_CHANNELTYPE_THERMOMETERDS18B20
        && Params.cb.get_temperature != NULL ) {
        
        
        if ( pin->time_left <= 0 ) {
            
            pin->time_left = 10000;
            double val = Params.cb.get_temperature(channel_number, pin->last_val_dbl1);
            
            if ( val != pin->last_val_dbl1 ) {
                pin->last_val_dbl1 = val;
                channelDoubleValueChanged(channel_number, val);
            }
            
        }
    } else if (channel->Type == SUPLA_CHANNELTYPE_PRESSURESENSOR
            && Params.cb.get_pressure != NULL ){
		if ( pin->time_left <= 0 ) {

					pin->time_left = 10000;
					double val = Params.cb.get_pressure(channel_number, pin->last_val_dbl1);

					if ( val != pin->last_val_dbl1 ) {
						pin->last_val_dbl1 = val;
						channelDoubleValueChanged(channel_number, val);
					}
				}
				
	} else if (channel->Type == SUPLA_CHANNELTYPE_WEIGHTSENSOR
            && Params.cb.get_weight != NULL ){
		if ( pin->time_left <= 0 ) {

					pin->time_left = 10000;
					double val = Params.cb.get_weight(channel_number, pin->last_val_dbl1);

					if ( val != pin->last_val_dbl1 ) {
						pin->last_val_dbl1 = val;
						channelDoubleValueChanged(channel_number, val);
					}
				}

	} else if (channel->Type == SUPLA_CHANNELTYPE_WINDSENSOR
            && Params.cb.get_wind != NULL ){
		if ( pin->time_left <= 0 ) {

					pin->time_left = 10000;
					double val = Params.cb.get_wind(channel_number, pin->last_val_dbl1);

					if ( val != pin->last_val_dbl1 ) {
						pin->last_val_dbl1 = val;
						channelDoubleValueChanged(channel_number, val);
					}
				}
	
	} else if (channel->Type == SUPLA_CHANNELTYPE_RAINSENSOR
            && Params.cb.get_rain != NULL ){
		if ( pin->time_left <= 0 ) {

					pin->time_left = 10000;
					double val = Params.cb.get_rain(channel_number, pin->last_val_dbl1);

					if ( val != pin->last_val_dbl1 ) {
						pin->last_val_dbl1 = val;
						channelDoubleValueChanged(channel_number, val);
					}
				}
	
    } else if ( ( channel->Type == SUPLA_CHANNELTYPE_DHT11
                 || channel->Type == SUPLA_CHANNELTYPE_DHT22
                 || channel->Type == SUPLA_CHANNELTYPE_AM2302 )
               && Params.cb.get_temperature_and_humidity != NULL ) {
        
        
        if ( pin->time_left <= 0 ) {
            
            pin->time_left = 10000;
            
            double t = pin->last_val_dbl1;
            double h = pin->last_val_dbl2;
            
            Params.cb.get_temperature_and_humidity(channel_number, &t, &h);
            
            if ( t != pin->last_val_dbl1
                || h != pin->last_val_dbl2 ) {
                
                pin->last_val_dbl1 = t;
                pin->last_val_dbl2 = h;
                
                channelSetTempAndHumidityValue(channel_number, t, h);
                srpc_ds_async_channel_value_changed(srpc, channel_number, channel->value);
            }
            
        }
    }
    
};

void CustomSuplaClass::rs_save_position(CustomSuplaRollerShutter *rs) {
    if ( impl_rs_save_position ) {
        impl_rs_save_position(rs->channel_number, rs->position);
    }
}

void CustomSuplaClass::rs_load_position(CustomSuplaRollerShutter *rs) {
    if ( impl_rs_load_position ) {
        impl_rs_load_position(rs->channel_number, &rs->position);
    }
}

void CustomSuplaClass::rs_save_settings(CustomSuplaRollerShutter *rs) {
    if ( impl_rs_save_settings ) {
        impl_rs_save_settings(rs->channel_number, rs->full_opening_time, rs->full_closing_time);
    }
}

void CustomSuplaClass::rs_load_settings(CustomSuplaRollerShutter *rs) {
    if ( impl_rs_load_settings ) {
        impl_rs_load_settings(rs->channel_number, &rs->full_opening_time, &rs->full_closing_time);
    }
}

void CustomSuplaClass::rs_set_relay(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin, byte value, bool cancel_task, bool stop_delay) {
    
    if ( cancel_task ) {
        rs_cancel_task(rs);
    }
    
    unsigned long now = millis();
    
    if ( value == RS_RELAY_OFF )  {
        
        if ( rs->cvr1.active ) {
            return;
        }
        
        rs->cvr2.active = false;
        rs->cvr1.value = value;
        
        if ( now-rs->start_time >= RS_STOP_DELAY  ) {
           rs->cvr1.time = now;
        } else {
           rs->cvr1.time = now + RS_STOP_DELAY - (now-rs->start_time);
        }
 
        rs->cvr1.active = true;
        
    } else {
        
        if ( rs->cvr2.active ) {
            return;
        }
        
        rs->cvr1.active = false;
        rs->cvr2.value = value;
        
        int _pin = value == RS_RELAY_DOWN ? pin->pin2 : pin->pin1;
        
        if ( suplaDigitalRead_isHI(rs->channel_number, _pin) ) {
            rs_set_relay(rs, pin, RS_RELAY_OFF, false, stop_delay);
            rs->cvr2.time = rs->cvr1.time + RS_START_DELAY;
        } else {
            if ( now-rs->stop_time >= RS_START_DELAY  ) {
                rs->cvr2.time = now;
            } else {
                rs->cvr2.time = now + RS_START_DELAY - (now-rs->stop_time);
            }
            
        }
      
        rs->cvr2.active = true;
        

    }
    

}

void CustomSuplaClass::rs_set_relay(int channel_number, byte value) {
    CustomSuplaRollerShutter *rs = rsByChannelNumber(channel_number);
    
    if ( rs ) {
        rs_set_relay(rs, &channel_pin[channel_number], value, true, true);
    }
    
};

void CustomSuplaClass::rs_calibrate(CustomSuplaRollerShutter *rs, unsigned long full_time, unsigned long time, int dest_pos) {
    
    if ( full_time > 0
        && ( rs->position < 100 || rs->position > 10100 ) ) {
        
        full_time *= 1.1; // 10% margin
        
        if ( time >= full_time ) {
            rs->position = dest_pos;
            rs->save_position = 1;
        }
        
    }
    
}

void CustomSuplaClass::rs_move_position(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin, unsigned long full_time, unsigned long *time, bool up) {
    
    if ( rs->position < 100
        || rs->position > 10100
        || full_time == 0 )  {
        return;
    };
    
    int last_pos = rs->position;
    unsigned long p = (*time) * 100.00 / full_time * 100;
    unsigned long x = p * (full_time) / 10000;
    
    if ( p > 0 ) {

        if ( up ) {
            if ( int(rs->position - p) <= 100 ) {
               rs->position = 100;
            } else {
                rs->position -= p;
            }
        } else {
            
            if ( int(rs->position + p) >= 10100 ) {
                rs->position = 10100;
            } else {
                rs->position += p;
            }
        }
        
        if ( last_pos != rs->position ) {
            rs->save_position = 1;
        }
        
    }
    
    if ( (up && rs->position == 100) || (!up && rs->position == 10100) ) {
        
        if ( (*time) >= full_time * 1.1 ) {
           rs_set_relay(rs, pin, RS_RELAY_OFF, false, false);
        }
        
        return;
    }
    
    
    if ( x <= (*time) ) {
        (*time) -= x;
    } else {  // something goes wrong
        (*time) = 0;
    }
}

bool CustomSuplaClass::rs_time_margin(unsigned long full_time, unsigned long time, byte m) {
    
    return  (full_time > 0 && ( time * 100 / full_time ) < m ) ? true : false;
    
}

void CustomSuplaClass::rs_task_processing(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin) {
    
    if ( !rs->task.active ) {
        return;
    }
    
    if ( rs->position < 100
         || rs->position > 10100 ) {
        
        if ( !suplaDigitalRead_isHI(rs->channel_number, pin->pin1)
             && !suplaDigitalRead_isHI(rs->channel_number, pin->pin2)
             && rs->full_opening_time > 0
             && rs->full_closing_time > 0 ) {
            
            if ( rs->task.percent < 50 ) {
                rs_set_relay(rs, pin, RS_RELAY_UP, false, false);
            } else {
                rs_set_relay(rs, pin, RS_RELAY_DOWN, false, false);
            }
        }
        
        return;
    }
    
    byte percent = (rs->position-100)/100;
    
    if ( rs->task.direction == RS_DIRECTION_NONE ) {
        
        if ( percent > rs->task.percent ) {
            
            rs->task.direction = RS_DIRECTION_UP;
            rs_set_relay(rs, pin, RS_RELAY_UP, false, false);
            
        } else if ( percent < rs->task.percent ) {
            
            rs->task.direction = RS_DIRECTION_DOWN;
            rs_set_relay(rs, pin, RS_RELAY_DOWN, false, false);
            
        } else {
            
            rs->task.active = 0;
            rs_set_relay(rs, pin, RS_RELAY_OFF, false, false);
            
        }
        
    } else if ( ( rs->task.direction == RS_DIRECTION_UP
                  && percent <= rs->task.percent )
               || ( rs->task.direction == RS_DIRECTION_DOWN
                   && percent >= rs->task.percent )  ) {
                   
       if ( rs->task.percent == 0
           && rs_time_margin(rs->full_opening_time, rs->up_time, 5) ) { // margin 5%
           
           //supla_log(LOG_DEBUG, "UP MARGIN 5%");
           
       } else if ( rs->task.percent == 100
                  && rs_time_margin(rs->full_closing_time, rs->down_time, 5) ) {
           
           //supla_log(LOG_DEBUG, "DOWN MARGIN 5%");
           
       } else {
           
           rs->task.active = 0;
           rs_set_relay(rs, pin, RS_RELAY_OFF, false, false);
           
       }
                   
                   
    }
    
}

void CustomSuplaClass::rs_add_task(CustomSuplaRollerShutter *rs, unsigned char percent) {
    
    if ( (rs->position-100)/100 == percent )
        return;
    
    if ( percent > 100 )
        percent = 100;
    
    
    rs->task.percent = percent;
    rs->task.direction = RS_DIRECTION_NONE;
    rs->task.active = 1;
    
}

void CustomSuplaClass::rs_cancel_task(CustomSuplaRollerShutter *rs) {
    
    if ( rs == NULL )
        return;
    
    rs->task.active = 0;
    rs->task.percent = 0;
    rs->task.direction = RS_DIRECTION_NONE;

}

void CustomSuplaClass::rs_cvr_processing(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin, CustomSuplaRollerShutterCVR *cvr) {
    
    unsigned long now = millis();
    
    if ( cvr->active && cvr->time <= now ) {
        
        cvr->active = false;
        
        if (  cvr->value == RS_RELAY_UP ) {
            rs->start_time = now;
            suplaDigitalWrite_setHI(rs->channel_number, pin->pin1, false);
            suplaDigitalWrite_setHI(rs->channel_number, pin->pin2, true);
        } else if ( cvr->value == RS_RELAY_DOWN ) {
            rs->start_time = now;
            suplaDigitalWrite_setHI(rs->channel_number, pin->pin2, false);
            suplaDigitalWrite_setHI(rs->channel_number, pin->pin1, true);
        } else {
            rs->stop_time = now;
            suplaDigitalWrite_setHI(rs->channel_number, pin->pin1, false);
            suplaDigitalWrite_setHI(rs->channel_number, pin->pin2, false);
        }
        
    }
}

bool CustomSuplaClass::rs_button_released(CustomSuplaRollerShutterButton *btn) {
    
    if ( btn->pin > 0 ) {
        byte v = digitalRead(btn->pin);
        if ( v != btn->value && millis()-btn->time >= 50 ) {
            btn->value = v;
            return v == 1;
        }
    }
    
    return false;
}

void CustomSuplaClass::rs_buttons_processing(CustomSuplaRollerShutter *rs) {
    
    if ( rs_button_released(&rs->btnUp) ) {
       
        if ( CustomSupla.rollerShutterMotorIsOn(rs->channel_number) ) {
            CustomSupla.rollerShutterStop(rs->channel_number);
        } else {
            CustomSupla.rollerShutterReveal(rs->channel_number);
        }
        
    } else if ( rs_button_released(&rs->btnDown) ) {

        if ( CustomSupla.rollerShutterMotorIsOn(rs->channel_number) ) {
            CustomSupla.rollerShutterStop(rs->channel_number);
        } else {
            CustomSupla.rollerShutterShut(rs->channel_number);
        }
        ;
    }
}

void CustomSuplaClass::iterate_rollershutter(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel) {
    
    rs_cvr_processing(rs, pin, &rs->cvr1);
    rs_cvr_processing(rs, pin, &rs->cvr2);
    
    if ( rs->last_iterate_time == 0 ) {
        rs->last_iterate_time = millis();
        return;
    }
    
    unsigned long time_diff = millis() - rs->last_iterate_time;
    
    if ( suplaDigitalRead_isHI(rs->channel_number, pin->pin1) ) { // DOWN
        
        rs->up_time = 0;
        rs->down_time += time_diff;
        
        rs_calibrate(rs, rs->full_closing_time, rs->down_time, 1100);
        rs_move_position(rs, pin, rs->full_closing_time, &rs->down_time, false);
        
    } else if ( suplaDigitalRead_isHI(rs->channel_number, pin->pin2) ) { // UP

        rs->up_time += time_diff;
        rs->down_time = 0;
     
        rs_calibrate(rs, rs->full_opening_time, rs->up_time, 100);
        rs_move_position(rs, pin, rs->full_opening_time, &rs->up_time, true);
        
    } else {
        
        if ( rs->up_time != 0 ) {
            rs->up_time = 0;
        }
        
        if ( rs->down_time != 0 ) {
            rs->down_time = 0;
        }
        
    }
    
    rs_task_processing(rs, pin);
    
    if ( rs->last_iterate_time-rs->tick_1s >= 1000 ) { // 1000 == 1 sec.
        
    
        if ( rs->last_position != rs->position ) {
            rs->last_position = rs->position;
            channelValueChanged(rs->channel_number, (rs->position-100)/100, 0, 1);
        }
        
        if ( rs->up_time > 600000 || rs->down_time > 600000 ) { // 10 min. - timeout
             rs_set_relay(rs, pin, RS_RELAY_OFF, false, false);
        }
        
        if ( rs->save_position ) {
            rs->save_position = 0;
            rs_save_position(rs);
        }
  
        rs->tick_1s = millis();
        
    }
    
    rs->last_iterate_time = millis();
    rs_buttons_processing(rs);
}

void CustomSuplaClass::onTimer(void) {

    if ( impl_arduino_timer ) {
        impl_arduino_timer();
    }
    
    for(int a=0;a<rs_count;a++) {
        iterate_rollershutter(&roller_shutter[a], &channel_pin[roller_shutter[a].channel_number], &Params.reg_dev.channels[roller_shutter[a].channel_number]);
    }
    
}

void CustomSuplaClass::iterate(void) {
	
    int a;
    unsigned long _millis = millis();
    unsigned long time_diff = abs(_millis - last_iterate_time);
    
    if ( wait_for_iterate != 0
         && _millis < wait_for_iterate ) {
    
        return;
        
    } else {
        wait_for_iterate = 0;
    }
    
	if ( !isInitialized(false) ) return;
	
	if ( !Params.cb.svr_connected() ) {
		
		status(STATUS_DISCONNECTED, "Not connected");
	    registered = 0;
	    last_response = 0;
        last_sent = 0;
	    last_ping_time = 0;
        
		if ( !Params.cb.svr_connect(Params.reg_dev.ServerName, 2015) ) {
			
		    	supla_log(LOG_DEBUG, "Connection fail. Server: %s", Params.reg_dev.ServerName);
		    	Params.cb.svr_disconnect();

                wait_for_iterate = millis() + 2000;
				return;
		}

	}


	if ( registered == 0 ) {
		
		registered = -1;
		srpc_ds_async_registerdevice_c(srpc, &Params.reg_dev);
		status(STATUS_REGISTER_IN_PROGRESS, "Register in progress");
		
	} else if ( registered == 1 ) {
		// PING
		if ( (_millis-last_response)/1000 >= (server_activity_timeout+10)  ) {
			
			supla_log(LOG_DEBUG, "TIMEOUT");
			Params.cb.svr_disconnect();

		} else if ( _millis-last_ping_time >= 1000
				    && ( (_millis-last_response)/1000 >= (server_activity_timeout-5)
                         || (_millis-last_sent)/1000 >= (server_activity_timeout-5) ) ) {
            last_ping_time = _millis;
			srpc_dcs_async_ping_server(srpc);
		}
        
        if ( time_diff > 0 ) {
            
            for(a=0;a<Params.reg_dev.channel_count;a++) {
                
                iterate_relay(&channel_pin[a], &Params.reg_dev.channels[a], time_diff, a);
                iterate_sensor(&channel_pin[a], &Params.reg_dev.channels[a], time_diff, a);
                iterate_thermometer(&channel_pin[a], &Params.reg_dev.channels[a], time_diff, a);
                
            }
            
            last_iterate_time = millis();
        }
	}

	if( srpc_iterate(srpc) == SUPLA_RESULT_FALSE ) {
		status(STATUS_ITERATE_FAIL, "Iterate fail");
		Params.cb.svr_disconnect();
        
		wait_for_iterate = millis() + 5000;
        return;
	}
	
	
}

void CustomSuplaClass::iterateOfline(void) {
    int a;
    unsigned long _millis = millis();
    unsigned long time_diff = abs(_millis - last_iterate_time);

        if ( time_diff > 0 ) {
            
            for(a=0;a<Params.reg_dev.channel_count;a++) {
                
                iterate_relay(&channel_pin[a], &Params.reg_dev.channels[a], time_diff, a);
                iterate_sensor(&channel_pin[a], &Params.reg_dev.channels[a], time_diff, a);
                iterate_thermometer(&channel_pin[a], &Params.reg_dev.channels[a], time_diff, a);
                
            }
            
            last_iterate_time = millis();
        }
}

void CustomSuplaClass::onResponse(void) {
	last_response = millis();
}

void CustomSuplaClass::onSent(void) {
    last_sent = millis();
}

void CustomSuplaClass::onVersionError(TSDC_SuplaVersionError *version_error) {
	status(STATUS_PROTOCOL_VERSION_ERROR, "Protocol version error");
	Params.cb.svr_disconnect();
    
    wait_for_iterate = millis()+5000;
}

void CustomSuplaClass::onRegisterResult(TSD_SuplaRegisterDeviceResult *register_device_result) {

	switch(register_device_result->result_code) {
        case SUPLA_RESULTCODE_BAD_CREDENTIALS:
            status(STATUS_BAD_CREDENTIALS, "Bad credentials!");
            break;
            
        case SUPLA_RESULTCODE_TEMPORARILY_UNAVAILABLE:
            status(STATUS_TEMPORARILY_UNAVAILABLE, "Temporarily unavailable!");
            break;
            
        case SUPLA_RESULTCODE_LOCATION_CONFLICT:
            status(STATUS_LOCATION_CONFLICT, "Location conflict!");
            break;
            
        case SUPLA_RESULTCODE_CHANNEL_CONFLICT:
            status(STATUS_CHANNEL_CONFLICT, "Channel conflict!");
            break;
        case SUPLA_RESULTCODE_TRUE:
            
            server_activity_timeout = register_device_result->activity_timeout;
            registered = 1;
            
            status(STATUS_REGISTERED_AND_READY, "Registered and ready.");
            
            if ( server_activity_timeout != ACTIVITY_TIMEOUT ) {
                
                TDCS_SuplaSetActivityTimeout at;
                at.activity_timeout = ACTIVITY_TIMEOUT;
                srpc_dcs_async_set_activity_timeout(srpc, &at);
                
            }
            
            return;
            
        case SUPLA_RESULTCODE_DEVICE_DISABLED:
            status(STATUS_DEVICE_IS_DISABLED, "Device is disabled!");
            break;
            
        case SUPLA_RESULTCODE_LOCATION_DISABLED:
            status(STATUS_LOCATION_IS_DISABLED, "Location is disabled!");
            break;
            
        case SUPLA_RESULTCODE_DEVICE_LIMITEXCEEDED:
            status(STATUS_DEVICE_LIMIT_EXCEEDED, "Device limit exceeded!");
            break;
            
        case SUPLA_RESULTCODE_GUID_ERROR:
            status(STATUS_INVALID_GUID, "Incorrect device GUID!");
            break;
            
        case SUPLA_RESULTCODE_AUTHKEY_ERROR:
            status(STATUS_INVALID_GUID, "Incorrect AuthKey!");
            break;
            
        case SUPLA_RESULTCODE_REGISTRATION_DISABLED:
            status(STATUS_INVALID_GUID, "Registration disabled!");
            break;
            
        case SUPLA_RESULTCODE_NO_LOCATION_AVAILABLE:
            status(STATUS_INVALID_GUID, "No location available!");
            break;
            
        case SUPLA_RESULTCODE_USER_CONFLICT:
            status(STATUS_INVALID_GUID, "User conflict!");
            break;
            
        default:
            supla_log(LOG_ERR, "Register result code %i", register_device_result->result_code);
            break;
	}

	Params.cb.svr_disconnect();
    wait_for_iterate = millis() + 5000;
}

void CustomSuplaClass::channelValueChanged(int channel_number, char v, double d, char var) {

	if ( srpc != NULL
		 && registered == 1 ) {

		char value[SUPLA_CHANNELVALUE_SIZE];
		memset(value, 0, SUPLA_CHANNELVALUE_SIZE);
		
		if ( var == 1 )
			value[0] = v;
		else if ( var == 2 ) 
			setDoubleValue(value, d);
        
        memcpy(Params.reg_dev.channels[channel_number].value, value, 8);

		srpc_ds_async_channel_value_changed(srpc, channel_number, value);
	}

}
void CustomSuplaClass::channelRSValueChanged(int channel_number, char v, double d, char var) {

	if ( srpc != NULL
		 && registered == 1 ) {

		char value[SUPLA_CHANNELVALUE_SIZE];
		memset(value, 0, SUPLA_CHANNELVALUE_SIZE);
		
		if ( var == 1 )
			value[0] = v;
		else if ( var == 2 ) 
			setDoubleValue(value, d);
        
        memcpy(Params.reg_dev.channels[channel_number].value, value, 8);

		srpc_ds_async_channel_value_changed(srpc, channel_number, value);
	}

}

void CustomSuplaClass::channelDoubleValueChanged(int channel_number, double v) {
	channelValueChanged(channel_number, 0, v, 2);
	
}

void CustomSuplaClass::channelValueChanged(int channel_number, char v) {

	channelValueChanged(channel_number, v, 0, 1);

}

void CustomSuplaClass::channelSetValue(int channel, char value, _supla_int_t DurationMS) {
	
	bool success = false;
	
	uint8_t _HI = channel_pin[channel].hiIsLo ? LOW : HIGH;
	uint8_t _LO = channel_pin[channel].hiIsLo ? HIGH : LOW;

	if ( Params.reg_dev.channels[channel].Type == SUPLA_CHANNELTYPE_RELAY ) {
		
		if ( channel_pin[channel].bistable ) 
		   if ( channel_pin[channel].bi_time_left > 0
				 || suplaDigitalRead(Params.reg_dev.channels[channel].Number, channel_pin[channel].pin2)  == value ) {
			   value = -1;
		   } else {
			   value = 1;
			   channel_pin[channel].bi_time_left = 500;
		   }
		
		if ( value == 0 ) {
			
			if ( channel_pin[channel].pin1 != 0 ) {
				suplaDigitalWrite(Params.reg_dev.channels[channel].Number, channel_pin[channel].pin1, _LO); 
				
				success = suplaDigitalRead(Params.reg_dev.channels[channel].Number, channel_pin[channel].pin1) == _LO;
			}
				

			if ( channel_pin[channel].pin2 != 0 
					&& channel_pin[channel].bistable == false ) {
				suplaDigitalWrite(Params.reg_dev.channels[channel].Number, channel_pin[channel].pin2, _LO); 
				
				if ( !success )
					success = suplaDigitalRead(Params.reg_dev.channels[channel].Number, channel_pin[channel].pin2) == _LO;
			}
				
			
		} else if ( value == 1 ) {
			
			if ( channel_pin[channel].pin2 != 0
					&& channel_pin[channel].bistable == false ) {
				suplaDigitalWrite(Params.reg_dev.channels[channel].Number, channel_pin[channel].pin2, _LO); 
				delay(50);
			}
			
			if ( channel_pin[channel].pin1 != 0 ) {
				suplaDigitalWrite(Params.reg_dev.channels[channel].Number, channel_pin[channel].pin1, _HI); 
				
				if ( !success )
					success = suplaDigitalRead(Params.reg_dev.channels[channel].Number, channel_pin[channel].pin1) == _HI;
				
				if ( DurationMS > 0 ){
					channel_pin[channel].time_left = DurationMS;
					channel_pin[channel].DurationMS = DurationMS;
				} else {
		  			channel_pin[channel].DurationMS = 0;
				}
			}
			
		}
			
		if ( channel_pin[channel].bistable ) {
			success = false;
			delay(50);
		}
		
	};

	if ( success
			&& registered == 1 
			&& srpc ) {
		channelValueChanged(Params.reg_dev.channels[channel].Number, value);
	}

	
}

void CustomSuplaClass::channelSetRGBWvalue(int channel, char value[SUPLA_CHANNELVALUE_SIZE]) {
	
	unsigned char red = (unsigned char)value[4];
	unsigned char green = (unsigned char)value[3];
	unsigned char blue = (unsigned char)value[2];
	char color_brightness = (unsigned char)value[1];
	char brightness = (unsigned char)value[0];
	
	Params.cb.set_rgbw_value(Params.reg_dev.channels[channel].Number, red, green, blue, color_brightness, brightness);
	
	if ( srpc != NULL
		 && registered == 1 ) {

		char value[SUPLA_CHANNELVALUE_SIZE];
		memset(value, 0, SUPLA_CHANNELVALUE_SIZE);
		
		setRGBWvalue(channel, value);

		srpc_ds_async_channel_value_changed(srpc, Params.reg_dev.channels[channel].Number, value);
	}
	
}

CustomSuplaRollerShutter *CustomSuplaClass::rsByChannelNumber(int channel_number) {
    for(int a=0;a<rs_count;a++) {
        if ( roller_shutter[a].channel_number == channel_number ) {
            return &roller_shutter[a];
        }
    }
    
    return NULL;
}

void CustomSuplaClass::channelSetValue(TSD_SuplaChannelNewValue *new_value) {
    
	for(int a=0;a<Params.reg_dev.channel_count;a++) 
		if ( new_value->ChannelNumber == Params.reg_dev.channels[a].Number ) {
			
			if ( Params.reg_dev.channels[a].Type == SUPLA_CHANNELTYPE_RELAY ) {
				
                if ( Params.reg_dev.channels[a].FuncList == SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEROLLERSHUTTER ) {
                    
                    CustomSuplaRollerShutter *rs = rsByChannelNumber(new_value->ChannelNumber);
                    if ( rs != NULL ) {
                        
                        char v = new_value->value[0];
                        
                        unsigned long ct = new_value->DurationMS & 0xFFFF;
                        unsigned long ot = (new_value->DurationMS >> 16) & 0xFFFF;
                        
                        
                        if ( ct < 0 ) {
                            ct = 0;
                        }
                        
                        if ( ot < 0 ) {
                            ot = 0;
                        }
                        
                        ct*=100;
                        ot*=100;
                        
                        if ( ct != rs->full_closing_time
                             || ot != rs->full_opening_time ) {
                            
                            rs->full_closing_time = ct;
                            rs->full_opening_time = ot;
                            rs->position = -1;
                            
                            rs_save_settings(rs);
                            rs->save_position = 1;
                        }
                        
                        if ( v >= 10 && v <= 110 ) {
                            rs_add_task(rs, v-10);
                        } else {
                            
                            if ( v == 1 ) {
                                rs_set_relay(rs->channel_number, RS_RELAY_DOWN);
                            } else if ( v == 2 ) {
                                rs_set_relay(rs->channel_number, RS_RELAY_UP);
                            } else {
                                rs_set_relay(rs->channel_number, RS_RELAY_OFF);
                            }
                            
                        }
                        
                    }
                    
                } else {
                   channelSetValue(new_value->ChannelNumber, new_value->value[0], new_value->DurationMS);
                }
				
			} else if ( ( Params.reg_dev.channels[a].Type == SUPLA_CHANNELTYPE_DIMMER
						   || Params.reg_dev.channels[a].Type == SUPLA_CHANNELTYPE_RGBLEDCONTROLLER
						   || Params.reg_dev.channels[a].Type == SUPLA_CHANNELTYPE_DIMMERANDRGBLED )
							&& Params.cb.set_rgbw_value ) {
				
				channelSetRGBWvalue(a, new_value->value);
                
            };
			break;
		}

}

void CustomSuplaClass::channelSetActivityTimeoutResult(TSDC_SuplaSetActivityTimeoutResult *result) {
	server_activity_timeout = result->activity_timeout;
}

bool CustomSuplaClass::relayOn(int channel_number, _supla_int_t DurationMS) {
    channelSetValue(channel_number, HIGH, DurationMS);
}

bool CustomSuplaClass::relayOff(int channel_number) {
    channelSetValue(channel_number, LOW, 0);
}

void CustomSuplaClass::rollerShutterReveal(int channel_number) {
    rs_set_relay(channel_number, RS_RELAY_UP);
}

void CustomSuplaClass::rollerShutterShut(int channel_number) {
    rs_set_relay(channel_number, RS_RELAY_DOWN);
}

void CustomSuplaClass::rollerShutterStop(int channel_number) {
    rs_set_relay(channel_number, RS_RELAY_OFF);
}

bool CustomSuplaClass::rollerShutterMotorIsOn(int channel_number) {
    return channel_number < Params.reg_dev.channel_count
           && ( suplaDigitalRead_isHI(channel_number, channel_pin[channel_number].pin1)
                || suplaDigitalRead_isHI(channel_number, channel_pin[channel_number].pin2) );
}
void CustomSuplaClass::StopTimer(){
	os_timer_disarm(&esp_timer);
}
void CustomSuplaClass::StartTimer(){
	os_timer_setfn(&esp_timer, (os_timer_func_t *)esp_timer_cb, NULL);
            os_timer_arm(&esp_timer, 10, 1);
}
void CustomSuplaClass::channelSetRGBWvalues(int channel,int mred,int mgreen,int mblue,int mcbr,int mbr) {
	
	unsigned char red = mred;
	unsigned char green = mgreen;
	unsigned char blue = mblue;
	char color_brightness = mcbr;
	char brightness = mbr;
	
	Params.cb.set_rgbw_value(Params.reg_dev.channels[channel].Number, red, green, blue, color_brightness, brightness);
	
	if ( srpc != NULL
		 && registered == 1 ) {

		char value[SUPLA_CHANNELVALUE_SIZE];
		memset(value, 0, SUPLA_CHANNELVALUE_SIZE);
		
		setRGBWvalue(channel, value);

		srpc_ds_async_channel_value_changed(srpc, Params.reg_dev.channels[channel].Number, value);
	}
	
}
void CustomSuplaClass::RGBchannelValueChanged(int channel_number, int red, int green, int blue, int color_brightness, int brightness) {		
	if ( srpc != NULL
		 && registered == 1 ) {

		char value[SUPLA_CHANNELVALUE_SIZE];
		memset(value, 0, SUPLA_CHANNELVALUE_SIZE);
		
                value[0] = brightness;
		value[1] = color_brightness;
		
		value[2] = blue;
		value[3] = green;
		value[4] = red;

		srpc_ds_async_channel_value_changed(srpc, channel_number, value);
	}
	
}

CustomSuplaClass CustomSupla;
