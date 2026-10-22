// SPDX-License-Identifier: GPL-3.0-or-later
#include "ble_bms_radio.h"
#include "ble_bms_wire.h"
#include "ble_db_discovery.h"
#include "ble_advdata.h"
#include "app_timer.h"
#include "app_util_platform.h"
#include "nrf_sdh_ble.h"
#include "nrf_soc.h"
#include "nrf_log.h"
#include "packet.h"
#include <string.h>

BLE_DB_DISCOVERY_DEF(discovery);
static uint16_t connection=BLE_CONN_HANDLE_INVALID,notify_handle,write_handle,cccd_handle;
static uint8_t protocol,state,without_response;
static uint8_t target_identity[8];
static bool want_discovery,subscribing,scan_active;
static uint8_t scan_bytes[BLE_GAP_SCAN_BUFFER_MIN];
static ble_data_t scan_buffer={scan_bytes,sizeof(scan_bytes)};
static const ble_gap_scan_params_t scan_params={.active=1,.interval=160,.window=80,.timeout=1000};
static uint32_t reset_reason,uart_errors,queue_drops,last_transition;
static uint8_t queue[16][BB_MAX_PACKET];static uint16_t lengths[16];static volatile unsigned head,tail;
static uint8_t scan_seen[16][7];static unsigned scan_seen_count;
static bool scan_named[16];

static uint32_t ms(void){
    uint32_t result;
    CRITICAL_REGION_ENTER();
    static uint32_t last,total;static uint32_t fraction;
    uint32_t ticks=app_timer_cnt_get();
    uint32_t delta=app_timer_cnt_diff_compute(ticks,last);last=ticks;
    uint64_t scaled=(uint64_t)delta*1000+fraction;
    total+=scaled/APP_TIMER_CLOCK_FREQ;fraction=scaled%APP_TIMER_CLOCK_FREQ;result=total;
    CRITICAL_REGION_EXIT();return result;
}
static void emit(uint8_t op,uint8_t seq,const uint8_t *p,unsigned n) {
    if(n>BB_MAX_DATA)return;
    CRITICAL_REGION_ENTER();
    unsigned next=(head+1)%16;
    if(next==tail){++queue_drops;}
    else {bb_header(queue[head],op,seq);if(n)memcpy(queue[head]+6,p,n);lengths[head]=n+6;head=next;}
    CRITICAL_REGION_EXIT();
}
static void set_state(uint8_t next,uint32_t reason) {
    state=next;last_transition=ms();
    uint8_t p[3]={state,(uint8_t)reason,without_response};emit(BB_STATE,0,p,3);
    NRF_LOG_INFO("BMS state=%u reason=%u uart_errors=%u drops=%u",state,reason,uart_errors,queue_drops);
}
static void fail(uint32_t error) {
    set_state(BB_ERROR,error);
    if(connection!=BLE_CONN_HANDLE_INVALID)sd_ble_gap_disconnect(connection,BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
}
static void db_event(ble_db_discovery_evt_t *e) {
    if(e->conn_handle!=connection)return;
    uint16_t service=protocol==1?0xff00:protocol==2?0xffe0:0xfff0;
    if(e->evt_type==BLE_DB_DISCOVERY_ERROR){fail(e->params.err_code);return;}
    if(e->evt_type!=BLE_DB_DISCOVERY_COMPLETE || e->params.discovered_db.srv_uuid.uuid!=service)return;
    ble_gatt_db_srv_t *db=&e->params.discovered_db;
    for(unsigned i=0;i<db->char_count;i++) {
        ble_gatt_db_char_t *c=&db->charateristics[i];uint16_t uuid=c->characteristic.uuid.uuid;
        if(uuid==(protocol==1?0xff01:protocol==2?0xffe1:0xfff1)) {
            notify_handle=c->characteristic.handle_value;cccd_handle=c->cccd_handle;
        }
        if(uuid==(protocol==1?0xff02:protocol==2?0xffe1:0xfff2)) {
            write_handle=c->characteristic.handle_value;
            without_response=c->characteristic.char_props.write_wo_resp;
            if(!without_response && !c->characteristic.char_props.write)write_handle=0;
        }
    }
    if(!notify_handle || !write_handle || !cccd_handle){fail(NRF_ERROR_NOT_FOUND);return;}
    subscribing=true;
}
void ble_bms_radio_init(void) {
    // POWER is protected after SoftDevice enable; direct writes cause MEMACC.
    APP_ERROR_CHECK(sd_power_reset_reason_get(&reset_reason));
    APP_ERROR_CHECK(sd_power_reset_reason_clr(reset_reason));
    APP_ERROR_CHECK(ble_db_discovery_init(db_event));
    for(unsigned i=0;i<3;i++) {
        ble_uuid_t uuid={i==0?0xff00:i==1?0xffe0:0xfff0,BLE_UUID_TYPE_BLE};
        APP_ERROR_CHECK(ble_db_discovery_evt_register(&uuid));
    }
    NRF_LOG_INFO("BLE BMS wire v1 nRF52840 reset=0x%x",reset_reason);
}
void ble_bms_radio_uart_error(uint32_t error){++uart_errors;NRF_LOG_WARNING("UART error %u",error);}
bool ble_bms_radio_packet(const uint8_t *p,unsigned n) {
    if(!bb_namespace(p,n))return false;
    // Controller replies to QML travel through this same UART. Only consume
    // radio control operations; forward QML replies to the peripheral normally.
    if(n>=BB_HEADER && (p[4]<BB_HELLO || p[4]>BB_WRITE))return false;
    if(!bb_valid(p,n))return true;
    uint8_t op=p[4],seq=p[5];const uint8_t *d=p+6;n-=6;
    uint32_t err=NRF_SUCCESS;
    if(op==BB_HELLO && !n) {
        uint8_t reply[25]={0,1,3,reset_reason>>24,reset_reason>>16,reset_reason>>8,reset_reason,state,without_response,
            uart_errors>>24,uart_errors>>16,uart_errors>>8,uart_errors,
            queue_drops>>24,queue_drops>>16,queue_drops>>8,queue_drops};
        memcpy(reply+17,target_identity,8);
        emit(op|BB_ACK,seq,reply,sizeof(reply));return true;
    } else if(op==BB_SCAN && !n) {
        scan_seen_count=0;
        if(scan_active)sd_ble_gap_scan_stop();
        scan_buffer.len=sizeof(scan_bytes);err=sd_ble_gap_scan_start(&scan_params,&scan_buffer);
        if(!err){scan_active=true;if(connection==BLE_CONN_HANDLE_INVALID)set_state(BB_SCANNING,0);}
    } else if(op==BB_STOP && !n) {
        err=sd_ble_gap_scan_stop();scan_active=false;
        if(err==NRF_ERROR_INVALID_STATE)err=0;
        if(connection==BLE_CONN_HANDLE_INVALID)set_state(BB_IDLE,0);
    } else if(op==BB_CONNECT && n==8 && d[0]>=1 && d[0]<=3 && d[1]<=1) {
        if(connection!=BLE_CONN_HANDLE_INVALID || state==BB_CONNECTING)err=NRF_ERROR_BUSY;
        else {
            sd_ble_gap_scan_stop();scan_active=false;protocol=d[0];
            ble_gap_addr_t addr={0};addr.addr_type=d[1];memcpy(addr.addr,d+2,6);
            ble_gap_conn_params_t params={MSEC_TO_UNITS(30,UNIT_1_25_MS),MSEC_TO_UNITS(60,UNIT_1_25_MS),0,MSEC_TO_UNITS(4000,UNIT_10_MS)};
            err=sd_ble_gap_connect(&addr,&scan_params,&params,1);
            if(!err){memcpy(target_identity,d,8);set_state(BB_CONNECTING,0);}
        }
    } else if(op==BB_DISCONNECT && !n) {
        sd_ble_gap_scan_stop();scan_active=false;
        sd_ble_gap_connect_cancel();want_discovery=false;subscribing=false;
        if(connection!=BLE_CONN_HANDLE_INVALID)err=sd_ble_gap_disconnect(connection,BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        else set_state(BB_IDLE,0);
    } else if(op==BB_WRITE && n>1 && state==BB_ACTIVE) {
        ble_gattc_write_params_t params={0};params.handle=write_handle;
        params.write_op=without_response?BLE_GATT_OP_WRITE_CMD:BLE_GATT_OP_WRITE_REQ;
        params.len=n-1;params.p_value=(uint8_t*)d+1;
        err=sd_ble_gattc_write(connection,&params);
    } else err=NRF_ERROR_INVALID_PARAM;
    uint8_t result=(uint8_t)err;emit(op|BB_ACK,seq,&result,1);return true;
}
bool ble_bms_radio_event(ble_evt_t const *e) {
    uint16_t id=e->header.evt_id;
    if(id==BLE_GAP_EVT_ADV_REPORT && scan_active) {
        ble_gap_evt_adv_report_t const *r=&e->evt.gap_evt.params.adv_report;
        uint8_t p[32]={0};p[0]=r->peer_addr.addr_type;memcpy(p+1,r->peer_addr.addr,6);p[7]=(uint8_t)r->rssi;
        unsigned offset=0,name=0;
        while(offset<r->data.len) {
            unsigned length=r->data.p_data[offset];
            if(!length || offset+length>=r->data.len)break;
            uint8_t type=r->data.p_data[offset+1];
            if(type==BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME || type==BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME) {
                name=length-1;if(name>24)name=24;memcpy(p+8,r->data.p_data+offset+2,name);break;
            }
            offset+=length+1;
        }
        unsigned at=0;while(at<scan_seen_count && memcmp(scan_seen[at],p,7))++at;
        if(p[0]<=1 && at==scan_seen_count && at<16){
            memcpy(scan_seen[scan_seen_count],p,7);scan_named[scan_seen_count++]=name!=0;emit(BB_DEVICE,0,p,8+name);
        } else if(at<scan_seen_count && name && !scan_named[at]) {
            scan_named[at]=true;emit(BB_DEVICE,0,p,8+name); // Name often arrives in a later scan response.
        }
        scan_buffer.len=sizeof(scan_bytes);uint32_t err=sd_ble_gap_scan_start(NULL,&scan_buffer);
        if(err){scan_active=false;NRF_LOG_WARNING("Scan resume %u",err);}return true;
    }
    if(id==BLE_GAP_EVT_TIMEOUT) {
        if(e->evt.gap_evt.params.timeout.src==BLE_GAP_TIMEOUT_SRC_SCAN){scan_active=false;if(state==BB_SCANNING)set_state(BB_IDLE,0);return true;}
        if(e->evt.gap_evt.params.timeout.src==BLE_GAP_TIMEOUT_SRC_CONN && state==BB_CONNECTING){set_state(BB_ERROR,NRF_ERROR_TIMEOUT);return true;}
    }
    if(id==BLE_GAP_EVT_CONNECTED && e->evt.gap_evt.params.connected.role==BLE_GAP_ROLE_CENTRAL) {
        connection=e->evt.gap_evt.conn_handle;notify_handle=write_handle=cccd_handle=0;
        memset(&discovery,0,sizeof(discovery));discovery.conn_handle=BLE_CONN_HANDLE_INVALID;
        want_discovery=true;set_state(BB_DISCOVERING,0);return true;
    }
    uint16_t handle=BLE_CONN_HANDLE_INVALID;
    if(id>=BLE_GAP_EVT_BASE && id<=BLE_GAP_EVT_LAST)handle=e->evt.gap_evt.conn_handle;
    else if(id>=BLE_GATTC_EVT_BASE && id<=BLE_GATTC_EVT_LAST)handle=e->evt.gattc_evt.conn_handle;
    else if(id>=BLE_GATTS_EVT_BASE && id<=BLE_GATTS_EVT_LAST)handle=e->evt.gatts_evt.conn_handle;
    if(connection==BLE_CONN_HANDLE_INVALID || handle!=connection)return false;
    if(id==BLE_GAP_EVT_DISCONNECTED) {
        uint8_t reason=e->evt.gap_evt.params.disconnected.reason;connection=BLE_CONN_HANDLE_INVALID;
        want_discovery=subscribing=false;set_state(BB_IDLE,reason);
    } else if(id==BLE_GATTC_EVT_HVX && e->evt.gattc_evt.params.hvx.handle==notify_handle) {
        ble_gattc_evt_hvx_t const *v=&e->evt.gattc_evt.params.hvx;
        if(v->len<=BB_MAX_DATA)emit(BB_NOTIFY,0,v->data,v->len);else fail(NRF_ERROR_DATA_SIZE);
        if(v->type==BLE_GATT_HVX_INDICATION)sd_ble_gattc_hv_confirm(connection,v->handle);
    } else if(id==BLE_GATTC_EVT_WRITE_RSP) {
        if(e->evt.gattc_evt.gatt_status!=BLE_GATT_STATUS_SUCCESS)fail(e->evt.gattc_evt.gatt_status);
        else if(e->evt.gattc_evt.params.write_rsp.handle==cccd_handle)set_state(BB_ACTIVE,0);
    } else if(id==BLE_GATTC_EVT_TIMEOUT)fail(NRF_ERROR_TIMEOUT);
    else if(id==BLE_GAP_EVT_SEC_PARAMS_REQUEST)sd_ble_gap_sec_params_reply(connection,BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP,NULL,NULL);
    else if(id==BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST)sd_ble_gap_conn_param_update(connection,&e->evt.gap_evt.params.conn_param_update_request.conn_params);
    else if(id==BLE_GAP_EVT_PHY_UPDATE_REQUEST) {
        ble_gap_phys_t phys={BLE_GAP_PHY_AUTO,BLE_GAP_PHY_AUTO};sd_ble_gap_phy_update(connection,&phys);
    }
    return true;
}
void ble_bms_radio_tick(void) {
    (void)ms();
    if(want_discovery) {
        uint32_t err=ble_db_discovery_start(&discovery,connection);
        if(!err)want_discovery=false;else if(err!=NRF_ERROR_BUSY && err!=NRF_ERROR_INVALID_STATE)fail(err);
    }
    if(subscribing) {
        uint8_t value[2]={1,0};ble_gattc_write_params_t params={0};
        params.write_op=BLE_GATT_OP_WRITE_REQ;params.handle=cccd_handle;params.len=2;params.p_value=value;
        uint32_t err=sd_ble_gattc_write(connection,&params);
        if(!err)subscribing=false;else if(err!=NRF_ERROR_BUSY)fail(err);
    }
    if(state==BB_DISCOVERING && ms()-last_transition>10000)fail(NRF_ERROR_TIMEOUT);
    uint8_t p[BB_MAX_PACKET];uint16_t n=0;
    CRITICAL_REGION_ENTER();
    if(tail!=head){n=lengths[tail];memcpy(p,queue[tail],n);tail=(tail+1)%16;}
    CRITICAL_REGION_EXIT();
    if(n){CRITICAL_REGION_ENTER();packet_send_packet(p,n,0);CRITICAL_REGION_EXIT();}
}
