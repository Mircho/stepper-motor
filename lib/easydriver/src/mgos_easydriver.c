#include "mgos_easydriver.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pcnt.h"

#ifdef MGOS_EVENT_BASE
#define EMIT_EVENT(et)  mgos_event_trigger(et, NULL)
#elif
#define EMIT_EVENT(et)
#endif

static const uint16_t     INIT_FREQUENCY = 10;
static const ledc_mode_t  LC_SPEED_MODE = LEDC_HIGH_SPEED_MODE; 
static const uint32_t     LC_RESOLUTION = (uint8_t)LEDC_TIMER_12_BIT;
static const uint32_t     LC_DUTY = 1<<(LC_RESOLUTION-1);

#if !defined(__containerof)
#define __containerof(ptr, type, member) ({         \
      const typeof( ((type *)0)->member ) *__mptr = (ptr); \
      (type *)( (char *)__mptr - offsetof(type,member) ); })
#endif

typedef struct ed_instance_t ed_instance_t;
struct ed_instance_t {
  ed_t            parent;

  int               enable_pin;
  int               dir_pin;
  // int               step_pin;
  int               ms1_pin;
  int               ms2_pin;

  ledc_timer_t    ledc_timer;
  ledc_channel_t  ledc_channel;
  pcnt_unit_t     pcnt_unit;
  uint16_t        frequency;
  uint32_t        duty;
  ed_direction_t  direction;
  bool            moving;
};

bool mgos_easydriver_init(void) {
  LOG(LL_INFO,("This is fine"));
  return true;
}

bool init_ed_ledc(ed_config_t *ed_config, uint16_t freq) {
  // Prepare and then apply the LEDC PWM timer configuration
  ledc_timer_config_t ledc_timer = {
    .speed_mode = LC_SPEED_MODE,
    .timer_num = (ledc_timer_t)(ed_config->pwm_timer),
    .duty_resolution = LC_RESOLUTION,
    .freq_hz = freq,
    .clk_cfg = LEDC_AUTO_CLK
  };
  if( ledc_timer_config(&ledc_timer) != ESP_OK ) {
    return false;
  };

  // Prepare and then apply the LEDC PWM channel configuration
  ledc_channel_config_t ledc_channel = {
    .speed_mode = LC_SPEED_MODE,
    .channel = (ledc_channel_t)(ed_config->pwm_channel),
    .timer_sel = (ledc_timer_t)(ed_config->pwm_timer),
    .intr_type = LEDC_INTR_DISABLE,
    .gpio_num = ed_config->step_pin,
    .duty = LC_DUTY,
    .hpoint = 0,
  };
  if( ledc_channel_config(&ledc_channel) != ESP_OK ) {
    return false;
  };

  ledc_timer_resume(LC_SPEED_MODE, (ledc_timer_t)(ed_config->pwm_timer));
  return true;
}

void init_ed_pcnt(ed_config_t *ed_config) {
  pcnt_config_t pcnt_config = {
    .unit = (pcnt_unit_t)(ed_config->cnt_dev),
    .channel = PCNT_CHANNEL_0,
    .pulse_gpio_num = ed_config->step_pin,
    .ctrl_gpio_num = PCNT_PIN_NOT_USED,
    .pos_mode = PCNT_COUNT_DIS,
    .neg_mode = PCNT_COUNT_INC,
    .lctrl_mode = PCNT_MODE_KEEP,
    .hctrl_mode = PCNT_MODE_KEEP,
    .counter_h_lim = 1,
    .counter_l_lim = 0,
  };
  pcnt_unit_config(&pcnt_config);

  //adjust the step pin to be also input
  gpio_set_direction(ed_config->step_pin, GPIO_MODE_INPUT_OUTPUT);
  //use the muxer to redirect output from the ledc out to the pin
  gpio_matrix_out(ed_config->step_pin, LEDC_HS_SIG_OUT0_IDX + (int)(ed_config->pwm_channel), false, false);

  pcnt_counter_pause((pcnt_unit_t)(ed_config->cnt_dev));
  pcnt_counter_clear((pcnt_unit_t)(ed_config->cnt_dev));
  pcnt_counter_resume((pcnt_unit_t)(ed_config->cnt_dev));

  pcnt_isr_service_install(0);
}

void ed_instance_enable(ed_t *ed, bool enable) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  uint32_t pin_value = 0;
  if(enable == false) pin_value = 1;
  gpio_set_level(ed_instance->enable_pin, pin_value);
}

bool ed_instance_is_enabled(ed_t *ed) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  return gpio_get_level(ed_instance->enable_pin) == 0;
}

void ed_instance_set_direction(ed_t *ed, ed_direction_t dir) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  if(ed_instance->direction == dir) return;
  ed_instance->direction = dir;
  gpio_set_level(ed_instance->dir_pin, (uint32_t)dir);
}
  
ed_direction_t ed_instance_get_direction(ed_t *ed) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  return ed_instance->direction;
}

void ed_instance_start(ed_t *ed) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  ed_instance->moving = true;
  ledc_set_duty(LC_SPEED_MODE, ed_instance->ledc_channel, 2048);
  ledc_update_duty(LC_SPEED_MODE, ed_instance->ledc_channel);
  EMIT_EVENT(EASYDRIVER_MOVE);
}

void ed_instance_stop(ed_t *ed) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  ledc_set_duty(LC_SPEED_MODE, ed_instance->ledc_channel, 0);
  ledc_update_duty(LC_SPEED_MODE, ed_instance->ledc_channel);
  ed_instance->moving = false;
  EMIT_EVENT(EASYDRIVER_STOP);
}

bool ed_instance_is_moving(ed_t *ed) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  return ed_instance->moving;
}

static void ed_overflow_handler(void *arg)
{
  ed_t *ed = (ed_t *)arg;
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  ed_instance->parent.stop(ed);
  pcnt_event_disable(ed_instance->pcnt_unit, PCNT_EVT_H_LIM);
  pcnt_isr_handler_remove(ed_instance->pcnt_unit);
}

bool ed_instance_step(ed_t *ed, int steps) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  if(ed_instance->moving || steps == 0) {
    return false;
  }
  ed_direction_t dir = ED_DIR_FORWARD;
  if(steps < 0) {
    dir = ED_DIR_BACKWARDS;
  }
  ed_instance->parent.set_direction(ed, dir);
  pcnt_set_event_value(ed_instance->pcnt_unit, PCNT_EVT_H_LIM, abs(steps));
  pcnt_event_enable(ed_instance->pcnt_unit, PCNT_EVT_H_LIM);
  pcnt_counter_pause(ed_instance->pcnt_unit);
  pcnt_counter_clear(ed_instance->pcnt_unit);

  if(pcnt_isr_handler_add(ed_instance->pcnt_unit, ed_overflow_handler, ed) != ESP_OK) {
    LOG(LL_INFO,("Can not step"));
    return false;
  };
  ed_instance->parent.start(ed);
  pcnt_counter_resume(ed_instance->pcnt_unit);
  return true;
}

void ed_instance_set_microstep(ed_t *ed, ed_microstep_mode_t ms_mode) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  if(ed_instance->ms1_pin < 0 && ed_instance->ms2_pin < 0) return;
  switch(ms_mode) {
    case ED_MS_FULL_STEP: {
      gpio_set_level(ed_instance->ms1_pin, 0);
      gpio_set_level(ed_instance->ms2_pin, 0);
    } break;
    case ED_MS_HALF_STEP: {
      gpio_set_level(ed_instance->ms1_pin, 1);
      gpio_set_level(ed_instance->ms2_pin, 1);
    } break;
    case ED_MS_QUARTER_STEP: {
      gpio_set_level(ed_instance->ms1_pin, 1);
      gpio_set_level(ed_instance->ms2_pin, 0);
    } break;
    case ED_MS_EIGHT_STEP: {
      gpio_set_level(ed_instance->ms1_pin, 0);
      gpio_set_level(ed_instance->ms2_pin, 1);
    } break;
  };
}

bool ed_instance_set_frequency(ed_t *ed, uint32_t freq) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  if(freq > ED_MAX_FREQUENCY) return false;
  ed_instance->frequency = freq;
  return ledc_set_freq(LEDC_HIGH_SPEED_MODE, ed_instance->ledc_timer, freq) == ESP_OK;
}

uint16_t ed_instance_get_frequency(ed_t *ed) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  return ed_instance->frequency;
}

bool ed_instance_destroy(ed_t *ed) {
  ed_instance_t *ed_instance = __containerof(ed, ed_instance_t, parent);
  free(ed_instance);
  return true;
}

bool mgos_easydriver_ctor(ed_config_t *config, ed_t **ed) {
  bool ret = true;
  ed_instance_t *ed_instance = NULL;

  if(config == NULL) {
    ret = false;
    goto error_exit;
  }

  if(ed == NULL) {
    ret = false;
    goto error_exit;
  }

  ed_instance = calloc(1, sizeof(ed_instance_t));
  if(ed_instance == NULL) {
    ret = false;
    goto error_exit;
  }

  gpio_set_level(config->enable_pin, 1);
  gpio_set_direction(config->enable_pin, GPIO_MODE_INPUT_OUTPUT);
  gpio_set_level(config->dir_pin, 0);
  gpio_set_direction(config->dir_pin, GPIO_MODE_INPUT_OUTPUT);

  if(config->ms1_pin > 0 && config->ms2_pin) {
    gpio_set_level(config->ms1_pin, 0);
    gpio_set_level(config->ms2_pin, 0);
    gpio_set_direction(config->ms1_pin, GPIO_MODE_OUTPUT);
    gpio_set_direction(config->ms2_pin, GPIO_MODE_OUTPUT);
  }

  if(!init_ed_ledc(config, 5)) {
    ret = false;
    goto error_exit;
  }
  init_ed_pcnt(config);

  ed_instance->enable_pin = config->enable_pin;
  ed_instance->dir_pin = config->dir_pin;
  ed_instance->ms1_pin = config->ms1_pin;
  ed_instance->ms2_pin = config->ms2_pin;

  ed_instance->pcnt_unit = (pcnt_unit_t)(config->cnt_dev);
  ed_instance->ledc_timer = (ledc_timer_t)(config->pwm_timer);
  ed_instance->ledc_channel = (ledc_channel_t)(config->pwm_channel);
  ed_instance->frequency = INIT_FREQUENCY;
  ed_instance->duty = LC_DUTY;
  ed_instance->direction = ED_DIR_FORWARD;
  ed_instance->moving = false;

  ed_instance->parent.enable = ed_instance_enable;
  ed_instance->parent.is_enabled = ed_instance_is_enabled;
  ed_instance->parent.start = ed_instance_start;
  ed_instance->parent.stop = ed_instance_stop;
  ed_instance->parent.step = ed_instance_step;
  ed_instance->parent.is_moving  = ed_instance_is_moving;
  ed_instance->parent.set_direction = ed_instance_set_direction;
  ed_instance->parent.get_direction = ed_instance_get_direction;
  ed_instance->parent.set_frequency = ed_instance_set_frequency;
  ed_instance->parent.get_frequency = ed_instance_get_frequency;
  ed_instance->parent.set_microstep = ed_instance_set_microstep;
  ed_instance->parent.destroy = ed_instance_destroy;

  *ed = &(ed_instance->parent);
  return ret;

error_exit:
  if(ed_instance) {
    free(ed_instance);
  }
  return ret;
}