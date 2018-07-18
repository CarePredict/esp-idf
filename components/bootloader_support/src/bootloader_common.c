// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <stdbool.h>
#include <assert.h>
#include "string.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "rom/spi_flash.h"
#include "rom/rtc.h"
#include "rom/crc.h"
#include "rom/ets_sys.h"
#include "rom/gpio.h"
#include "rom/BootloaderVrsnControl.h"
#include "esp_flash_data_types.h"
#include "esp_secure_boot.h"
#include "esp_flash_partitions.h"
#include "bootloader_flash.h"
#include "bootloader_common.h"
#include "soc/gpio_periph.h"

static const char* TAG = "boot_comm";

uint32_t bootloader_common_ota_select_crc(const esp_ota_select_entry_t *s)
{
    return crc32_le(UINT32_MAX, (uint8_t*)&s->ota_seq, 4);
}

bool bootloader_common_ota_select_valid(const esp_ota_select_entry_t *s)
{
    return s->ota_seq != UINT32_MAX && s->crc == bootloader_common_ota_select_crc(s);
}

esp_comm_gpio_hold_t bootloader_common_check_long_hold_gpio(uint32_t num_pin, uint32_t delay_sec)
{
	/**************< CP Custom section >*************/
	if (RTC_NO_INIT_DATA_STRUCT->StructIntegrityMagic != STRUCT_INTEGRITY_MAGIC_NUM) {
		memset((uint8_t*) RTC_NO_INIT_DATA_STRUCT, 0, sizeof(RTC_NO_INIT_DATA_STRUCT_t));
		ESP_LOGE(TAG, "[%s-%d] RstCnt2TriggerFactory: %d--garbage data! Reseting to 0", __FUNCTION__, __LINE__, RTC_NO_INIT_DATA_STRUCT->RstCnt2TriggerFactory);
		RTC_NO_INIT_DATA_STRUCT->RstCnt2TriggerFactory = 0;
		RTC_NO_INIT_DATA_STRUCT->StructIntegrityMagic = STRUCT_INTEGRITY_MAGIC_NUM;
	}

	RTC_NO_INIT_DATA_STRUCT->RstCnt2TriggerFactory++;
	ESP_LOGE(TAG, "[%s-%d] RstCnt2TriggerFactory: %d", __FUNCTION__, __LINE__, RTC_NO_INIT_DATA_STRUCT->RstCnt2TriggerFactory);

	if (RTC_NO_INIT_DATA_STRUCT->RstCnt2TriggerFactory > MAX_RST_CNT_2_TRIGGER_FACTORY_BOOT) {
		ESP_LOGE(TAG, "Forcing Factory-> DUE to consecutive reset: %d, MAx val: %d", RTC_NO_INIT_DATA_STRUCT->RstCnt2TriggerFactory, MAX_RST_CNT_2_TRIGGER_FACTORY_BOOT);
		return GPIO_LONG_HOLD;
	}

	if (rtc_get_reset_reason(0) == 5) { //DEEPSLEEP_RESET
		return GPIO_SHORT_HOLD;
	}

	/**************< CP Custom section Ends>*************/

	gpio_pad_select_gpio(num_pin);
    if (GPIO_PIN_MUX_REG[num_pin]) {
        PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[num_pin]);
    }
    gpio_pad_pullup(num_pin);
    uint32_t tm_start = esp_log_early_timestamp();
	uint32_t tm_SecCntr = tm_start;
    if (GPIO_INPUT_GET(num_pin) == 1) {
        return GPIO_NOT_HOLD;
    }
	int cnt = 0;
    do {
        if (GPIO_INPUT_GET(num_pin) != 0) {
            return GPIO_SHORT_HOLD;
        }

		if (((esp_log_early_timestamp() - tm_SecCntr) / 1000L) >= 1) {
			tm_SecCntr = esp_log_early_timestamp();
			cnt++;
			ESP_LOGE(TAG, "ForceFactoryAppButton Held for: %d Sec", cnt);
		}
    } while (delay_sec > ((esp_log_early_timestamp() - tm_start) / 1000L));
    return GPIO_LONG_HOLD;
}

// Search for a label in the list. list = "nvs1, nvs2, otadata, nvs"; label = "nvs".
bool bootloader_common_label_search(const char *list, char *label)
{
    if (list == NULL || label == NULL) {
        return false;
    }
    const char *sub_list_start_like_label = strstr(list, label);
    while (sub_list_start_like_label != NULL) {

        // ["," or " "] + label + ["," or " " or "\0"]
        // first character before the label found there must be a delimiter ["," or " "].
        int idx_first = sub_list_start_like_label - list;
        if (idx_first == 0 || (idx_first != 0 && (list[idx_first - 1] == ',' || list[idx_first - 1] == ' '))) {
            // next character after the label found there must be a delimiter ["," or " " or "\0"].
            int len_label = strlen(label);
            if (sub_list_start_like_label[len_label] == 0   ||
                sub_list_start_like_label[len_label] == ',' ||
                sub_list_start_like_label[len_label] == ' ') {
                return true;
            }
        }

        // [start_delim] + label + [end_delim] was not found.
        // Position is moving to next delimiter if it is not the end of list.
        int pos_delim = strcspn(sub_list_start_like_label, ", ");
        if (pos_delim == strlen(sub_list_start_like_label)) {
            break;
        }
        sub_list_start_like_label = strstr(&sub_list_start_like_label[pos_delim], label);
    }
    return false;
}

bool bootloader_common_erase_part_type_data(const char *list_erase, bool ota_data_erase)
{
    const esp_partition_info_t *partitions;
    const char *marker;
    esp_err_t err;
    int num_partitions;
    bool ret = true;

#ifdef CONFIG_SECURE_BOOT_ENABLED
    if (esp_secure_boot_enabled()) {
        ESP_LOGI(TAG, "Verifying partition table signature...");
        err = esp_secure_boot_verify_signature(ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to verify partition table signature.");
            return false;
        }
        ESP_LOGD(TAG, "Partition table signature verified");
    }
#endif

    partitions = bootloader_mmap(ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
    if (!partitions) {
            ESP_LOGE(TAG, "bootloader_mmap(0x%x, 0x%x) failed", ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
            return false;
    }
    ESP_LOGD(TAG, "mapped partition table 0x%x at 0x%x", ESP_PARTITION_TABLE_OFFSET, (intptr_t)partitions);

    err = esp_partition_table_basic_verify(partitions, true, &num_partitions);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to verify partition table");
        ret = false;
    } else {
        ESP_LOGI(TAG, "## Label            Usage Offset   Length   Cleaned");
        for (int i = 0; i < num_partitions; i++) {
            const esp_partition_info_t *partition = &partitions[i];
            char label[sizeof(partition->label) + 1] = {0};
            if (partition->type == PART_TYPE_DATA) {
                bool fl_ota_data_erase = false;
                if (ota_data_erase == true && partition->subtype == PART_SUBTYPE_DATA_OTA) {
                    fl_ota_data_erase = true;
                }
                // partition->label is not null-terminated string.
                strncpy(label, (char *)&partition->label, sizeof(partition->label));
                if (fl_ota_data_erase == true || (bootloader_common_label_search(list_erase, label) == true)) {
                    err = esp_rom_spiflash_erase_area(partition->pos.offset, partition->pos.size);
                    if (err != ESP_OK) {
                        ret = false;
                        marker = "err";
                    } else {
                        marker = "yes";
                    }
                } else {
                    marker = "no";
                }

                ESP_LOGI(TAG, "%2d %-16s data  %08x %08x [%s]", i, partition->label,
                         partition->pos.offset, partition->pos.size, marker);
            }
        }
    }

    bootloader_munmap(partitions);

    return ret;
}
