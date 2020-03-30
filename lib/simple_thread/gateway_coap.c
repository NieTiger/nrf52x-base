#include <stdio.h>
#include <string.h>

#include "gateway_coap.h"
#include "thread_coap.h"
#include "thread_dns.h"

#include <openthread/message.h>

#include "custom_board.h"

#ifdef RTC_CS
#include "ab1815.h"
#endif

#include "pb_encode.h"

static uint32_t seq_no = 0;
static uint8_t* message_buf = NULL;
static block_finalize_cb callback;
extern uint8_t device_id[6];

static const otIp6Address unspecified_ipv6 =
{
    .mFields =
    {
        .m8 = {0}
    }
};

void gateway_block_finalize(uint8_t code, otError result) {
  free(message_buf);
  callback(code, result);
}

void gateway_response_handler (void* context, otMessage* message, const
                                 otMessageInfo* message_info, otError result) {
  if (result == OT_ERROR_NONE) {
    printf("got response!\n");
  }
}

otError gateway_coap_send(otIp6Address* dest_addr,
    const char* path, const char* device_type, bool confirmable, Message* msg) {
  if (otIp6IsAddressEqual(dest_addr, &unspecified_ipv6)) {
    return OT_ERROR_ADDRESS_QUERY;
  }

  otInstance * thread_instance = thread_get_instance();
  Header header = Header_init_default;
  header.version = GATEWAY_PACKET_VERSION;
  memcpy(header.id.bytes, device_id, sizeof(device_id));
  strncpy(header.device_type, device_type, sizeof(header.device_type));
  header.id.size = sizeof(device_id);
#ifdef RTC_CS
    struct timeval time = ab1815_get_time_unix();
    header.tv_sec = time.tv_sec;
    header.tv_usec = time.tv_usec;
#endif
  header.seq_no = seq_no;

  memcpy(&(msg->header), &header, sizeof(header));

  uint8_t packed_data [256];

  pb_ostream_t stream;
  stream = pb_ostream_from_buffer(packed_data, sizeof(packed_data));
  pb_encode(&stream, Message_fields, msg);
  size_t len = stream.bytes_written;
  APP_ERROR_CHECK_BOOL(len < 256);

  otCoapType coap_type = confirmable ? OT_COAP_TYPE_CONFIRMABLE : OT_COAP_TYPE_NON_CONFIRMABLE;

  otError error = thread_coap_send(thread_instance, OT_COAP_CODE_PUT, coap_type, dest_addr, path, packed_data, len, gateway_response_handler);

  // increment sequence number if successful
  if (error == OT_ERROR_NONE) {
    seq_no++;
  }

  return error;
}

otError gateway_coap_block_send(otIp6Address* dest_addr, block_info* b_info,
    const char* device_type, Message* msg, block_finalize_cb cb) {
  otInstance * thread_instance = thread_get_instance();
  callback = cb;

  Header header = Header_init_default;
  header.version = GATEWAY_PACKET_VERSION;
  memcpy(header.id.bytes, device_id, sizeof(device_id));
  strncpy(header.device_type, device_type, sizeof(header.device_type));
  header.id.size = sizeof(device_id);
#ifdef RTC_CS
  struct timeval time = ab1815_get_time_unix();
  header.tv_sec = time.tv_sec;
  header.tv_usec = time.tv_usec;
#endif
  header.seq_no = seq_no;

  memcpy(&(msg->header), &header, sizeof(header));

  pb_ostream_t stream;
  message_buf = malloc(b_info->data_len + 256);
  if(message_buf == NULL) {
    return NRF_ERROR_INVALID_PARAM;
  }
  stream = pb_ostream_from_buffer(message_buf, b_info->data_len + 256);
  pb_encode(&stream, Message_fields, msg);
  b_info->data_addr = message_buf;
  b_info->data_len = stream.bytes_written;
  b_info->callback = gateway_block_finalize;

  return start_blockwise_transfer(thread_instance, dest_addr, b_info, block_response_handler);
}
