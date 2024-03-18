#include "mgos_easydriver.h"

#include <string.h>

#include "mgos.h"
#include "mgos_rpc.h"
#include "mgos_gpio.h"
#include "mgos_pwm.h"

#include "driver/pcnt.h"
#include "driver/ledc.h"

ed_t *test_jig_ed;
static uint32_t start_frequency = 50;

static const char *freq_fmt = "{freq:%i}";
static const char *step_fmt = "{steps:%i}";
static const char *dir_fmt = "{dir:%B}";

void cleanup_mbuf(struct mbuf *buffer)
{
  if (buffer != NULL)
    mbuf_free(buffer);
}

static void notify_listeners(void)
{
  struct mg_mgr *mgr = mgos_get_mgr(); 
  struct mbuf response_buffer __attribute__((__cleanup__(cleanup_mbuf)));
  mbuf_init(&response_buffer, 1024);

  struct json_out json_result = JSON_OUT_MBUF(&response_buffer);
  // mbuf_init(buffer, 1024);
  json_printf(&json_result,
              "{"
                "moving: %B"
              "}",
              test_jig_ed->is_moving(test_jig_ed)
              );
    // WS notify
  for (struct mg_connection *c = mgr->active_connections; c != NULL; c = mg_next(mgr, c))
  {
    if((c->flags & MG_F_IS_WEBSOCKET) == 0) continue;
    mg_send_websocket_frame(c, WEBSOCKET_OP_TEXT | WEBSOCKET_OP_CONTINUE, response_buffer.buf, response_buffer.len);
  }
}

static void rpc_motor_handler(struct mg_rpc_request_info *ri, void *cb_arg UNUSED_ARG,
                              struct mg_rpc_frame_info *fi, struct mg_str args)
{
  struct mbuf response_buffer __attribute__((__cleanup__(cleanup_mbuf)));
  mbuf_init(&response_buffer, 1024);

  struct json_out json_result = JSON_OUT_MBUF(&response_buffer);

  if (strncasecmp("Motor.Status", ri->method.p, ri->method.len) == 0)
  {
    json_printf(&json_result,
                "{"
                  "enabled: %B,"
                  "moving: %B,"
                  "dir: \"%s\","
                  "freq: %d"
                "}",
                test_jig_ed->is_enabled(test_jig_ed),
                test_jig_ed->is_moving(test_jig_ed),
                test_jig_ed->get_direction(test_jig_ed) == ED_DIR_FORWARD ? "fwd" : "back",
                test_jig_ed->get_frequency(test_jig_ed)
                );
    mg_rpc_send_responsef(ri, "%.*s", response_buffer.len, response_buffer.buf);
    return;
  }
  if (strncasecmp("Motor.Enable", ri->method.p, ri->method.len) == 0)
  {
    test_jig_ed->enable(test_jig_ed, true);
    mg_rpc_send_responsef(ri, NULL);
    return;
  }
  if (strncasecmp("Motor.Disable", ri->method.p, ri->method.len) == 0)
  {
    test_jig_ed->enable(test_jig_ed, false);
    mg_rpc_send_responsef(ri, NULL);
    return;
  }
  if (strncasecmp("Motor.Start", ri->method.p, ri->method.len) == 0)
  {
    test_jig_ed->start(test_jig_ed);
    mg_rpc_send_responsef(ri, NULL);
    return;
  }
  if (strncasecmp("Motor.Stop", ri->method.p, ri->method.len) == 0)
  {
    test_jig_ed->stop(test_jig_ed);
    mg_rpc_send_responsef(ri, NULL);
    return;
  }
  if (strncasecmp("Motor.DirectionFwd", ri->method.p, ri->method.len) == 0)
  {
    test_jig_ed->set_direction(test_jig_ed, ED_DIR_FORWARD);
    mg_rpc_send_responsef(ri, NULL);
    return;
  }
  if (strncasecmp("Motor.DirectionBack", ri->method.p, ri->method.len) == 0)
  {
    test_jig_ed->set_direction(test_jig_ed, ED_DIR_BACKWARDS);
    mg_rpc_send_responsef(ri, NULL);
    return;
  }
  if (strncasecmp("Motor.SetFrequency", ri->method.p, ri->method.len) == 0)
  {
    int new_frequency = -1;
    if (json_scanf(args.p, args.len, ri->args_fmt, &new_frequency) == 1)
    {
      if (!test_jig_ed->set_frequency(test_jig_ed, new_frequency))
      {
        mg_rpc_send_errorf(ri, 500, "Invalid value for frequency [%d..%d]", ED_MIN_FREQUENCY, ED_MAX_FREQUENCY);
      }
      else
      {
        new_frequency = test_jig_ed->get_frequency(test_jig_ed);
        mgos_sys_config_set_easydriver_freq(new_frequency);
        LOG(LL_INFO, ("New frequency %d", new_frequency));
        mg_rpc_send_responsef(ri, "{freq:%d}", new_frequency);
      }
    }
    return;
  }
  if (strncasecmp("Motor.Step", ri->method.p, ri->method.len) == 0)
  {
    int steps = -1;
    if (json_scanf(args.p, args.len, ri->args_fmt, &steps) == 1)
    {
      if (!test_jig_ed->step(test_jig_ed, steps))
      {
        mg_rpc_send_errorf(ri, 500, "Can not step. Maybe still moving?");
      }
      else
      {
        LOG(LL_INFO, ("Steps %d", steps));
        mg_rpc_send_responsef(ri, "{steps:%d}", steps);
      }
    }
    return;
  }

  mg_rpc_send_errorf(ri, 500, "Method not implemented");
}

static void easydriver_cb(int ev, void *evd, void *user_data UNUSED_ARG)
{
  notify_listeners();
}

enum mgos_app_init_result mgos_app_init(void)
{
  LOG(LL_INFO, ("Starting stepper motor test jig"));

  int enable_pin = -1;
  int dir_pin = -1;
  int step_pin = -1;
  int ms1_pin = -1;
  int ms2_pin = -1;

  enable_pin = mgos_sys_config_get_easydriver_enable_pin();
  dir_pin = mgos_sys_config_get_easydriver_dir_pin();
  step_pin = mgos_sys_config_get_easydriver_step_pin();
  ms1_pin = mgos_sys_config_get_easydriver_ms1_pin();
  ms2_pin = mgos_sys_config_get_easydriver_ms2_pin();

  start_frequency = mgos_sys_config_get_easydriver_freq();

  if (enable_pin < 0 || dir_pin < 0 || step_pin < 0)
  {
    return MGOS_APP_INIT_ERROR;
  }

  ed_config_t ed_config = ED_DEFAULT_CONFIG(enable_pin, dir_pin, step_pin, LEDC_TIMER_1, LEDC_CHANNEL_1, PCNT_UNIT_0);
  ed_config.ms1_pin = ms1_pin;
  ed_config.ms2_pin = ms2_pin;

  if (!mgos_easydriver_ctor(&ed_config, &test_jig_ed))
  {
    return MGOS_APP_INIT_ERROR;
  };

  test_jig_ed->set_frequency(test_jig_ed, start_frequency);
  test_jig_ed->enable(test_jig_ed, true);
  test_jig_ed->start(test_jig_ed);

  // Set the rpc methods for this application
  struct mg_rpc *c = mgos_rpc_get_global();
  mg_rpc_add_handler(c, "Motor.Status",
                     "{}", rpc_motor_handler, NULL);
  mg_rpc_add_handler(c, "Motor.Start",
                     "{}", rpc_motor_handler, NULL);
  mg_rpc_add_handler(c, "Motor.Stop",
                     "{}", rpc_motor_handler, NULL);
  mg_rpc_add_handler(c, "Motor.Enable",
                     "{}", rpc_motor_handler, NULL);
  mg_rpc_add_handler(c, "Motor.Disable",
                     "{}", rpc_motor_handler, NULL);
  mg_rpc_add_handler(c, "Motor.DirectionFwd",
                     "{}", rpc_motor_handler, NULL);
  mg_rpc_add_handler(c, "Motor.DirectionBack",
                     "{}", rpc_motor_handler, NULL);
  mg_rpc_add_handler(c, "Motor.SetFrequency",
                     freq_fmt, rpc_motor_handler, NULL);
  mg_rpc_add_handler(c, "Motor.Step",
                     step_fmt, rpc_motor_handler, NULL);

  mgos_event_add_group_handler(EASYDRIVER_EVENT_BASE, easydriver_cb, NULL);

  return MGOS_APP_INIT_SUCCESS;
}