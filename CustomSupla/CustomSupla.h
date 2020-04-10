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

#ifndef CUSTOMSUPLA_H
#define CUSTOMSUPLA_H

#include "proto.h"
#include <IPAddress.h>

#define ACTIVITY_TIMEOUT 30

#define STATUS_ALREADY_INITIALIZED     2
#define STATUS_CB_NOT_ASSIGNED         3
#define STATUS_INVALID_GUID            4
#define STATUS_UNKNOWN_SERVER_ADDRESS  5
#define STATUS_UNKNOWN_LOCATION_ID     6
#define STATUS_INITIALIZED             7
#define STATUS_CHANNEL_LIMIT_EXCEEDED  8
#define STATUS_DISCONNECTED            9
#define STATUS_REGISTER_IN_PROGRESS    10
#define STATUS_ITERATE_FAIL            11
#define STATUS_PROTOCOL_VERSION_ERROR  12
#define STATUS_BAD_CREDENTIALS         13
#define STATUS_TEMPORARILY_UNAVAILABLE 14
#define STATUS_LOCATION_CONFLICT       15
#define STATUS_CHANNEL_CONFLICT        16
#define STATUS_REGISTERED_AND_READY    17
#define STATUS_DEVICE_IS_DISABLED      18
#define STATUS_LOCATION_IS_DISABLED    19
#define STATUS_DEVICE_LIMIT_EXCEEDED   20

typedef _supla_int_t (*_cb_arduino_rw)(void *buf, _supla_int_t count);
typedef void (*_cb_arduino_eth_setup)(uint8_t mac[6], IPAddress *ip);
typedef bool (*_cb_arduino_connect)(const char *server, _supla_int_t port);
typedef bool (*_cb_arduino_connected)(void);
typedef void (*_cb_arduino_stop)(void);
typedef double (*_cb_arduino_get_double)(int channelNumber, double current_value);
typedef void (*_cb_arduino_get_temperature_and_humidity)(int channelNumber, double *temp, double *humidity);
typedef void (*_cb_arduino_get_rgbw_value)(int channelNumber, unsigned char *red, unsigned char *green, unsigned char *blue, unsigned char *color_brightness, unsigned char *brightness);
typedef void (*_cb_arduino_set_rgbw_value)(int channelNumber, unsigned char red, unsigned char green, unsigned char blue, unsigned char color_brightness, unsigned char brightness);
typedef int (*_impl_arduino_digitalRead)(int channelNumber, uint8_t pin);
typedef void (*_impl_arduino_digitalWrite)(int channelNumber, uint8_t pin, uint8_t val);
typedef void (*_impl_arduino_status)(int status, const char *msg);

typedef void (*_impl_rs_save_position)(int channelNumber, int position);
typedef void (*_impl_rs_load_position)(int channelNumber, int *position);
typedef void (*_impl_rs_save_settings)(int channelNumber, unsigned int full_opening_time, unsigned int full_closing_time);
typedef void (*_impl_rs_load_settings)(int channelNumber, unsigned int *full_opening_time, unsigned int *full_closing_time);

typedef void (*_impl_arduino_timer)(void);

typedef struct CustomSuplaCallbacks {
	
	_cb_arduino_rw tcp_read;
	_cb_arduino_rw tcp_write;
	_cb_arduino_eth_setup eth_setup;
	_cb_arduino_connected svr_connected;
	_cb_arduino_connect svr_connect;
	_cb_arduino_stop svr_disconnect;
	_cb_arduino_get_double get_temperature;
	_cb_arduino_get_double get_pressure;
	_cb_arduino_get_double get_weight;
	_cb_arduino_get_double get_wind;
	_cb_arduino_get_double get_rain;
	_cb_arduino_get_temperature_and_humidity get_temperature_and_humidity;
	_cb_arduino_get_rgbw_value get_rgbw_value;
	_cb_arduino_set_rgbw_value set_rgbw_value;
	_cb_arduino_get_double get_distance;

}CustomSuplaCallbacks;

typedef struct CustomSuplaParams {
	
	bool use_local_ip;
	IPAddress local_ip;
	
	CustomSuplaCallbacks cb;
	TDS_SuplaRegisterDevice_C reg_dev;
	uint8_t mac[6];
	
}CustomSuplaParams;

typedef struct SuplaChannelPin {
	int pin1;
	int pin2;
	bool hiIsLo;
	bool bistable;
	_supla_int_t DurationMS;
	
	unsigned long time_left;
	unsigned long bi_time_left;
	unsigned long vc_time;
	
	//union {
		uint8_t last_val;
		double last_val_dbl1;
		double last_val_dbl2;
	//};
}SuplaChannelPin;

typedef struct CustomSuplaRollerShutterTask {
    
    byte percent;
    byte direction;
    bool active;
    
}CustomSuplaRollerShutterTask;

typedef struct CustomSuplaRollerShutterCVR {
    
    byte active;
    byte value;
    unsigned long time;
    
}CustomSuplaRollerShutterCVR;

typedef struct {
    int pin;
    byte value;
    unsigned long time;
}CustomSuplaRollerShutterButton;

typedef struct CustomSuplaRollerShutter {
    
    CustomSuplaRollerShutterButton btnUp;
    CustomSuplaRollerShutterButton btnDown;
    
    int position;
    int last_position;
    int channel_number;
    unsigned int full_opening_time;
    unsigned int full_closing_time;
    
    unsigned long last_iterate_time;
    unsigned long tick_1s;
    unsigned long up_time;
    unsigned long down_time;
    
    unsigned long start_time;
    unsigned long stop_time;
    
    CustomSuplaRollerShutterCVR cvr1; // Change Value Request 1
    CustomSuplaRollerShutterCVR cvr2; 
    
    CustomSuplaRollerShutterTask task;
    byte save_position;
}CustomSuplaRollerShutter;


class CustomSuplaClass
{
protected:
	void *srpc;
	char registered;
	bool isInitialized(bool msg);
	void setString(char *dst, const char *src, int max_size);
	int addChannel(int pin1, int pin2, bool hiIsLo, bool bistable);
	void channelValueChanged(int channel_number, char v, double d, char var);
	void channelSetValue(int channel, char value, _supla_int_t DurationMS);
	void channelSetDoubleValue(int channelNum, double value);
	void setDoubleValue(char value[SUPLA_CHANNELVALUE_SIZE], double v);
	bool addDHT(int Type);
	void channelSetTempAndHumidityValue(int channelNum, double temp, double humidity);
	void setRGBWvalue(int channelNum, char value[SUPLA_CHANNELVALUE_SIZE]);
	void channelSetRGBWvalue(int channel, char value[SUPLA_CHANNELVALUE_SIZE]);

	CustomSuplaParams Params;
	_supla_int_t server_activity_timeout, last_response, last_sent;
	
    
    int rs_count;
    CustomSuplaRollerShutter *roller_shutter;
    
    CustomSuplaRollerShutter *rsByChannelNumber(int channel_number);

	unsigned long last_iterate_time;
    unsigned long wait_for_iterate;
	unsigned long last_ping_time;
    
	_impl_arduino_digitalRead impl_arduino_digitalRead;
	_impl_arduino_digitalWrite impl_arduino_digitalWrite;
    _impl_arduino_status impl_arduino_status;
    
    _impl_rs_save_position impl_rs_save_position;
    _impl_rs_load_position impl_rs_load_position;
    _impl_rs_save_settings impl_rs_save_settings;
    _impl_rs_load_settings impl_rs_load_settings;
    
    _impl_arduino_timer impl_arduino_timer;

    void rs_save_position(CustomSuplaRollerShutter *rs);
    void rs_load_position(CustomSuplaRollerShutter *rs);
    void rs_save_settings(CustomSuplaRollerShutter *rs);
    void rs_load_settings(CustomSuplaRollerShutter *rs);
    void rs_cvr_processing(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin, CustomSuplaRollerShutterCVR *cvr);
    void rs_set_relay(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin, byte value, bool cancel_task, bool stop_delay);
    void rs_set_relay(int channel_number, byte value);
    void rs_calibrate(CustomSuplaRollerShutter *rs, unsigned long full_time, unsigned long time, int dest_pos);
    void rs_move_position(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin, unsigned long full_time, unsigned long *time, bool up);
    bool rs_time_margin(unsigned long full_time, unsigned long time, byte m);
    void rs_task_processing(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin);
    void rs_add_task(CustomSuplaRollerShutter *rs, unsigned char percent);
    void rs_cancel_task(CustomSuplaRollerShutter *rs);
    bool rs_button_released(CustomSuplaRollerShutterButton *btn);
    void rs_buttons_processing(CustomSuplaRollerShutter *rs);
    
    void iterate_relay(SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel, unsigned long time_diff, int channel_idx);
    void iterate_sensor(SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel, unsigned long time_diff, int channel_idx);
    void iterate_thermometer(SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel, unsigned long time_diff, int channel_idx);
    void iterate_rollershutter(CustomSuplaRollerShutter *rs, SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel);
    
    void begin_thermometer(SuplaChannelPin *pin, TDS_CustomSuplaChannel_B *channel, int channel_number);
    
private:
	int suplaDigitalRead(int channelNumber, uint8_t pin);
    bool suplaDigitalRead_isHI(int channelNumber, uint8_t pin);
	void suplaDigitalWrite(int channelNumber, uint8_t pin, uint8_t val);
    void suplaDigitalWrite_setHI(int channelNumber, uint8_t pin, bool hi);
    void status(int status, const char *msg);
public:
   CustomSuplaClass();
   ~CustomSuplaClass();
   
   SuplaChannelPin *channel_pin;
   
   void channelRSValueChanged(int channel_number, char v, double d, char var);
   void channelValueChanged(int channel_number, char v);
   void channelDoubleValueChanged(int channel_number, double v);
    
   bool begin(IPAddress *local_ip, char GUID[SUPLA_GUID_SIZE], uint8_t mac[6], const char *Server,
		      int LocationID, const char *LocationPWD);
   
   bool begin(char GUID[SUPLA_GUID_SIZE], uint8_t mac[6], const char *Server,
		      int LocationID, const char *LocationPWD);
   
   void setName(const char *Name);
   
   int addRelay(int relayPin1, int relayPin2, bool hiIsLo, bool bistable, _supla_int_t functions);
   bool addRelay(int relayPin1, int relayPin2, bool hiIsLo);
   bool addRelay(int relayPin1, bool hiIsLo);
   bool addRelay(int relayPin1);
   bool addRollerShutterRelays(int relayPin1, int relayPin2, bool hiIsLo);
   bool addRollerShutterRelays(int relayPin1, int relayPin2);
   void setRollerShutterButtons(int channel_number, int btnUpPin, int btnDownPin);
   bool addSensorNO(int sensorPin, bool pullUp);
   bool addSensorNO(int sensorPin);
   bool addDS18B20Thermometer(void);
   bool addDHT11(void);
   bool addDHT22(void);
   bool addAM2302(void);
   bool addRgbControllerAndDimmer(void);
   bool addRgbController(void);
   bool addDimmer(void);
   bool addDistanceSensor(void);
   bool addPressureSensor(void);
   bool addWeightSensor(void);
   bool addWindSensor(void);
   bool addRainSensor(void);
    
   bool relayOn(int channel_number, _supla_int_t DurationMS);
   bool relayOff(int channel_number);
    
   void rollerShutterReveal(int channel_number);
   void rollerShutterShut(int channel_number);
   void rollerShutterStop(int channel_number);
   bool rollerShutterMotorIsOn(int channel_number);
   
   void onTimer(void);
   void iterate(void);
   void iterateOfline(void);
      
   void StopTimer();
   void StartTimer();
   void channelSetRGBWvalues(int channel,int mred,int mgreen,int mblue,int mcbr,int mbr);
   void RGBchannelValueChanged(int channel_number, int red, int green, int blue, int color_brightness, int brightness);
   
   CustomSuplaCallbacks getCallbacks(void);
   void setTemperatureCallback(_cb_arduino_get_double get_temperature);
   void setTemperatureHumidityCallback(_cb_arduino_get_temperature_and_humidity get_temperature_and_humidity);
   void setRGBWCallbacks(_cb_arduino_get_rgbw_value get_rgbw_value, _cb_arduino_set_rgbw_value set_rgbw_value);
   void setDistanceCallback(_cb_arduino_get_double get_distance);
   void setPressureCallback(_cb_arduino_get_double get_pressure);
   void setWeightCallback(_cb_arduino_get_double get_weight);
   void setWindCallback(_cb_arduino_get_double get_wind);
   void setRainCallback(_cb_arduino_get_double get_rain);
   void setRollerShutterFuncImpl(_impl_rs_save_position impl_save_position,
                                   _impl_rs_load_position impl_load_position,
                                   _impl_rs_save_settings impl_save_settings,
                                   _impl_rs_load_settings impl_load_settings);
   
   void setDigitalReadFuncImpl(_impl_arduino_digitalRead impl_arduino_digitalRead);
   void setDigitalWriteFuncImpl(_impl_arduino_digitalWrite impl_arduino_digitalWrite);
   void setStatusFuncImpl(_impl_arduino_status impl_arduino_status);
   void setTimerFuncImpl(_impl_arduino_timer impl_arduino_timer);
    
   void onSent(void);
   void onResponse(void);
   void onVersionError(TSDC_SuplaVersionError *version_error);
   void onRegisterResult(TSD_SuplaRegisterDeviceResult *register_device_result);
   void onSensorInterrupt(void);
   void channelSetValue(TSD_SuplaChannelNewValue *new_value);
   void channelSetActivityTimeoutResult(TSDC_SuplaSetActivityTimeoutResult *result);
    

};

#include "supla_main_helper.cpp_"			
			
extern CustomSuplaClass CustomSupla;
extern CustomSuplaCallbacks supla_arduino_get_callbacks(void);
#endif
