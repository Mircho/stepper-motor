// A naive driver for EasyDriver stepper controller
#pragma once

#include "stdint.h"
#include "stdbool.h"

#include "mgos.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MGOS_EVENT_BASE
#define EASYDRIVER_EVENT_BASE MGOS_EVENT_BASE('E', 'D', 'R')
#elif
#define EASYDRIVER_EVENT_BASE 0
#endif

enum easydriver_event {
  EASYDRIVER_BASE = EASYDRIVER_EVENT_BASE,
  EASYDRIVER_MOVE,
  EASYDRIVER_STOP
};

static const uint16_t ED_MIN_FREQUENCY = 0;
static const uint16_t ED_MAX_FREQUENCY = 240;

// MS2 || MS1
enum mgos_easydriver_microstep_mode {
  ED_MS_FULL_STEP     = 0,
  ED_MS_HALF_STEP     = 2,
  ED_MS_QUARTER_STEP  = 1,
  ED_MS_EIGHT_STEP    = 3
};
typedef enum mgos_easydriver_microstep_mode ed_microstep_mode_t;

enum mgos_easydriver_direction {
  ED_DIR_FORWARD      = 0,
  ED_DIR_BACKWARDS    = 1
};
typedef enum mgos_easydriver_direction ed_direction_t;

typedef void *ed_pwm_timer_t;
typedef void *ed_pwm_channel_t;
typedef void *ed_cnt_dev_t;

typedef struct ed_config_t ed_config_t;
struct ed_config_t {
  int               enable_pin;
  int               dir_pin;
  int               step_pin;
  int               ms1_pin;
  int               ms2_pin;
  ed_pwm_timer_t    pwm_timer;
  ed_pwm_channel_t  pwm_channel;
  ed_cnt_dev_t      cnt_dev;
};

#define ED_DEFAULT_CONFIG(en_gpio, dir_gpio, step_gpio, pwm_t, pwm_c, cnt_d) \
{                                                                            \
  .enable_pin = en_gpio,                                                     \
  .dir_pin = dir_gpio,                                                       \
  .step_pin = step_gpio,                                                     \
  .ms1_pin = -1,                                                             \
  .ms2_pin = -1,                                                             \
  .pwm_timer = (ed_pwm_timer_t *)pwm_t,                                      \
  .pwm_channel = (ed_pwm_channel_t *)pwm_c,                                  \
  .cnt_dev = (ed_cnt_dev_t *)cnt_d,                                          \
}

typedef struct ed_t ed_t;
struct ed_t {
  void            (*enable)(ed_t *ed, bool enable);
  bool            (*is_enabled)(ed_t *ed);
  void            (*start)(ed_t *ed);
  void            (*stop)(ed_t *ed);
  bool            (*step)(ed_t *ed, int steps);
  bool            (*is_moving)(ed_t *ed);
  void            (*set_direction)(ed_t *ed, ed_direction_t dir);
  ed_direction_t  (*get_direction)(ed_t *ed);
  void            (*set_microstep)(ed_t *ed, ed_microstep_mode_t ms_mode);
  bool            (*set_frequency)(ed_t *ed, uint32_t freq);
  uint16_t        (*get_frequency)(ed_t *ed);
  bool            (*destroy)(ed_t *ed);
};

bool mgos_easydriver_ctor(ed_config_t *config, ed_t **ed);

bool mgos_easydriver_init(void);

#ifdef __cplusplus
}
#endif