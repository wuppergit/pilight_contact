/*

	Copyright (C) 2017 wupperbra		2017-01-25

	This file may be part of pilight.

	pilight is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	pilight is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../core/pilight.h"
#include "../../core/common.h"
#include "../../core/dso.h"
#include "../../core/log.h"
#include "../../core/binary.h"
#include "../../core/gc.h"
#include "../protocol.h"
#include "mcvoice_rm1011b.h"

#define PULSE_MULTIPLIER	3
#define MIN_PULSE_LENGTH	450			
#define MAX_PULSE_LENGTH	500			
#define AVG_PULSE_LENGTH	475          
#define RAW_LENGTH			52  		


/**
 *  McVoice-pulsstream has a header as well as a footer. The header is recognized ba
 *  pilight as a seperate pulsstream. 
 *  Therefor RAW_LENGHT of mcvoice_rm1001b variable RAW_LENGTH must be reduced by 1 
 *  to validate correctly
 **/
static int validate(void) {
	if(mcvoice_rm1011b->rawlen == RAW_LENGTH - 1) {
		if(mcvoice_rm1011b->raw[mcvoice_rm1011b->rawlen-1] >= (MIN_PULSE_LENGTH*PULSE_DIV) &&
		   mcvoice_rm1011b->raw[mcvoice_rm1011b->rawlen-1] <= (MAX_PULSE_LENGTH*PULSE_DIV)) {
		   return 0;
		}
	}

	return -1;
}


static void createMessage(int unitcode, int state) {
	mcvoice_rm1011b->message = json_mkobject();
	json_append_member(mcvoice_rm1011b->message, "unitcode", json_mknumber(unitcode, 0));

	if(state == 0) {
		json_append_member(mcvoice_rm1011b->message, "state", json_mkstring("on"));
	} else {
		json_append_member(mcvoice_rm1011b->message, "state", json_mkstring("off"));
	}
	
}

/**
 *  transform array of receives pulses into binary. The array may contain 51 elements
 *  which are 1 part (right-part) of header, 2 parts per 24 bits binary information and
 *  2 parts for the footer.
 *  one binary is reprensente by 2 two elements
 **/
static void parseCode(void) {

	int x = 0, binElements = (RAW_LENGTH-1-2)/2, binary[binElements];

	for(x=1; x < mcvoice_rm1011b->rawlen-2;x+=2) {
	   if (mcvoice_rm1011b->raw[x+1] >  MIN_PULSE_LENGTH *6 &&
		   mcvoice_rm1011b->raw[x+1] <  MAX_PULSE_LENGTH *6) {
	       binary[x/2]=1;
	   } else {
	       binary[x/2]=0;
	   } 
	}

	int unitcode = binToDecRev(binary, 0, binElements-1);     
	createMessage(unitcode, 1);

}

/**
  *  set 2 elements of the pulse-stream to LOW-information
  *  s = position in pulse-strem to set 
  *  e = last position
  */
static void createLow(int s, int e) {
	int i;
	for(i=s;i<=e;i+=2) {    
		mcvoice_rm1011b->raw[i]=(AVG_PULSE_LENGTH*2);
		mcvoice_rm1011b->raw[i+1]=(AVG_PULSE_LENGTH*PULSE_MULTIPLIER);		
	}
}

/**
  *  set 2 elements of the pulse-stream for HIGH-information
  *  s = position in pulse-strem to set 
  */
static void createHigh(int s, int e) {
	int i;
	for(i=s;i<=e;i+=2) { 		
		mcvoice_rm1011b->raw[i]=(AVG_PULSE_LENGTH*2);
		mcvoice_rm1011b->raw[i+1]=(AVG_PULSE_LENGTH*PULSE_MULTIPLIER*2);			
	}
}

/**
  *  set all pulses of the pulse-stream to LOW except footer
  */
static void clearCode(void) {
	createLow(0,49);					
}


static void createUnitCode(int unitCode)  {

	int binary[255];
	int length = 0;
	int i=0, x=0;

	length = decToBin(unitCode, binary);	
	for(i=0;i<=length;i++) {
		if(binary[i]==1) {		
			x=2+(i*2);
			createHigh(x, x+1);
		}
	}
}

/**
  * McVoice-RM1011B-protocol has a header
  */
static void createHeader(void) {
	mcvoice_rm1011b->raw[0]=(AVG_PULSE_LENGTH*17);
	mcvoice_rm1011b->raw[1]=(AVG_PULSE_LENGTH*2);
}

/**
  * McVoice-RM1011B-protocol has a footer
  */
static void createFooter(void) {
	mcvoice_rm1011b->raw[50]=(AVG_PULSE_LENGTH*2);
	mcvoice_rm1011b->raw[51]=(AVG_PULSE_LENGTH*34);	
}

static int createCode(JsonNode *code) {

	int unitcode = -1;
	int state = -1;
	double itmp = 0;

	if(json_find_number(code, "unitcode", &itmp) == 0)
		unitcode = (int)round(itmp);
	if(json_find_number(code, "off", &itmp) == 0)
		state=1;
	else if(json_find_number(code, "on", &itmp) == 0)
		state=0;
		
	if(unitcode == -1 || state == -1) {
		logprintf(LOG_ERR, "mcvoice_rm1011b: insufficient number of arguments");
		return EXIT_FAILURE;
	} else {	
		createMessage(unitcode, state);				
		clearCode();		
		createHeader();	
		createUnitCode(unitcode);
		createFooter();
		mcvoice_rm1011b->rawlen = RAW_LENGTH;				
	}
	return EXIT_SUCCESS;
}

static void printHelp(void) {
	printf("\t -u --unitcode=unitcode\t\tcontrol a device with this unitcode\n");
	printf("\t -t --on\t\t\tsend an on signal\n");
	printf("\t -f --off\t\t\tsend an off signal\n");
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void mcvoice_rm1011bInit(void) {

	//  add protocol  mcvoice_rm1011b)  to the protocol list
	protocol_register(&mcvoice_rm1011b);			
	//  set identifier so pilight can identify protocol in the protocol list			
	protocol_set_id(mcvoice_rm1011b, "mcvoice_rm1011b");
	//  set name and description for help pilight-send
	protocol_device_add(mcvoice_rm1011b, "mcvoice_rm1011b", "McVoice Smoke Alarm ");
	//  set DEVICE-type
	mcvoice_rm1011b->devtype = SWITCH;      //ALARM;				//SWITCH;
	mcvoice_rm1011b->hwtype = RF433;
	mcvoice_rm1011b->minrawlen = RAW_LENGTH;
	mcvoice_rm1011b->maxrawlen = RAW_LENGTH;
	mcvoice_rm1011b->maxgaplen = MAX_PULSE_LENGTH*PULSE_DIV;
	mcvoice_rm1011b->mingaplen = MIN_PULSE_LENGTH*PULSE_DIV;
	mcvoice_rm1011b->txrpt = 35;	


	options_add(&mcvoice_rm1011b->options, 'u', "unitcode", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^([0-9]{7})$");
	options_add(&mcvoice_rm1011b->options, 't', "on", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);
	options_add(&mcvoice_rm1011b->options, 'f', "off", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);

	options_add(&mcvoice_rm1011b->options, 0, "readonly", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");

	mcvoice_rm1011b->parseCode=&parseCode;
	mcvoice_rm1011b->createCode=&createCode;
	mcvoice_rm1011b->printHelp=&printHelp;
	mcvoice_rm1011b->validate=&validate;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
//printf("function passed:  compatibility                   \n" );
	module->name = "mcvoice_rm1011b";
	module->version = "1.0";
	module->reqversion = "6.0";
	module->reqcommit = "200";
}

void init(void) {
	mcvoice_rm1011bInit();
}

#endif
