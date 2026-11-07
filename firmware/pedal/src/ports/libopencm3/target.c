#include "board.h"
#include "pedal_target.h"
#include "gg_usb_link.h"
#include <libopencm3/usb/hid.h>
#include <string.h>

/* Foreground owns USB, events and HAL. Only the board's clock uses an ISR. */
static usbd_device *usb;
static gg_usb_link_t link;
static gg_transport_event_t events[32];
static unsigned head, count;
static uint32_t overflows;
static bool restart, idle_busy, configured_active, sleeping;
static uint64_t attach_at, last_sent, idle_started;
static uint8_t last_report[8], idle_rate, protocol = 1, leds;
static uint8_t control_buffer[128];

static const uint8_t report_descriptor[] = {
 0x05,0x01,0x09,0x06,0xa1,0x01,
 0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,
 0x75,0x01,0x95,0x08,0x81,0x02,
 0x95,0x01,0x75,0x08,0x81,0x01,
 0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,
 0x95,0x01,0x75,0x03,0x91,0x01,
 0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,
 0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xc0
};
static const uint8_t hid_descriptor[] = {
 9,0x21,0x11,0x01,0,1,0x22,sizeof(report_descriptor),0
};
static const struct usb_endpoint_descriptor endpoint = {
 .bLength=USB_DT_ENDPOINT_SIZE,.bDescriptorType=USB_DT_ENDPOINT,
 .bEndpointAddress=0x81,.bmAttributes=USB_ENDPOINT_ATTR_INTERRUPT,
 .wMaxPacketSize=8,.bInterval=1
};
static const struct usb_interface_descriptor interface_descriptor = {
 .bLength=USB_DT_INTERFACE_SIZE,.bDescriptorType=USB_DT_INTERFACE,
 .bInterfaceNumber=0,.bNumEndpoints=1,.bInterfaceClass=USB_CLASS_HID,
 .bInterfaceSubClass=1,.bInterfaceProtocol=1,
 .endpoint=&endpoint,.extra=hid_descriptor,.extralen=sizeof(hid_descriptor)
};
static const struct usb_interface interfaces[] = {
 {.num_altsetting=1,.altsetting=&interface_descriptor}
};
static const struct usb_config_descriptor config_descriptor = {
 .bLength=USB_DT_CONFIGURATION_SIZE,.bDescriptorType=USB_DT_CONFIGURATION,
 .wTotalLength=34,.bNumInterfaces=1,.bConfigurationValue=1,
 .bmAttributes=0x80,.bMaxPower=50,.interface=interfaces
};
/* pid.codes private-test identity. Assign your own VID/PID before distribution. */
static const struct usb_device_descriptor device_descriptor = {
 .bLength=USB_DT_DEVICE_SIZE,.bDescriptorType=USB_DT_DEVICE,.bcdUSB=0x0200,
 .bMaxPacketSize0=64,.idVendor=0x1209,.idProduct=0x0001,.bcdDevice=0x0100,
 .iManufacturer=1,.iProduct=2,.bNumConfigurations=1
};
static const char *const strings[] = {"GunGameSimulator","Portable WASD Pedal"};

static void emit(gg_transport_event_type_t type, const gg_hid_report_t *report)
{
 if(count==32) { overflows++; restart=true; return; }
 events[(head+count)%32]=(gg_transport_event_t){
  .type=type,.session=report?report->session:link.session,
  .itf=report?report->itf:GG_HID_ITF_KEYBOARD,
  .submit_id=report?report->submit_id:0
 };
 count++;
}
static void disconnected(void)
{
 configured_active=false; sleeping=false; idle_rate=0; protocol=1; leds=0;
 gg_usb_link_disconnect(&link);
 idle_busy=false;
 memset(last_report,0,sizeof(last_report));
 emit(GG_TRANSPORT_DISCONNECT,0);
}
static void suspended(void)
{
 if(!configured_active || sleeping) return;
 sleeping=true;
 gg_usb_link_suspend(&link);
 emit(GG_TRANSPORT_SUSPEND,0);
}
static void resumed(void)
{
 if(!configured_active || !sleeping) return;
 sleeping=false;
 gg_usb_link_resume(&link);
 emit(GG_TRANSPORT_RESUME,0);
}
static void complete(usbd_device *dev, uint8_t ep)
{
 (void)dev; (void)ep;
 if(idle_busy) { idle_busy=false; last_sent=board_now_us(); return; }
 gg_hid_report_t report;
 if(gg_usb_link_finish(&link,GG_HID_ITF_KEYBOARD,&report)) {
  memcpy(last_report,report.data,8);
  last_sent=board_now_us();
  emit(GG_TRANSPORT_COMPLETE,&report);
 }
}
static enum usbd_request_return_codes control(usbd_device *dev,
 struct usb_setup_data *req,uint8_t **buf,uint16_t *len,
 usbd_control_complete_callback *done)
{
 (void)dev; (void)done;
 if(req->wIndex!=0) return USBD_REQ_NEXT_CALLBACK;
 const uint8_t *data=0;
 uint16_t size=0;
 if(req->bmRequestType==0x81 && req->bRequest==USB_REQ_GET_DESCRIPTOR) {
  if(req->wValue==0x2200) { data=report_descriptor; size=sizeof(report_descriptor); }
  else if(req->wValue==0x2100) { data=hid_descriptor; size=sizeof(hid_descriptor); }
  else return USBD_REQ_NEXT_CALLBACK;
 } else if(req->bmRequestType==0xa1) {
  switch(req->bRequest) {
   case USB_HID_REQ_TYPE_GET_REPORT:
    if(req->wValue!=0x0100) return USBD_REQ_NOTSUPP;
    data=last_report; size=8; break;
   case USB_HID_REQ_TYPE_GET_IDLE:
    if(req->wValue!=0) return USBD_REQ_NOTSUPP;
    data=&idle_rate; size=1; break;
   case USB_HID_REQ_TYPE_GET_PROTOCOL:
    if(req->wValue!=0) return USBD_REQ_NOTSUPP;
    data=&protocol; size=1; break;
   default: return USBD_REQ_NOTSUPP;
  }
 } else if(req->bmRequestType==0x21) {
  switch(req->bRequest) {
   case USB_HID_REQ_TYPE_SET_IDLE:
    if((req->wValue&255)!=0 || req->wLength!=0) return USBD_REQ_NOTSUPP;
    idle_rate=req->wValue>>8; last_sent=board_now_us(); break;
   case USB_HID_REQ_TYPE_SET_PROTOCOL:
    if(req->wValue>1 || req->wLength!=0) return USBD_REQ_NOTSUPP;
    protocol=req->wValue; break;
   case USB_HID_REQ_TYPE_SET_REPORT:
    if(req->wValue!=0x0200 || req->wLength!=1 || *len!=1) return USBD_REQ_NOTSUPP;
    leds=(*buf)[0]; break;
   default: return USBD_REQ_NOTSUPP;
  }
  *len=0; return USBD_REQ_HANDLED;
 } else return USBD_REQ_NEXT_CALLBACK;
 *buf=(uint8_t *)data;
 if(*len>size) *len=size;
 return USBD_REQ_HANDLED;
}
static void configured(usbd_device *dev,uint16_t value)
{
 /* libopencm3 clears control handlers and endpoints on SET_CONFIGURATION. */
 usbd_register_control_callback(dev,0x01,0x1f,control);
 disconnected();
 idle_rate=0; protocol=1; leds=0;
 if(value==1) {
  configured_active=true;
  usbd_ep_setup(dev,0x81,USB_ENDPOINT_ATTR_INTERRUPT,8,complete);
  gg_usb_link_connect(&link);
  last_sent=board_now_us();
  emit(GG_TRANSPORT_CONNECT,0);
 }
}
static void pump(void)
{
 uint64_t now=board_now_us();
 if(restart) {
  restart=false;
  board_usb_attach(false);
  usb=0; disconnected(); attach_at=now+100000;
 }
 if(!usb && now>=attach_at) {
  usb=usbd_init(board_usb_driver,&device_descriptor,&config_descriptor,
                strings,2,control_buffer,sizeof(control_buffer));
  usbd_register_reset_callback(usb,disconnected);
  usbd_register_suspend_callback(usb,suspended);
  usbd_register_resume_callback(usb,resumed);
  usbd_register_set_config_callback(usb,configured);
  usbd_register_control_callback(usb,0x01,0x1f,control);
  board_usb_attach(true);
 }
 if(!usb) return;
 usbd_poll(usb);
 if(!link.connected) return;
 if(gg_usb_link_expired(&link,now) || (idle_busy && now-idle_started>=100000)) {
  restart=true; return;
 }
 if(idle_rate && !idle_busy && !link.slots[GG_HID_ITF_KEYBOARD].active &&
    now-last_sent>=(uint64_t)idle_rate*4000) {
  if(usbd_ep_write_packet(usb,0x81,last_report,8)==8) {
   idle_busy=true; idle_started=now;
  }
 }
}
static bool init(void *ctx)
{
 (void)ctx;
 board_init();
 board_usb_attach(false);
 attach_at=board_now_us()+100000;
 return true;
}
static uint64_t now_us(void *ctx) { (void)ctx; return board_now_us(); }
static void wait_next(void *ctx,uint32_t period)
{
 (void)ctx;
 const uint64_t until=board_now_us()+period;
 do { pump(); } while(board_now_us()<until);
}
static bool inputs(void *ctx,uint32_t *mask)
{ (void)ctx; *mask=board_pressed(); return true; }
static bool poll_event(void *ctx,gg_transport_event_t *event)
{
 (void)ctx;
 if(!count) return false;
 *event=events[head]; head=(head+1)%32; count--; return true;
}
static uint32_t overflow_count(void *ctx) { (void)ctx; return overflows; }
static bool submit(void *ctx,const gg_hid_report_t *report)
{
 (void)ctx;
 if(!report || !usb || idle_busy || report->itf!=GG_HID_ITF_KEYBOARD || report->len!=8 ||
    !gg_usb_link_begin(&link,report,board_now_us())) return false;
 if(usbd_ep_write_packet(usb,0x81,report->data,8)==8) return true;
 gg_hid_report_t rejected;
 gg_usb_link_finish(&link,GG_HID_ITF_KEYBOARD,&rejected);
 return false;
}
static void reconnect(void *ctx) { (void)ctx; restart=true; }
static void indicator(void *ctx,bool on) { (void)ctx; board_indicator(on); }
gg_hal_t pedal_target_hal(void)
{
 return (gg_hal_t){
  .abi_version=GG_HAL_ABI_VERSION,.init=init,.now_us=now_us,.wait_next=wait_next,
  .read_inputs=inputs,.poll_event=poll_event,.event_overflows=overflow_count,
  .submit_report=submit,.reconnect=reconnect,.set_indicator=indicator
 };
}


