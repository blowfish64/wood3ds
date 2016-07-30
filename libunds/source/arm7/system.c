/*---------------------------------------------------------------------------------

  Copyright (C) 2005 - 2010
    Michael Noland (joat)
	Jason Rogers (Dovoto)
	Dave Murphy (WinterMute)

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any
  damages arising from the use of this software.

  Permission is granted to anyone to use this software for any
  purpose, including commercial applications, and to alter it and
  redistribute it freely, subject to the following restrictions:

  1.  The origin of this software must not be misrepresented; you
      must not claim that you wrote the original software. If you use
      this software in a product, an acknowledgment in the product
      documentation would be appreciated but is not required.
  2.  Altered source versions must be plainly marked as such, and
      must not be misrepresented as being the original software.
  3.  This notice may not be removed or altered from any source
      distribution.

---------------------------------------------------------------------------------*/
#include <nds/system.h>
#include <nds/fifocommon.h>
#include <nds/fifomessages.h>
#include <nds/ndstypes.h>
#include <nds/interrupts.h>
#include <nds/bios.h>
#include <nds/debug.h>
#include <nds/arm7/clock.h>
#include <nds/arm7/sdmmc.h>

//---------------------------------------------------------------------------------
void powerValueHandler(u32 value, void* user_data) {
//---------------------------------------------------------------------------------
	u32 temp;
	u32 ie_save, ime_save;
	int power;

	switch(value & 0xFFFF0000) {
		//power control
	case PM_REQ_LED:
		temp = readPowerManagement(PM_CONTROL_REG);
		temp &= ~(3 << 4); //clear led bits
		temp |= value & 0xFF;
		writePowerManagement(PM_CONTROL_REG, temp);
		break;
	case PM_REQ_ON:
		temp = readPowerManagement(PM_CONTROL_REG);
		writePowerManagement(PM_CONTROL_REG, temp | (value & 0xFFFF));
		break;
	case PM_REQ_OFF:
		temp = readPowerManagement(PM_CONTROL_REG) & (~(value & 0xFFFF));
		writePowerManagement(PM_CONTROL_REG, temp);
		break;

	case PM_REQ_SLEEP:
			
		ime_save = REG_IME;
		REG_IME = 1;
		ie_save = REG_IE;
		// Turn the speaker down.
		swiChangeSoundBias(0,0x400);
		// Save current power state.
		power = readPowerManagement(PM_CONTROL_REG);
		// Set sleep LED.
		writePowerManagement(PM_CONTROL_REG, PM_LED_CONTROL(1));
		// Register for the lid interrupt.
		REG_IE = IRQ_LID;

		// Power down till we get our interrupt.
		swiSleep(); //waits for PM (lid open) interrupt

		//100ms
		swiDelay(838000);
		
		// Restore the interrupt state.
		REG_IE = ie_save;
		REG_IME = ime_save;

		// Restore power state.
		writePowerManagement(PM_CONTROL_REG, power);

		// Turn the speaker up.
		swiChangeSoundBias(1,0x400); 

		// update clock tracking
		resyncClock();
		break;

	}
}

//extern void sdmmcValueHandler(u32 value, void* user_data);
//extern void sdmmcMsgHandler(u32 value, void* user_data);

int msg_count = 0;
static FifoMessage msg;

//---------------------------------------------------------------------------------
void sdmmcMsgHandler(u32 value, void* user_data) {
//---------------------------------------------------------------------------------
	switch(msg_count) {
		case 0:
			msg.type = value;
			msg_count++;
			fifoSendValue32(FIFO_SDMMC, SDMMC_MSG);
			break;
		case 1:
			msg.sdParams.startsector = value;
			msg_count++;
			fifoSendValue32(FIFO_SDMMC, SDMMC_MSG);
			break;
		case 2:
			msg.sdParams.numsectors = value;
			msg_count++;
			fifoSendValue32(FIFO_SDMMC, SDMMC_MSG);
			break;
		case 3:
			msg.sdParams.buffer = value;
			msg_count++;
			
			int retval = 0;
			
			int oldIME = enterCriticalSection();
			switch (msg.type) {
				case SYS_SD_READ_SECTORS:
					/* retval =  */sdmmc_sdcard_readsectors(msg.sdParams.startsector, msg.sdParams.numsectors, msg.sdParams.buffer);
					break;
				case SYS_SD_WRITE_SECTORS:
					//retval = sdmmc_writesectors(msg.sdParams.startsector, msg.sdParams.numsectors, msg.sdParams.buffer);
					break;
			}

			leaveCriticalSection(oldIME);
			fifoSendValue32(FIFO_SDMMC, 0);
			break;
	}
}

//---------------------------------------------------------------------------------
void sdmmcValueHandler(u32 value, void* user_data) {
//---------------------------------------------------------------------------------
    int result = 0;

    int oldIME = enterCriticalSection();

	switch(value) {

	case SDMMC_HAVE_SD:
		result = sdmmc_read16(REG_SDSTATUS0);
		break;

	case SDMMC_SD_START:
		if (sdmmc_read16(REG_SDSTATUS0) == 0) {
			result = 1;
		} else {
			sdmmc_controller_init();
			result = sdmmc_sdcard_init();
		}
		break;

	case SDMMC_SD_IS_INSERTED:
		result = sdmmc_cardinserted();
		break;

	case SDMMC_SD_STOP:
		break;		
		
	case SDMMC_MSG:
	    msg_count = 0;
		break;		
	}
	

    leaveCriticalSection(oldIME);

    fifoSendValue32(FIFO_SDMMC, result);
}

//---------------------------------------------------------------------------------
void installSystemFIFO(void) {
//---------------------------------------------------------------------------------
    
	fifoSetValue32Handler(FIFO_PM, powerValueHandler, 0);
	fifoSetValue32Handler(FIFO_SDMMC, sdmmcValueHandler, 0);
	fifoSetValue32Handler(FIFO_SDMSG, sdmmcMsgHandler, 0);	
	
	nocashMessage("system.c installSystemFIFO");
}


