#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl/lvgl.h"
#include "ui/ui.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Simple stroke detector state
typedef struct {
  int stroke_count;
  float stroke_rate;
  float total_distance;
  uint32_t elapsed_ms;
  float current_speed;
} sim_state_t;

static sim_state_t sim_state = {0};
static lv_timer_t *ui_timer = NULL;

// CSV Data structure (simplified)
typedef struct {
  float *timestamps;
  float *acc_x;
  float *acc_y;
  float *acc_z;
  size_t count;
  size_t current_idx;
} csv_data_t;

static csv_data_t csv_data = {0};
static uint32_t last_update_ms = 0;

// Load CSV file
static bool load_csv(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    printf("Failed to open CSV file: %s\n", filename);
    return false;
  }

  // Count lines
  size_t line_count = 0;
  char line[256];
  fgets(line, sizeof(line), f); // Skip header
  while (fgets(line, sizeof(line), f)) {
    line_count++;
  }

  if (line_count == 0) {
    fclose(f);
    return false;
  }

  // Allocate arrays
  csv_data.timestamps = (float *)malloc(line_count * sizeof(float));
  csv_data.acc_x = (float *)malloc(line_count * sizeof(float));
  csv_data.acc_y = (float *)malloc(line_count * sizeof(float));
  csv_data.acc_z = (float *)malloc(line_count * sizeof(float));
  csv_data.count = line_count;
  csv_data.current_idx = 0;

  // Read data
  rewind(f);
  fgets(line, sizeof(line), f); // Skip header again

  size_t idx = 0;
  while (fgets(line, sizeof(line), f) && idx < line_count) {
    float ts, ax, ay, az;
    if (sscanf(line, "%f,%f,%f,%f", &ts, &ax, &ay, &az) == 4) {
      csv_data.timestamps[idx] = ts;
      csv_data.acc_x[idx] = ax;
      csv_data.acc_y[idx] = ay;
      csv_data.acc_z[idx] = az;
      idx++;
    }
  }

  fclose(f);
  printf("Loaded %zu samples from CSV\n", csv_data.count);
  return true;
}

// Simple stroke detection (stub for now - will enhance later)
static void process_sample(float ax, float ay, float az, float timestamp_ms) {
  // Simple detection: count when Z acceleration crosses threshold
  static float last_az = 0;
  static bool in_stroke = false;
  static uint32_t stroke_start_ms = 0;

  const float threshold = 0.3f;

  if (!in_stroke && az > threshold && last_az <= threshold) {
    // Stroke start
    in_stroke = true;
    stroke_start_ms = (uint32_t)timestamp_ms;
    sim_state.stroke_count++;
  } else if (in_stroke && az < -threshold) {
    // Stroke end
    in_stroke = false;

    // Update metrics
    uint32_t stroke_duration = (uint32_t)timestamp_ms - stroke_start_ms;
    if (stroke_duration > 100 && stroke_duration < 3000) {
      // Valid stroke - update stats
      sim_state.total_distance += 10.0f; // Assume 10m per stroke
      sim_state.current_speed = 10.0f / (stroke_duration / 1000.0f);
    }
  }

  last_az = az;

  // Update elapsed time
  sim_state.elapsed_ms = (uint32_t)timestamp_ms;

  // Calculate stroke rate (SPM - strokes per minute)
  if (sim_state.elapsed_ms > 0 && sim_state.stroke_count > 0) {
    float elapsed_min = sim_state.elapsed_ms / 60000.0f;
    if (elapsed_min > 0.01f) {
      sim_state.stroke_rate = sim_state.stroke_count / elapsed_min;
    }
  }
}

// Update UI with current state
static void update_ui(lv_timer_t *timer) {
  (void)timer;

  uint32_t now = lv_tick_get();

  // Play CSV data at 1x speed
  if (csv_data.count > 0 && csv_data.current_idx < csv_data.count) {
    size_t idx = csv_data.current_idx;
    process_sample(csv_data.acc_x[idx], csv_data.acc_y[idx],
                   csv_data.acc_z[idx], csv_data.timestamps[idx]);
    csv_data.current_idx++;
  }

  // Update UI labels every 100ms
  if (now - last_update_ms < 100) {
    return;
  }
  last_update_ms = now;

  // Update stroke rate (桨频)
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", sim_state.stroke_rate);
  lv_label_set_text(ui_Label9, buf);

  // Update stroke count (桨数)
  snprintf(buf, sizeof(buf), "%d", sim_state.stroke_count);
  lv_label_set_text(ui_Label44, buf);

  // Update timer (计时)
  uint32_t total_sec = sim_state.elapsed_ms / 1000;
  uint32_t minutes = total_sec / 60;
  uint32_t seconds = total_sec % 60;
  snprintf(buf, sizeof(buf), "%02u:%02u", minutes, seconds);
  lv_label_set_text(ui_Label7, buf);

  // Update distance (距离)
  snprintf(buf, sizeof(buf), "%.3f", sim_state.total_distance / 1000.0f); // km
  lv_label_set_text(ui_Label23, buf);

  // Update split time (配速) - time per 500m
  if (sim_state.total_distance > 0) {
    float seconds_per_500m =
        (sim_state.elapsed_ms / 1000.0f) / (sim_state.total_distance / 500.0f);
    uint32_t split_min = (uint32_t)seconds_per_500m / 60;
    uint32_t split_sec = (uint32_t)seconds_per_500m % 60;
    uint32_t split_ms =
        (uint32_t)((seconds_per_500m - (split_min * 60 + split_sec)) * 10);
    snprintf(buf, sizeof(buf), "%02u:%02u.%u", split_min, split_sec, split_ms);
  } else {
    snprintf(buf, sizeof(buf), "00:00.0");
  }
  lv_label_set_text(ui_Label8, buf);

  // Update speed (速度)
  snprintf(buf, sizeof(buf), "%.1f", sim_state.current_speed);
  lv_label_set_text(ui_Label46, buf);

  // Update time display (top bar clock)
  time_t t = time(NULL);
  struct tm *tm_info = localtime(&t);
  snprintf(buf, sizeof(buf), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
  lv_label_set_text(ui_Label10, buf);

  // Update battery level (simulate full battery)
  lv_obj_clear_flag(ui_battery6on, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_battery7on, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_battery8on, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_battery9on, LV_OBJ_FLAG_HIDDEN);

  // Update GNSS signal (simulate strong signal)
  lv_obj_clear_flag(ui_sig1bar1on2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_sig1bar2on2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_sig1bar3on2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_sig1bar4on2, LV_OBJ_FLAG_HIDDEN);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  // Initialize LVGL
  lv_init();

  // Initialize display and input drivers
  lv_port_disp_init();
  lv_port_indev_init();

  // Initialize UI (from SquareLine Studio)
  ui_init();

  // Load Screen1 by default
  lv_disp_load_scr(ui_Screen1);

  // Load CSV data if available
  const char *csv_file = "../../data/imu_log_063.csv";
  if (!load_csv(csv_file)) {
    printf("Warning: Could not load CSV file, using demo mode\n");
  }

  // Create UI update timer (10Hz)
  ui_timer = lv_timer_create(update_ui, 100, NULL);

  printf("LVGL Rowing Simulator started\n");
  printf("Press Ctrl+C to exit\n");

  // Main loop
  while (1) {
    uint32_t time_till_next = lv_timer_handler();
    if (!lv_port_disp_handle_events()) {
      break; // Quit on window close
    }
    SDL_Delay(time_till_next < 5 ? 5 : time_till_next);
  }

  return 0;
}
