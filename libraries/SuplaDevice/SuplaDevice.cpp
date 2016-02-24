/*
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

 Author: Przemyslaw Zygmunt <przemek@supla.org>
*/

#define SUPLADEVICE_CPP

#include <Arduino.h>
#include "SuplaDevice.h"
#include "srpc.h"
#include "log.h"

_supla_int_t supla_arduino_data_read(void *buf, _supla_int_t count, void *sdc) {
	return ((SuplaDeviceClass*)sdc)->getCallbacks().tcp_read(buf, count);
}

_supla_int_t supla_arduino_data_write(void *buf, _supla_int_t count, void *sdc) {
	return ((SuplaDeviceClass*)sdc)->getCallbacks().tcp_write(buf, count);
}



void supla_arduino_on_remote_call_received(void *_srpc, unsigned _supla_int_t rr_id, unsigned _supla_int_t call_type, void *_sdc, unsigned char proto_version) {

	TsrpcReceivedData rd;
	char result;

	((SuplaDeviceClass*)_sdc)->onResponse();

	if ( SUPLA_RESULT_TRUE == ( result = srpc_getdata(_srpc, &rd, 0)) ) {
		
		switch(rd.call_type) {
		case SUPLA_SDC_CALL_VERSIONERROR:
			((SuplaDeviceClass*)_sdc)->onVersionError(rd.data.sdc_version_error);
			break;
		case SUPLA_SD_CALL_REGISTER_DEVICE_RESULT:
			((SuplaDeviceClass*)_sdc)->onRegisterResult(rd.data.sd_register_device_result);
			break;
		case SUPLA_SD_CALL_CHANNEL_SET_VALUE:
			((SuplaDeviceClass*)_sdc)->channelSetValue(rd.data.sd_channel_new_value);
			break;
		case SUPLA_SDC_CALL_SET_ACTIVITY_TIMEOUT_RESULT:
			((SuplaDeviceClass*)_sdc)->channelSetActivityTimeoutResult(rd.data.sdc_set_activity_timeout_result);
			break;
		}

		srpc_rd_free(&rd);

	} else if ( result == SUPLA_RESULT_DATA_ERROR ) {

		supla_log(LOG_DEBUG, "DATA ERROR!");
	}
	
}


SuplaDeviceClass::SuplaDeviceClass() {

	char a;
	srpc = NULL;
	registered = 0;
	last_iterate_time = 0;
	channel_pin = NULL;
	
	memset(&Params, 0, sizeof(SuplaDeviceParams));
	
	for(a=0;a<6;a++)
		Params.mac[a] = a;
	
	Params.cb = supla_arduino_get_callbacks();
}

SuplaDeviceClass::~SuplaDeviceClass() {
	if ( Params.server != NULL ) {
		free(Params.server);
		Params.server = NULL;
	}
	
	if ( channel_pin != NULL ) {
		free(channel_pin);
		channel_pin = NULL;
	}
	
}

bool SuplaDeviceClass::isInitialized(bool msg) {
	if ( srpc != NULL ) {
		
		if ( msg )
		   supla_log(LOG_DEBUG, "SuplaDevice is already initialized");
		
		return true;
	}
	
	return false;
}

void SuplaDeviceClass::begin(char GUID[SUPLA_GUID_SIZE], uint8_t mac[6], const char *Server,
	                         int LocationID, const char *LocationPWD) {

	unsigned char a;
	if ( isInitialized(true) ) return;
	
	if ( Params.cb.tcp_read == NULL
	     || Params.cb.tcp_write == NULL
	     || Params.cb.eth_setup == NULL
	     || Params.cb.svr_connected == NULL
	     || Params.cb.svr_connect == NULL
	     || Params.cb.svr_disconnect == NULL ) {
		
		supla_log(LOG_DEBUG, "Callbacks not assigned!");
		return;
	}
	
	memcpy(Params.reg_dev.GUID, GUID, SUPLA_GUID_SIZE);
	memcpy(Params.mac, mac, 6);
	Params.reg_dev.LocationID = LocationID;
	setString(Params.reg_dev.LocationPWD, LocationPWD, SUPLA_LOCATION_PWD_MAXSIZE);
	Params.server = strdup(Server);
	
	for(a=0;a<SUPLA_GUID_SIZE;a++)
		if ( Params.reg_dev.GUID[a] != 0 ) break;
	
	if ( a == SUPLA_GUID_SIZE ) {
		supla_log(LOG_DEBUG, "Invalid GUID");
		return;
	}
	
	if ( Params.server == NULL 
			|| Params.server[0] == NULL ) {
		supla_log(LOG_DEBUG, "Unknown server address");
		return;
	}
	
	if ( Params.reg_dev.LocationID == 0 ) {
		supla_log(LOG_DEBUG, "Unknown LocationID");
		return;
	}
	
	setString(Params.reg_dev.Name, "ARDUINO", SUPLA_DEVICE_NAME_MAXSIZE);
	setString(Params.reg_dev.SoftVer, "1.0", SUPLA_SOFTVER_MAXSIZE);
	
	Params.cb.eth_setup(Params.mac);

	TsrpcParams srpc_params;
	srpc_params_init(&srpc_params);
	srpc_params.data_read = &supla_arduino_data_read;
	srpc_params.data_write = &supla_arduino_data_write;
	srpc_params.on_remote_call_received = &supla_arduino_on_remote_call_received;
	srpc_params.user_params = this;
	

	srpc = srpc_init(&srpc_params);
	supla_log(LOG_DEBUG, "SuplaDevice initialized");
}

void SuplaDeviceClass::setName(const char *Name) {
	
	if ( isInitialized(true) ) return;
	setString(Params.reg_dev.Name, Name, SUPLA_DEVICE_NAME_MAXSIZE);
}

int SuplaDeviceClass::addChannel(int pin1, int pin2, bool hiIsLo) {
	if ( isInitialized(true) ) return -1;
	
	if ( Params.reg_dev.channel_count >= SUPLA_CHANNELMAXCOUNT ) {
		supla_log(LOG_DEBUG, "Channel limit exceeded");
		return -1;
	}
	
	Params.reg_dev.channels[Params.reg_dev.channel_count].Number = Params.reg_dev.channel_count;
	channel_pin = (SuplaChannelPin*)realloc(channel_pin, sizeof(SuplaChannelPin)*(Params.reg_dev.channel_count+1));
	channel_pin[Params.reg_dev.channel_count].pin1 = pin1; 
	channel_pin[Params.reg_dev.channel_count].pin2 = pin2; 
	channel_pin[Params.reg_dev.channel_count].hiIsLo = hiIsLo;
	channel_pin[Params.reg_dev.channel_count].time_left = 0;
	channel_pin[Params.reg_dev.channel_count].last_val = digitalRead(pin1);
	
	Params.reg_dev.channel_count++;
	
	return Params.reg_dev.channel_count-1;
}

bool SuplaDeviceClass::addRelay(int relayPin1, int relayPin2, bool hiIsLo, _supla_int_t functions) {
	
	int c = addChannel(relayPin1, relayPin2, hiIsLo);
	if ( c == -1 ) return false; 
	
	uint8_t _HI = hiIsLo ? LOW : HIGH;
	uint8_t _LO = hiIsLo ? HIGH : LOW;
	
	Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_RELAY;
	Params.reg_dev.channels[c].FuncList = functions;
	
	if ( relayPin1 != 0 ) {
		pinMode(relayPin1, OUTPUT); 
		digitalWrite(relayPin1, hiIsLo ? HIGH : LOW); 
		
		Params.reg_dev.channels[c].value[0] = digitalRead(relayPin1) == _HI ? 1 : 0;
	}

	if ( relayPin2 != 0 ) {
		pinMode(relayPin2, OUTPUT); 
		digitalWrite(relayPin2, hiIsLo ? HIGH : LOW); 
		
		if ( Params.reg_dev.channels[c].value[0] == 0
				&& digitalRead(relayPin2) == _HI )
			Params.reg_dev.channels[c].value[0] = 2;
	}

	
	return true;
}

bool SuplaDeviceClass::addRelay(int relayPin, bool hiIsLo) {
	return addRelay(relayPin, 0, hiIsLo, SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEGATEWAYLOCK
                              | SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEGATE
                              | SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEGARAGEDOOR
                              | SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEDOORLOCK
                              | SUPLA_BIT_RELAYFUNC_POWERSWITCH
                              | SUPLA_BIT_RELAYFUNC_LIGHTSWITCH);
}

bool SuplaDeviceClass::addRelay(int relayPin1) {
	return addRelay(relayPin1, false);
}

bool SuplaDeviceClass::addRollerShutterRelays(int relayPin1, int relayPin2, bool hiIsLo) {
	return addRelay(relayPin1, relayPin2, hiIsLo, SUPLA_BIT_RELAYFUNC_CONTROLLINGTHEROLLERSHUTTER);
}

bool SuplaDeviceClass::addRollerShutterRelays(int relayPin1, int relayPin2) {
	return addRollerShutterRelays(relayPin1, relayPin2, false);
}

bool SuplaDeviceClass::addSensorNO(int sensorPin, bool pullUp) {
	
	int c = addChannel(sensorPin, 0, false);
	if ( c == -1 ) return false; 
	
	Params.reg_dev.channels[c].Type = SUPLA_CHANNELTYPE_SENSORNO;
	pinMode(sensorPin, INPUT); 
	digitalWrite(sensorPin, pullUp ? HIGH : LOW);
	
	Params.reg_dev.channels[c].value[0] = digitalRead(sensorPin) == HIGH ? 1 : 0;
	
	return true;
}

bool SuplaDeviceClass::addSensorNO(int sensorPin) {
	return addSensorNO(sensorPin, false);
}

SuplaDeviceCallbacks SuplaDeviceClass::getCallbacks(void) {
	return Params.cb;
}


void SuplaDeviceClass::setString(char *dst, const char *src, int max_size) {
	
	if ( src == NULL ) {
		dst[0] = 0;
		return;
	}
	
	int size = strlen(src);
	
	if ( size+1 > max_size )
		size = max_size-1;
	
	memcpy(dst, src, size);
}

void SuplaDeviceClass::iterate(void) {
	
	if ( !isInitialized(false) ) return;
	
	if ( !Params.cb.svr_connected() ) {
		
		supla_log(LOG_DEBUG, "Not connected");
	    registered = 0;
	    last_response = 0;
	    ping_flag = false;
	    
		if ( !Params.cb.svr_connect(Params.server, 2015) ) {
			
		    	supla_log(LOG_DEBUG, "Connection fail. Server: %s", Params.server);
				delay(2000);
				return;
		}
	}
	
	unsigned long _millis = millis();

	
	if ( registered == 0 ) {
		
		registered = -1;
		srpc_ds_async_registerdevice_b(srpc, &Params.reg_dev);
		supla_log(LOG_DEBUG, "Register");
		
	} else if ( registered == 1 ) {
		// PING
		if ( (_millis-last_response)/1000 >= (server_activity_timeout+10)  ) {
			
			supla_log(LOG_DEBUG, "TIMEOUT");
			Params.cb.svr_disconnect();

		} else if ( ping_flag == false 
				    && (_millis-last_response)/1000 >= (server_activity_timeout-5) ) {
			ping_flag = true;
			srpc_dcs_async_ping_server(srpc);
		}
	}
	
	if ( last_iterate_time != 0 ) {
		
		unsigned long td = abs(_millis - last_iterate_time);
		
		for(int a=0;a<Params.reg_dev.channel_count;a++) {
			
			if ( channel_pin[a].time_left != 0 ) {
				if ( td >= channel_pin[a].time_left ) {
					
					channel_pin[a].time_left = 0;
					
					if ( Params.reg_dev.channels[a].Type == SUPLA_CHANNELTYPE_SENSORNO ) 
						channel_pin[a].last_val = -1;
					
					else if ( Params.reg_dev.channels[a].Type == SUPLA_CHANNELTYPE_RELAY )
						channelSetValue(a, 0, 0);
					
				} else if ( channel_pin[a].time_left > 0 ) {
					channel_pin[a].time_left-=td;
				}
			}
			
			if ( Params.reg_dev.channels[a].Type == SUPLA_CHANNELTYPE_SENSORNO ) {
				
				uint8_t val = digitalRead(channel_pin[a].pin1);
				
				if ( val != channel_pin[a].last_val ) {
					
					channel_pin[a].last_val = val;
					
					if ( channel_pin[a].time_left <= 0 ) {
						channel_pin[a].time_left = 500;
						channelValueChanged(Params.reg_dev.channels[a].Number, val == HIGH ? 1 : 0); 
					}
						
				}		
			}
			
		}

	}

	
	last_iterate_time = millis();
	
	if( srpc_iterate(srpc) == SUPLA_RESULT_FALSE ) {
		supla_log(LOG_DEBUG, "Iterate fail");
		Params.cb.svr_disconnect();
		delay(5000);
	}
	
	
}

void SuplaDeviceClass::onResponse(void) {
	last_response = millis();
	ping_flag = false;
}

void SuplaDeviceClass::onVersionError(TSDC_SuplaVersionError *version_error) {
	supla_log(LOG_ERR, "Protocol version error");
	Params.cb.svr_disconnect();
	delay(5000);
}

void SuplaDeviceClass::onRegisterResult(TSD_SuplaRegisterDeviceResult *register_device_result) {

	switch(register_device_result->result_code) {
	case SUPLA_RESULTCODE_BAD_CREDENTIALS:
		supla_log(LOG_ERR, "Bad credentials!");
		break;

	case SUPLA_RESULTCODE_TEMPORARILY_UNAVAILABLE:
		supla_log(LOG_NOTICE, "Temporarily unavailable!");
		break;

	case SUPLA_RESULTCODE_LOCATION_CONFLICT:
		supla_log(LOG_ERR, "Location conflict!");
		break;

	case SUPLA_RESULTCODE_CHANNEL_CONFLICT:
		supla_log(LOG_ERR, "Channel conflict!");
		break;
	case SUPLA_RESULTCODE_TRUE:

		server_activity_timeout = register_device_result->activity_timeout;
		registered = 1;

		supla_log(LOG_DEBUG, "Registered and ready.");

		if ( server_activity_timeout != ACTIVITY_TIMEOUT ) {

			TDCS_SuplaSetActivityTimeout at;
			at.activity_timeout = ACTIVITY_TIMEOUT;
			srpc_dcs_async_set_activity_timeout(srpc, &at);

		}

		return;

	case SUPLA_RESULTCODE_DEVICE_DISABLED:
		supla_log(LOG_NOTICE, "Device is disabled!");
		break;

	case SUPLA_RESULTCODE_LOCATION_DISABLED:
		supla_log(LOG_NOTICE, "Location is disabled!");
		break;

	case SUPLA_RESULTCODE_DEVICE_LIMITEXCEEDED:
		supla_log(LOG_NOTICE, "Device limit exceeded!");
		break;

	case SUPLA_RESULTCODE_GUID_ERROR:
		supla_log(LOG_NOTICE, "Incorrect device GUID!");
		break;
	}

	Params.cb.svr_disconnect();
	delay(5000);
}


void SuplaDeviceClass::channelValueChanged(int channel_number, char v) {

	if ( srpc != NULL
		 && registered == 1 ) {

		char value[SUPLA_CHANNELVALUE_SIZE];
		memset(value, 0, SUPLA_CHANNELVALUE_SIZE);
		value[0] = v;

		srpc_ds_async_channel_value_changed(srpc, channel_number, value);
	}

}

void SuplaDeviceClass::channelSetValue(int channel, char value, _supla_int_t DurationMS) {
	
	bool success = false;
	
	uint8_t _HI = channel_pin[channel].hiIsLo ? LOW : HIGH;
	uint8_t _LO = channel_pin[channel].hiIsLo ? HIGH : LOW;

	if ( Params.reg_dev.channels[channel].Type == SUPLA_CHANNELTYPE_RELAY ) {
		
		
		if ( value == 0 ) {
			
			if ( channel_pin[channel].pin1 != 0 ) {
				digitalWrite(channel_pin[channel].pin1, _LO); 
				
				success = digitalRead(channel_pin[channel].pin1) == _LO;
			}
				

			if ( channel_pin[channel].pin2 != 0 ) {
				digitalWrite(channel_pin[channel].pin2, _LO); 
				
				if ( !success )
					success = digitalRead(channel_pin[channel].pin2) == _LO;
			}
				
			
		} else if ( value == 1 ) {
			
			if ( channel_pin[channel].pin2 != 0 ) {
				digitalWrite(channel_pin[channel].pin2, _LO); 
				delay(50);
			}
			
			if ( channel_pin[channel].pin1 != 0 ) {
				digitalWrite(channel_pin[channel].pin1, _HI); 
				
				if ( !success )
					success = digitalRead(channel_pin[channel].pin1) == _HI;
				
				if ( DurationMS > 0 )
					channel_pin[channel].time_left = DurationMS;
			}
				
			
			
		} else if ( value == 2 ) {
			
			if ( channel_pin[channel].pin1 != 0 ) {
				digitalWrite(channel_pin[channel].pin1, _LO); 
				delay(50);
			}
			
			if ( channel_pin[channel].pin2 != 0 ) {
				digitalWrite(channel_pin[channel].pin2, _HI); 
				
				if ( !success )
					success = digitalRead(channel_pin[channel].pin2) == _HI;
			}
				
		}
			
		
	}

	
	if ( success
			&& registered == 1 
			&& srpc ) {
		channelValueChanged(Params.reg_dev.channels[channel].Number, value);
	}
	
}

void SuplaDeviceClass::channelSetValue(TSD_SuplaChannelNewValue *new_value) {

	for(int a=0;a<Params.reg_dev.channel_count;a++) 
		if ( new_value->ChannelNumber == Params.reg_dev.channels[a].Number ) {
			channelSetValue(new_value->ChannelNumber, new_value->value[0], new_value->DurationMS);
			break;
		}

	
}

void SuplaDeviceClass::channelSetActivityTimeoutResult(TSDC_SuplaSetActivityTimeoutResult *result) {
	server_activity_timeout = result->activity_timeout;
}

SuplaDeviceClass SuplaDevice;