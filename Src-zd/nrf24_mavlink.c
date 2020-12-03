#include <nrf24l01.h>
#include <entry.h>
#include <rtt_interface.h>
#include <mavlink.h>
#include <rc_def.h>

#define NRF24_DEMO_ROLE                 ROLE_PRX
#define NRF24_DEMO_SPI_DEV_NAME         "spi10"
#define NRF24_DEMO_CE_PIN               GET_PIN(A, 3)
#define NRF24_DEMO_IRQ_PIN              GET_PIN(C, 13)

extern uint8_t key_value;
extern char global_buf[4][16];

static rt_sem_t nrfirq_sem;
static uint8_t  cmd_cnt = 0;

uint32_t nrf24_timestamp;
ap_t     mav_data;
uint8_t  mode_changed;
int8_t   car_mode;

static void _irq_init(void);
static void _waitirq(void);
static void _nrf24_param_set(nrf24_cfg_t *pt);

vel_target vel={.vel_x = 0.0f, .vel_y = 0.0f, .rad_z = 0.0f, .ext_1 = 0.0f, .ext_2 = 0.0f, .ext_3 = 0};

void nrf24l01_mavlink_entry(void *param)
{
  struct hal_nrf24l01_port_cfg halcfg;
  nrf24_cfg_t cfg;
  int rlen;
  uint8_t tlen = 0;
  uint8_t rbuf[32 + 1];
  uint8_t tbuf[32] = "first\r\n";
  
  _irq_init();
  
  _nrf24_param_set(&cfg);
  halcfg.ce_pin = NRF24_DEMO_CE_PIN;
  halcfg.spi_device_name = NRF24_DEMO_SPI_DEV_NAME;
  cfg.role = ROLE_PRX;    // PRX
  cfg.ud = &halcfg;
  cfg.use_irq = 1;        // True
  nrf24_init(&cfg);
  
  while (1) {
    rlen = nrf24_irq_prx_run(rbuf, tbuf, tlen, _waitirq);
    if (rlen > 0) {       // received data (also indicating that the previous frame of data was sent complete)
      uint8_t i;
      mavlink_message_t msg_receive;
      mavlink_status_t mav_status;
      for(i=0; i<32; i++) {
        if(mavlink_parse_char(0, rbuf[i], &msg_receive, &mav_status)) {
          if(msg_receive.sysid != MAV_SYSID || msg_receive.compid != MVA_COMPID)
            break;
          switch (msg_receive.msgid) {
          case MAVLINK_MSG_ID_SIMPLE: {
            mavlink_simple_t packet;
            mavlink_msg_simple_decode(&msg_receive, &packet);
            
            nrf24_timestamp = HAL_GetTick();
            
            memcpy(&mav_data, &packet.data, sizeof(packet.data));
            
#if MAVLINK_VCOM_DEBUG == 1
            if(vcom != NULL){
              char buf[32];
              sprintf(buf, "%d\r\n", packet.data);
              rt_device_write(vcom, 0, buf, rt_strlen(buf));
            }
#endif
            
            mavlink_message_t msg_ack;
            
            if(key_value == 11 || cmd_cnt != 0){
              mavlink_msg_cmd_pack( MAV_SYSID, MVA_COMPID, &msg_ack, 1 );
              cmd_cnt++;
              sprintf(global_buf[2], "nrf24: cnt %d \r\n", cmd_cnt);
              if(cmd_cnt > 59){
                cmd_cnt = 0;
              }
              key_value = 0;
            } else if(mode_changed){
              mavlink_msg_mode_pack( MAV_SYSID, MVA_COMPID, &msg_ack, car_mode, 0);
              mode_changed = 0;
              key_value    = 0;
            } else {
              mavlink_msg_velocity_pack(MAV_SYSID, MVA_COMPID, &msg_ack, vel.vel_x, vel.vel_y, vel.rad_z, vel.ext_1, vel.ext_2, vel.ext_3);
            }
            tlen = mavlink_msg_to_send_buffer((uint8_t *)tbuf, &msg_ack);
            break;
          }
          }
        }
      }
    }
    rt_thread_delay(1);
  }
}

static void _irq_cb(void *param)
{
  rt_sem_release(nrfirq_sem);
}

static void _irq_init(void)
{
  nrfirq_sem = rt_sem_create("nrfIRQ", 0, RT_IPC_FLAG_FIFO);

  rt_pin_attach_irq(NRF24_DEMO_IRQ_PIN, PIN_IRQ_MODE_FALLING, _irq_cb, 0);
  rt_pin_irq_enable(NRF24_DEMO_IRQ_PIN, PIN_IRQ_ENABLE);
}

static void _waitirq(void)
{
  rt_sem_take(nrfirq_sem, RT_WAITING_FOREVER);
}

static void _nrf24_param_set(nrf24_cfg_t *pt)
{
  pt->power = RF_POWER_0dBm;
  pt->esb.ard = 5;        // (5+1)*250 = 1500us
  pt->esb.arc = 6;        // up to 6 times
  pt->crc = CRC_2_BYTE;   // crc; fcs is two bytes
  pt->adr = ADR_1Mbps;    // air data rate 1Mbps
  pt->channel = 6;        // rf channel 6

  pt->address[4] = 0x12;  // address is 0x123456789A
  pt->address[3] = 0x34;
  pt->address[2] = 0x56;
  pt->address[1] = 0x78;
  pt->address[0] = 0x9A;
}

static int nrf24l01_mavlink_init(void)
{
  rt_thread_t nrf_mav_thread;

  RTT_CREATE(nrf_mav, nrf24l01_mavlink_entry, RT_NULL, 1536 , 3, 20)

  return RT_EOK;
}

INIT_APP_EXPORT(nrf24l01_mavlink_init);
