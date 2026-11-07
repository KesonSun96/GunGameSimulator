/* Exercise the production adapter with the actual SDK USB declarations.
 * Only peripheral operations and time are replaced. No ARM hardware required. */
#include <assert.h>
#include <stdio.h>
#include "../firmware/pedal/src/ports/libopencm3/target.c"
static uint64_t clock_us;
static uint16_t write_result=8;
static bool attached;
const usbd_driver *const board_usb_driver=0;
void board_init(void) {}
uint64_t board_now_us(void) { return clock_us; }
uint32_t board_pressed(void) { return GG_INPUT_BIT(GG_INPUT_W); }
void board_indicator(bool on) { (void)on; }
void board_usb_attach(bool on) { attached=on; }
usbd_device *usbd_init(const usbd_driver *d,const struct usb_device_descriptor *a,
 const struct usb_config_descriptor *b,const char *const *c,int n,uint8_t *buf,uint16_t len)
{ (void)d;(void)a;(void)b;(void)c;(void)n;(void)buf;(void)len; return (usbd_device *)&clock_us; }
void usbd_register_reset_callback(usbd_device *d,void(*cb)(void)) {(void)d;(void)cb;}
void usbd_register_suspend_callback(usbd_device *d,void(*cb)(void)) {(void)d;(void)cb;}
void usbd_register_resume_callback(usbd_device *d,void(*cb)(void)) {(void)d;(void)cb;}
int usbd_register_set_config_callback(usbd_device *d,usbd_set_config_callback cb)
{(void)d;(void)cb;return 0;}
int usbd_register_control_callback(usbd_device *d,uint8_t t,uint8_t m,usbd_control_callback cb)
{(void)d;(void)t;(void)m;(void)cb;return 0;}
void usbd_ep_setup(usbd_device *d,uint8_t a,uint8_t t,uint16_t s,void(*cb)(usbd_device*,uint8_t))
{(void)d;(void)a;(void)t;(void)s;(void)cb;}
uint16_t usbd_ep_write_packet(usbd_device *d,uint8_t a,const void *b,uint16_t len)
{(void)d;(void)b;assert(a==0x81 && len==8);return write_result;}
void usbd_poll(usbd_device *d) {(void)d;}
static void drain(void) { gg_transport_event_t e; while(poll_event(0,&e)) {} }
int main(void)
{
 gg_hal_t hal=pedal_target_hal();
 assert(hal.init(0));
 pump(); assert(!usb && !attached);
 clock_us=100000; pump(); assert(usb && attached);
 resumed(); assert(!link.connected); configured(usb,1); assert(link.connected); drain();
 gg_hid_report_t r={.itf=GG_HID_ITF_KEYBOARD,.len=8,.session=link.session,
                    .submit_id=42,.data={0,0,26}};
 assert(submit(0,&r));
 assert(!count); /* Submit is not completion. */
 r.data[2]=22;
 assert(!submit(0,&r)); /* Endpoint ownership is exclusive. */
 complete(usb,1);
 gg_transport_event_t e;
 assert(poll_event(0,&e) && e.type==GG_TRANSPORT_COMPLETE && e.submit_id==42);
 assert(e.itf==GG_HID_ITF_KEYBOARD && last_report[2]==26); /* Copy before return. */
 write_result=0; assert(!submit(0,&r)); assert(!link.slots[1].active);
 write_result=8; assert(submit(0,&r));
 uint32_t old_session=r.session;
 suspended(); assert(!submit(0,&r));
 resumed(); assert(link.session!=old_session);
 complete(usb,1);
 assert(poll_event(0,&e) && e.type==GG_TRANSPORT_SUSPEND);
 assert(poll_event(0,&e) && e.type==GG_TRANSPORT_RESUME);
 assert(poll_event(0,&e) && e.session==old_session && e.submit_id==42);
 assert(!submit(0,&r)); /* Reject stale sessions. */
 r.session=link.session; assert(submit(0,&r));
 clock_us+=100001; pump(); assert(restart);
 pump(); assert(!attached && !usb && !link.connected);
 clock_us+=100000; pump(); configured(usb,1); drain();
 r.session=link.session;
 uint8_t *buf=control_buffer; uint16_t len=128;
 struct usb_setup_data req={.bmRequestType=0x81,.bRequest=USB_REQ_GET_DESCRIPTOR,
                            .wValue=0x2200,.wIndex=0,.wLength=128};
 assert(control(usb,&req,&buf,&len,0)==USBD_REQ_HANDLED);
 assert(len==sizeof(report_descriptor) && buf==report_descriptor);
 req.wIndex=1; assert(control(usb,&req,&buf,&len,0)==USBD_REQ_NEXT_CALLBACK);
 req=(struct usb_setup_data){.bmRequestType=0x21,.bRequest=USB_HID_REQ_TYPE_SET_IDLE,.wValue=0x0100};
 assert(control(usb,&req,&buf,&len,0)==USBD_REQ_HANDLED && idle_rate==1);
 clock_us+=4000; pump(); assert(idle_busy);
 assert(!submit(0,&r));
 complete(usb,1); assert(!count && !idle_busy);
 assert(submit(0,&r)); complete(usb,1); drain();
 for(unsigned i=0;i<33;i++) emit(GG_TRANSPORT_CONNECT,0);
 assert(count==32 && overflows==1 && restart);
 drain(); pump(); assert(!usb && !link.connected);
 puts("STM32 USB adapter lifecycle/control regression tests passed.");
 return 0;
}

