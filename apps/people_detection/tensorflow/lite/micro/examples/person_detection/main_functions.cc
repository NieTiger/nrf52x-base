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
#include <string.h>
#include "tensorflow/lite/micro/examples/person_detection/main_functions.h"

#include "tensorflow/lite/micro/examples/person_detection/detection_responder.h"
//#include "tensorflow/lite/micro/examples/person_detection/image_provider.h"
#include "tensorflow/lite/micro/examples/person_detection/model_settings.h"
#include "tensorflow/lite/micro/examples/person_detection/person_detect_model_data.h"
#include "tensorflow/lite/micro/tools/make/downloads/person_model_int8/images/96x96/man_image_data.h"

// #include "tensorflow/lite/micro/tools/make/downloads/person_model_int8/images/48x48/man_image_data_48.h"
// #include "tensorflow/lite/micro/tools/make/downloads/person_model_int8/images/48x48/indian_image_data_48.h"
// #include "tensorflow/lite/micro/tools/make/downloads/person_model_int8/images/48x48/meninshower_image_data_48.h"
// #include "tensorflow/lite/micro/tools/make/downloads/person_model_int8/images/48x48/pineapple_image_data_48.h"
// #include "tensorflow/lite/micro/tools/make/downloads/person_model_int8/images/48x48/lady_image_data_48.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/kernels/all_ops_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include <cstdio>
#include <stdlib.h>
#include "tensorflow/lite/kernels/internal/common.h"
#include "tensorflow/lite/kernels/internal/round.h"
#include "tensorflow/lite/kernels/internal/types.h"
extern "C" {
#include "virtual_timer.h"
}

// Globals, used for compatibility with Arduino-style sketches.
namespace {
tflite::ErrorReporter* error_reporter = nullptr;
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;

// An area of memory to use for input, output, and intermediate arrays.
constexpr int kTensorArenaSize = 180 * 1024;
static uint8_t tensor_arena[kTensorArenaSize];
}  // namespace


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
    error_reporter->Report(
        "Model provided is schema version %d not equal "
        "to supported version %d.",
        model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  //
  // tflite::ops::micro::AllOpsResolver resolver;
  // NOLINTNEXTLINE(runtime-global-variables)
  // static tflite::MicroOpResolver<3> micro_op_resolver;
  // micro_op_resolver.AddBuiltin(
  //     tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
  //     tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  // micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
  //                              tflite::ops::micro::Register_CONV_2D());
  // micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_AVERAGE_POOL_2D,
  //                              tflite::ops::micro::Register_AVERAGE_POOL_2D());

  static tflite::ops::micro::AllOpsResolver micro_op_resolver;

  // Build an interpreter to run the model with.
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;

  volatile size_t tensor_size = interpreter->tensors_size();


  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    error_reporter->Report("AllocateTensors() failed");
    return;
  }

  // // Get information about the memory area to use for the model's input.

  input = interpreter->tensor(1);
  error_reporter->Report("Input Name: %s", input->name);
  error_reporter->Report("Input Type: %d", input->type);
  error_reporter->Report("Input Size: %d", input->bytes);


}

void GetQuantizedImage(int8_t* input, void * image, int image_size, int32_t zero_point, float scale){

  static constexpr int32_t min_val = std::numeric_limits<int8_t>::min();
  static constexpr int32_t max_val = std::numeric_limits<int8_t>::max();

  unsigned char* buffer = (unsigned char*) malloc (sizeof(char)*image_size);
  memcpy(buffer, image, image_size);

  for(int i = 0; i < image_size; i++){
    float val = (float) buffer[i];
    int32_t unclamped =static_cast<int32>(::round(val / static_cast<float>(scale))) +
        zero_point;
    int32_t clamped = std::min(std::max(unclamped, min_val), max_val);
    input[i] = clamped;
  }

  free(buffer);
}


// The name of this function is important for Arduino compatibility.
void loop() {

  GetQuantizedImage(input->data.int8, (void *) man_data, kMaxImageSize, -128, 1.0f);

  virtual_timer_init();
  volatile uint32_t start_time = read_timer();
  
  if (kTfLiteOk != interpreter->Invoke()) {
    error_reporter->Report("Invoke failed.");
  }
  volatile uint32_t end_time = read_timer();
  volatile uint32_t time_taken = end_time-start_time;
  TfLiteTensor* output = interpreter->tensor(0);

  // error_reporter->Report("Output Name: %s", output->name);
  // error_reporter->Report("Output Type: %d", output->type);
  // error_reporter->Report("Output Size: %d", output->bytes);


  TfLiteQuantizationParams params = output->params;
  int32_t zero_point = params.zero_point;
  double scale = params.scale;
  int32_t p_score = output->data.int8[kPersonIndex];
  int32_t no_p_score = output->data.int8[kNotAPersonIndex];

  volatile float person_score = static_cast<float>(scale * (p_score - zero_point));
  volatile float no_person_score = static_cast<float>(scale * (no_p_score - zero_point));

  RespondToDetection(error_reporter, person_score, no_person_score);
}
