// SPDX-License-Identifier: GPL-3.0-or-later
// Spare nRF52840 DK only. This is a synthetic BMS, never a controller radio image.
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_ble_gatt.h"
#include "ble_advertising.h"
#include "ble_srv_common.h"
#include "app_timer.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "sim_frames.h"

NRF_BLE_GATT_DEF(gatt);
BLE_ADVERTISING_DEF(advertising);
static uint16_t connection=BLE_CONN_HANDLE_INVALID;
static ble_gatts_char_handles_t notify_char,write_char;
static uint8_t bytes[244];static unsigned length,offset;
static bool subscribed,disconnect_down;
static volatile bool periodic;
APP_TIMER_DEF(timer);

static void timer_event(void *p){(void)p;periodic=true;}
static void prepare(unsigned command) {
    if(offset<length)return;
    length=ble_bms_sim_frame(BMS_SIM_PROTOCOL,command,bytes);offset=0;
    if(!nrf_gpio_pin_read(11))bytes[length-3]^=1; // Button 1: corrupt CRC.
}
static void ble_event(ble_evt_t const *e,void *context) {
    (void)context;
    switch(e->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
        connection=e->evt.gap_evt.conn_handle;NRF_LOG_INFO("Simulator connected");break;
    case BLE_GAP_EVT_DISCONNECTED:
        connection=BLE_CONN_HANDLE_INVALID;subscribed=false;length=offset=0;
        NRF_LOG_INFO("Simulator disconnected");break;
    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
        sd_ble_gap_sec_params_reply(connection,BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP,NULL,NULL);break;
    case BLE_GATTS_EVT_SYS_ATTR_MISSING:sd_ble_gatts_sys_attr_set(connection,NULL,0,0);break;
    case BLE_GATTS_EVT_WRITE: {
        ble_gatts_evt_write_t const *w=&e->evt.gatts_evt.params.write;
        if(w->handle==notify_char.cccd_handle && w->len==2)subscribed=(w->data[0]&1)!=0;
        if(w->handle==write_char.value_handle && subscribed) {
            unsigned command=(BMS_SIM_PROTOCOL==1 && w->len>=3)?w->data[2]:3;
            prepare(command);
        }
    } break;
    default:break;
    }
}
NRF_SDH_BLE_OBSERVER(sim_observer,3,ble_event,NULL);

int main(void) {
    APP_ERROR_CHECK(NRF_LOG_INIT(NULL));NRF_LOG_DEFAULT_BACKENDS_INIT();
    extern void ble_bms_fault_report(void);ble_bms_fault_report();NRF_LOG_FLUSH();
    for(unsigned i=0;i<4;i++)nrf_gpio_cfg_input((uint32_t[]){11,12,24,25}[i],NRF_GPIO_PIN_PULLUP);
    APP_ERROR_CHECK(app_timer_init());APP_ERROR_CHECK(nrf_sdh_enable_request());
    uint32_t ram=0;APP_ERROR_CHECK(nrf_sdh_ble_default_cfg_set(1,&ram));APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram));
    APP_ERROR_CHECK(nrf_ble_gatt_init(&gatt,NULL));
    ble_gap_conn_sec_mode_t security;BLE_GAP_CONN_SEC_MODE_SET_OPEN(&security);
    static const char *name=BMS_SIM_PROTOCOL==1?"BMS simulator JBD":BMS_SIM_PROTOCOL==2?"BMS simulator ANT":"BMS simulator Daly";
    APP_ERROR_CHECK(sd_ble_gap_device_name_set(&security,(uint8_t*)name,strlen(name)));
    ble_uuid_t uuid={BMS_SIM_PROTOCOL==1?0xff00:BMS_SIM_PROTOCOL==2?0xffe0:0xfff0,BLE_UUID_TYPE_BLE};
    uint16_t service;APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,&uuid,&service));
    ble_add_char_params_t p={0};p.uuid=BMS_SIM_PROTOCOL==1?0xff01:BMS_SIM_PROTOCOL==2?0xffe1:0xfff1;
    p.uuid_type=BLE_UUID_TYPE_BLE;p.max_len=244;p.is_var_len=1;p.char_props.notify=1;
    p.char_props.write=BMS_SIM_PROTOCOL==2;p.char_props.write_wo_resp=BMS_SIM_PROTOCOL==2;
    p.read_access=p.write_access=p.cccd_write_access=SEC_OPEN;
    APP_ERROR_CHECK(characteristic_add(service,&p,&notify_char));
    if(BMS_SIM_PROTOCOL==2)write_char=notify_char;
    else {
        p.uuid=BMS_SIM_PROTOCOL==1?0xff02:0xfff2;p.char_props.notify=0;
        p.char_props.write=p.char_props.write_wo_resp=1;
        APP_ERROR_CHECK(characteristic_add(service,&p,&write_char));
    }
    ble_advertising_init_t adv={0};adv.advdata.name_type=BLE_ADVDATA_FULL_NAME;
    adv.advdata.flags=BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    adv.srdata.uuids_complete.uuid_cnt=1;adv.srdata.uuids_complete.p_uuids=&uuid;
    adv.config.ble_adv_fast_enabled=true;adv.config.ble_adv_fast_interval=160;adv.config.ble_adv_fast_timeout=0;
    APP_ERROR_CHECK(ble_advertising_init(&advertising,&adv));ble_advertising_conn_cfg_tag_set(&advertising,1);
    APP_ERROR_CHECK(ble_advertising_start(&advertising,BLE_ADV_MODE_FAST));
    APP_ERROR_CHECK(app_timer_create(&timer,APP_TIMER_MODE_REPEATED,timer_event));
    APP_ERROR_CHECK(app_timer_start(timer,APP_TIMER_TICKS(1000),NULL));
    NRF_LOG_INFO("Synthetic BMS protocol=%u; buttons CRC / silence / disconnect / 1-byte fragments",BMS_SIM_PROTOCOL);
    for(;;) {
        bool down=!nrf_gpio_pin_read(24);
        if(down&&!disconnect_down&&connection!=BLE_CONN_HANDLE_INVALID)sd_ble_gap_disconnect(connection,BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        disconnect_down=down;
        if(periodic){periodic=false;if(subscribed)prepare(3);}
        if(subscribed&&offset<length&&nrf_gpio_pin_read(12)) {
            uint16_t count=length-offset;unsigned chunk=nrf_gpio_pin_read(25)?20:1;if(count>chunk)count=chunk;
            ble_gatts_hvx_params_t notification={0};notification.handle=notify_char.value_handle;
            notification.type=BLE_GATT_HVX_NOTIFICATION;notification.p_len=&count;notification.p_data=bytes+offset;
            uint32_t err=sd_ble_gatts_hvx(connection,&notification);
            if(!err)offset+=count;else if(err!=NRF_ERROR_RESOURCES) {NRF_LOG_WARNING("Simulator notify %u",err);length=offset=0;}
        }
        NRF_LOG_PROCESS();sd_app_evt_wait();
    }
}
