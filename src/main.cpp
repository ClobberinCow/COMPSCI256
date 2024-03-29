#include <Arduino.h>
#include <TensorFlowLite_ESP32.h>
/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "main_functions.h"

#include "detection_responder.h"
#include "image_provider.h"
#include "model_settings.h"
#include "person_detect_model_data.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_log.h>
#include "esp_main.h"
#include "esp_cli.h"
#include <picdat.h>

// Globals, used for compatibility with Arduino-style sketches.
int picnum = 0;
long long cycle_start = 0;
long long cycles = 0;
namespace {
tflite::ErrorReporter* error_reporter = nullptr;
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;

// In order to use optimized tensorflow lite kernels, a signed int8_t quantized
// model is preferred over the legacy unsigned model format. This means that
// throughout this project, input images must be converted from unisgned to
// signed format. The easiest and quickest way to convert from unsigned to
// signed 8-bit integers is to subtract 128 from the unsigned value to get a
// signed value.

#ifdef CONFIG_IDF_TARGET_ESP32S3
constexpr int scratchBufSize = 39 * 1024;
#else
constexpr int scratchBufSize = 0;
#endif
// An area of memory to use for input, output, and intermediate arrays.
constexpr int kTensorArenaSize = 81 * 1024 + scratchBufSize;
static uint8_t *tensor_arena;//[kTensorArenaSize]; // Maybe we should move this to external
}  // namespace

#if defined(COLLECT_CPU_STATS)
  long long total_time = 0;
  long long start_time = 0;
  // extern long long softmax_total_time;
  // extern long long dc_total_time;
  // extern long long conv_total_time;
  // extern long long fc_total_time;
  // extern long long pooling_total_time;
  // extern long long add_total_time;
  // extern long long mul_total_time;
#endif
// The name of this function is important for Arduino compatibility.
void setup() {
  // Set up logging. Google style is to avoid globals or statics because of
  // lifetime uncertainty, but since this has a trivial destructor it's okay.
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_person_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(error_reporter,
                         "Model provided is schema version %d not equal "
                         "to supported version %d.",
                         model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  if (tensor_arena == NULL) {
    tensor_arena = (uint8_t *) heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (tensor_arena == NULL) {
    printf("Couldn't allocate memory of %d bytes\n", kTensorArenaSize);
    return;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  //
  // tflite::AllOpsResolver resolver;
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroMutableOpResolver<6> micro_op_resolver;
  micro_op_resolver.AddAveragePool2D();
  micro_op_resolver.AddConv2D();
  micro_op_resolver.AddDepthwiseConv2D();
  micro_op_resolver.AddReshape();
  micro_op_resolver.AddSoftmax();
  micro_op_resolver.AddFullyConnected();

  // Build an interpreter to run the model with.
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
    return;
  }

  // Get information about the memory area to use for the model's input.
  input = interpreter->input(0);

  // Initialize Camera
  TfLiteStatus init_status = InitCamera(error_reporter);
  if (init_status != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "InitCamera failed\n");
    return;
  }
  #ifdef CLI_ONLY_INFERENCE
  esp_cli_init();
  #endif
}

// The name of this function is important for Arduino compatibility.
void loop() {

  if (cycles == 0)
  {
    cycle_start = esp_timer_get_time();
  }

  vTaskDelay(5); // to avoid watchdog trigger
  cycles ++;
  if (cycles > 1000)
  {
    long long avgtime = esp_timer_get_time() - cycle_start;
    avgtime = avgtime / 1000000;
    printf("Avg time is: %lld\r\n", avgtime);
    cycles = 0;
  }
}


// run_inferencevoid run_inference(void *ptr)
void run_inference(void *ptr) {
  /* Convert image to  */
  long long start_time = esp_timer_get_time();
  for (int j = 0; j < 50; j++)
  {
  for (int i = 0; i < 784; i++)
  {
      input->data.f[i] = (255.0 - (float)pics[j][i]) /255.0;
      //input->data.f[i] = (float)pic1[i];
  }
  // for (int i = 0; i < kNumCols * kNumRows; i++) {
  //   input->data.int8[i] = ((uint8_t *) ptr)[i] ^ 0x80;
  // }

// #if defined(COLLECT_CPU_STATS)
//   long long start_time = esp_timer_get_time();
// #endif
  // Run the model on this input and make sure it succeeds.
  if (kTfLiteOk != interpreter->Invoke()) {
    error_reporter->Report("Invoke failed.");
  }


  TfLiteTensor* output = interpreter->output(0);
  // // Process the inference results.
  int8_t person_score = output->data.uint8[kPersonIndex];
  int8_t no_person_score = output->data.uint8[kNotAPersonIndex];

  float person_score_f =
      (person_score - output->params.zero_point) * output->params.scale;
  float no_person_score_f =
      (no_person_score - output->params.zero_point) * output->params.scale;
  float scratch = 0;
  int label = 0;
  // printf("Zero Point: %d \r\n", output->params.zero_point);
  // printf("Scale: %f \r\n", output->params.scale);
  for (int c = 0; c < 10; c++)
  {
    if (output->data.f[c] > scratch)
    {
      scratch = output->data.f[c];
      label = c;
    }
    // printf("uint8 label %i: %d \r\n", i, output->data.uint8[i]);
    printf("f label %i: %f \r\n", c, output->data.f[c]);
  }
  printf("Inferred Label is %d \r\n", label);
  printf("Actual Label: %d \r\n", labels[j]);
  }
  #if defined(COLLECT_CPU_STATS)
  long long total_time = (esp_timer_get_time() - start_time);
  printf("Total time = %lld\n", total_time / 1000);
  //printf("Softmax time = %lld\n", softmax_total_time / 1000);
  // printf("FC time = %lld\n", fc_total_time / 1000);
  // printf("DC time = %lld\n", dc_total_time / 1000);
  // printf("conv time = %lld\n", conv_total_time / 1000);
  // printf("Pooling time = %lld\n", pooling_total_time / 1000);
  // printf("add time = %lld\n", add_total_time / 1000);
  // printf("mul time = %lld\n", mul_total_time / 1000);

  /* Reset times */
  total_time = 0;
  //softmax_total_time = 0;
  // dc_total_time = 0;
  // conv_total_time = 0;
  // fc_total_time = 0;
  // pooling_total_time = 0;
  // add_total_time = 0;
  // mul_total_time = 0;
  #endif
  // RespondToDetection(error_reporter, person_score_f, no_person_score_f);
}
