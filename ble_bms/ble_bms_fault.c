// SPDX-License-Identifier: GPL-3.0-or-later
#include "nrf.h"
#include "nrf_log.h"
#include "app_error.h"
#include <stdint.h>

// Retained through software reset, never written to flash in a fault handler.
static volatile struct {
    uint32_t magic,pc,lr,cfsr,hfsr,bfar,mmfar,id,error,line,info;
} fault __attribute__((section(".noinit.ble_bms")));

void ble_bms_fault_report(void) {
    if(fault.magic==0xbbee5284) {
        NRF_LOG_ERROR("Previous fault pc=0x%x lr=0x%x",fault.pc,fault.lr);
        NRF_LOG_ERROR("cfsr=0x%x hfsr=0x%x bfar=0x%x mmfar=0x%x",fault.cfsr,fault.hfsr,fault.bfar,fault.mmfar);
        NRF_LOG_ERROR("fault id=0x%x sdk_error=0x%x line=%u",fault.id,fault.error,fault.line);
        NRF_LOG_ERROR("fault info=0x%x",fault.info);
        fault.magic=0;
    }
}
void ble_bms_hardfault(uint32_t *stack) __attribute__((noreturn,used));
void ble_bms_hardfault(uint32_t *stack) {
    __disable_irq();
    fault.pc=0;fault.lr=0;fault.id=0;fault.error=0;fault.line=0;fault.info=0;
    if((uintptr_t)stack>=0x20000000 && (uintptr_t)stack<=0x20040000-32) {
        fault.pc=stack[6];fault.lr=stack[5];
    }
    fault.cfsr=SCB->CFSR;fault.hfsr=SCB->HFSR;fault.bfar=SCB->BFAR;fault.mmfar=SCB->MMFAR;
    fault.magic=0xbbee5284;__DSB();NVIC_SystemReset();for(;;){}
}
// Override the SDK weak handler so assertion details survive its reset too.
void app_error_fault_handler(uint32_t id,uint32_t pc,uint32_t info) {
    __disable_irq();
    fault.pc=pc;fault.lr=0;fault.id=id;fault.error=0;fault.line=0;fault.info=info;
    fault.cfsr=SCB->CFSR;fault.hfsr=SCB->HFSR;fault.bfar=SCB->BFAR;fault.mmfar=SCB->MMFAR;
    if(info>=0x20000000 && info<=0x20040000-sizeof(error_info_t)) {
        if(id==NRF_FAULT_ID_SDK_ERROR){error_info_t *e=(error_info_t*)info;fault.error=e->err_code;fault.line=e->line_num;}
        else if(id==NRF_FAULT_ID_SDK_ASSERT){assert_info_t *e=(assert_info_t*)info;fault.line=e->line_num;}
    }
    fault.magic=0xbbee5284;__DSB();
    if(CoreDebug->DHCSR&CoreDebug_DHCSR_C_DEBUGEN_Msk)__BKPT(0);
    NVIC_SystemReset();for(;;){}
}
void HardFault_Handler(void) __attribute__((naked));
void HardFault_Handler(void) {
    __asm volatile("tst lr,#4\n ite eq\n mrseq r0,msp\n mrsne r0,psp\n b ble_bms_hardfault\n");
}
