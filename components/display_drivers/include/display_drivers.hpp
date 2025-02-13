#pragma once

#include "display.hpp"
#include "driver/gpio.h"
#include "esp_lcd_panel_commands.h"

namespace espp {
namespace display_drivers {
/**
 * @brief Low-level callback to write bytes to the display controller.
 * @param data Pointer to array of bytes to write.
 * @param length Number of bytes to write.
 * @param user_data User data associated with this transfer, used for flags.
 */
typedef std::function<void(const uint8_t *data, size_t length, uint32_t user_data)> write_fn;

/**
 * @brief Send color data to the display, with optional flags.
 * @param sx The starting x-coordinate of the area to fill.
 * @param sy The starting y-coordinate of the area to fill.
 * @param ex The ending x-coordinate of the area to fill.
 * @param ey The ending y-coordinate of the area to fill.
 * @param color_data Pointer to the color data. Should be at least
 *                   (x_end-x_start)*(y_end-y_start)*2 bytes.
 * @param flags Optional flags to send with the transaction.
 */
typedef std::function<void(int sx, int sy, int ex, int ey, const uint8_t *color_data,
                           uint32_t flags)>
    send_lines_fn;

/**
 * @brief Config structure for all display drivers.
 */
struct Config {
  write_fn lcd_write; /**< Function which the display driver uses to write data (blocking) to the
                         display. */
  send_lines_fn lcd_send_lines{
      nullptr}; /**< Function which the display driver uses to send bulk (color) data (non-blocking)
                   to be written to the display. If not provided, it will default to using the
                   provided lcd_write (blocking) call. */
  gpio_num_t reset_pin;        /**< GPIO used for resetting the display. */
  gpio_num_t data_command_pin; /**< GPIO used for indicating to the LCD whether the bits are data or
                                  command bits. */
  gpio_num_t backlight_pin;    /**< GPIO used for controlling the backlight of the display. */
  bool backlight_on_value{false}; /**< Whether backlight is active high or active low(default). */
  bool invert_colors{false};      /**< Whether to invert the colors on the display. */
  int offset_x{0};                /**< X Gap / offset, in pixels. */
  int offset_y{0};                /**< Y Gap / offset, in pixels. */
  bool swap_xy{false};            /**< Swap row/column order. */
  bool mirror_x{false};           /**< Mirror the display horizontally. */
  bool mirror_y{false};           /**< Mirror the display vertically. */
};

/**
 * @brief Mode for configuring the data/command pin.
 */
enum class Mode {
  COMMAND = 0, /**< Mode for sending commands to the display. */
  DATA = 1     /**< Mode for sending data (config / color) to the display. */
};

/**
 * @brief Flags that will be used by each display driver to signal to the
 *        low level pre/post callbacks to perform different actions.
 */
enum class Flags {
  FLUSH_BIT = 0, /**< Flag for use with the LVGL subsystem, indicating that the display is ready to
                    be flushed. */
  DC_LEVEL_BIT = 1 /**< Flag for use with the pre-transfer callback to set the data/command pin into
                      the correct level for the upcoming transfer. */
};

/**
 * @brief Command structure for initializing the lcd
 */
struct LcdInitCmd {
  uint8_t command;  /**< Command byte */
  uint8_t data[16]; /**< Data bytes */
  uint8_t length; /**< Number of data bytes; bit 7 means delay after, 0xFF means end of commands. */
};

static void init_pins(gpio_num_t reset, gpio_num_t data_command, gpio_num_t backlight,
                      uint8_t backlight_on) {
  // Initialize display pins
  uint64_t gpio_output_pin_sel = ((1ULL << data_command) | (1ULL << reset) | (1ULL << backlight));

  gpio_config_t o_conf{.pin_bit_mask = gpio_output_pin_sel,
                       .mode = GPIO_MODE_OUTPUT,
                       .pull_up_en = GPIO_PULLUP_DISABLE,
                       .pull_down_en = GPIO_PULLDOWN_DISABLE,
                       .intr_type = GPIO_INTR_DISABLE};
  ESP_ERROR_CHECK(gpio_config(&o_conf));

  // turn on the backlight
  gpio_set_level(backlight, backlight_on);

  using namespace std::chrono_literals;
  // Reset the display
#if CONFIG_BSP_ESP32_S3_BOX_3
  gpio_set_level(reset, 1);
  std::this_thread::sleep_for(100ms);
  gpio_set_level(reset, 0);
  std::this_thread::sleep_for(100ms);
#else
  gpio_set_level(reset, 0);
  std::this_thread::sleep_for(100ms);
  gpio_set_level(reset, 1);
  std::this_thread::sleep_for(100ms);
#endif
}
} // namespace display_drivers
} // namespace espp
