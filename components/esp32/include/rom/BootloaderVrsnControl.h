/*
 * BootLoaderVersionControl.h
 *
 *  Created on: May 18, 2018
 *      Author: Muhammad S Khan
 */

#ifndef COMPONENTS_BOOTLOADER_SUBPROJECT_MAIN_BOOTLOADERVRSNCONTROL_H_
#define COMPONENTS_BOOTLOADER_SUBPROJECT_MAIN_BOOTLOADERVRSNCONTROL_H_
#include "esp_attr.h"
#include "sdkconfig.h"

#define MOTHER_SHIP_ESP_IDF_VRSN0	6 /*Updated at: July 03, 2018*/  //DHCP info out to application layer implemented

#define MAX_RST_CNT_2_TRIGGER_FACTORY_BOOT 100

typedef struct {
	uint32_t RstCnt2TriggerFactory;
} RTC_NO_INIT_DATA_STRUCT_t;

///*Structure size can not be more than what is defined in the linker (16 bytes)*/
//#if sizeof(RTC_NO_INIT_DATA_STRUCT_t) > 16
//#error "Structure size can not be more than what is defined in the linker (16 bytes)"

#define RTC_NO_INIT_DATA_STRUCT	((RTC_NO_INIT_DATA_STRUCT_t *) (0x50000000 + (CONFIG_ULP_COPROC_RESERVE_MEM)))

#endif /* COMPONENTS_BOOTLOADER_SUBPROJECT_MAIN_BOOTLOADERVRSNCONTROL_H_ */
