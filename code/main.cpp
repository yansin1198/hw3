#include "accelerometer_handler.h"
#include "config.h"
#include "magic_wand_model_data.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

#include "mbed.h"
#include "mbed_rpc.h"
#include "uLCD_4DGL.h"
#include <string>
//add acc
#include "stm32l475e_iot01_accelero.h"

#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"

//-----RPC------//
DigitalOut myled1(LED1);
DigitalOut myled2(LED2);
DigitalOut myled3(LED3);
BufferedSerial pc(USBTX, USBRX);
uLCD_4DGL uLCD(D1, D0, D2);
int currentRow = 6;
int prePosition = 6;
double x, y;

void gestureControl(Arguments *in, Reply *out);
void tiltAngleControl(Arguments *in, Reply *out);
RPCFunction gestureUI(&gestureControl, "gestureControl");
RPCFunction angleDetection(&tiltAngleControl, "tiltAngleControl");
Thread thread1;
Thread thread2;
EventQueue queue1;
EventQueue queue2;

// uLCD Info
EventQueue queue3;
Thread thread3;

int angles[4] = {30, 45, 60, 90};
int mode = 0;
int curGesture = -1;
int confirmGesture = angles[0];

int16_t tiltValue[3] = {0};
int16_t reference[3] = {0};
bool measure = 0;
bool overThreshold = 0;
float cosValue = 0;
int thresholdIndicate = 0;
#define PI 3.14159265
float d = 0; //tilt angle on uLCD
int tiltEvents = 0;
//--------------//


//---MQTT------//
WiFiInterface *wifi;
InterruptIn btn2(USER_BUTTON);
//InterruptIn btn3(SW3);
volatile int message_num = 0;
volatile int arrivedcount = 0;
volatile bool closed = false;

const char* topic = "Mbed";

Thread mqtt_thread(osPriorityHigh);
Thread mqtt_thread2(osPriorityHigh);
EventQueue mqtt_queue;
EventQueue mqtt_queue2;

void messageArrived(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char msg[300];
    sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);
    printf(msg);
    ThisThread::sleep_for(1000ms);
    char payload[300];
    sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char*)message.payload);
    printf(payload);
    ++arrivedcount;
}

void publish_message(MQTT::Client<MQTTNetwork, Countdown>* client) { //PC/Python
  if (mode == 1) {
      MQTT::Message message;
      char buff[100];
      sprintf(buff, "Confirm Angle: %d", confirmGesture);
      message.qos = MQTT::QOS0;
      message.retained = false;
      message.dup = false;
      message.payload = (void*) buff;
      message.payloadlen = strlen(buff) + 1;
      int rc = client->publish(topic, message);

      printf("rc:  %d\r\n", rc);
      printf("Puslish message: %s\r\n", buff);
      mode = 0;
      myled1 = 0;
      
  } else if (mode == 2) {
      if (overThreshold == 1) {
        MQTT::Message message;
        char buff[100];
        sprintf(buff, "Tilt event #%d: %.2f", tiltEvents, d);
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*) buff;
        message.payloadlen = strlen(buff) + 1;
        int rc = client->publish(topic, message);

        printf("Puslish message: %s\r\n", buff);
        tiltEvents++;
        if (tiltEvents > 10) {
          myled2 = 0;
          myled3 = 0;
          tiltEvents = 0;
          overThreshold = 0;
          mode = 0;
        }
      } 
  }

  
}

/*void close_mqtt() {
    closed = true;
}*/
//---MQTT----------//

//-----Init--------//
void Selection() {
    uLCD.locate(3, 10);
    uLCD.printf("Now Angle:\n");
    if (currentRow == 6) {
        uLCD.locate(3, 11);
        uLCD.printf("30\n");
        confirmGesture = angles[0];
        thresholdIndicate = 0;
    } else if (currentRow == 7) {
        uLCD.locate(3, 11);
        uLCD.printf("45\n");
        confirmGesture = angles[1];
        thresholdIndicate = 1;
    } else if (currentRow == 8) {
        uLCD.locate(3, 11);
        uLCD.printf("60\n");
        confirmGesture = angles[2];
        thresholdIndicate = 2;
    } else if (currentRow == 9) {
        uLCD.locate(3, 11);
        uLCD.printf("90\n");
        confirmGesture = angles[3];
        thresholdIndicate = 3;
    }
}

void Info() {
   // Note that printf is deferred with a call in the queue
   // It is not executed in the interrupt context
   uLCD.locate(2, prePosition);
   uLCD.printf(" ");
   uLCD.locate(2, currentRow);
   uLCD.printf(">");
}

void uLCDInfo() {
    uLCD.background_color(0xFFFFFF);
    uLCD.textbackground_color(0xFFFFFF);
    uLCD.color(BLUE);
    //uLCD.locate(3, 6);
    //uLCD.printf("Now angle: %d", angles[0]);

    uLCD.locate(2, currentRow);
    uLCD.printf(">");
    uLCD.locate(3, 6);
    uLCD.printf("30\n");
    uLCD.locate(3, 7);
    uLCD.printf("45\n");
    uLCD.locate(3, 8);
    uLCD.printf("60\n");
    uLCD.locate(3, 9);
    uLCD.printf("90\n");
}
//-------------//


// Create an area of memory to use for input, output, and intermediate arrays.
// The size of this will depend on the model you're using, and may need to be
// determined by experimentation.
constexpr int kTensorArenaSize = 60 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

// Return the result of the last prediction
int PredictGesture(float* output) {
  // How many times the most recent gesture has been matched in a row
  static int continuous_count = 0;
  // The result of the last prediction
  static int last_predict = -1;

  // Find whichever output has a probability > 0.8 (they sum to 1)
  int this_predict = -1;
  for (int i = 0; i < label_num; i++) {
    if (output[i] > 0.8) this_predict = i;
  }

  // No gesture was detected above the threshold
  if (this_predict == -1) {
    continuous_count = 0;
    last_predict = label_num;
    return label_num;
  }

  if (last_predict == this_predict) {
    continuous_count += 1;
  } else {
    continuous_count = 0;
  }
  last_predict = this_predict;

  // If we haven't yet had enough consecutive matches for this gesture,
  // report a negative result
  if (continuous_count < config.consecutiveInferenceThresholds[this_predict]) {
    return label_num;
  }
  // Otherwise, we've seen a positive result, so clear all our variables
  // and report it
  continuous_count = 0;
  last_predict = -1;

  return this_predict;
}

int main(int argc, char* argv[]) {

  uLCDInfo();

  // Whether we should clear the buffer next time we fetch data
  bool should_clear_buffer = false;
  bool got_data = false;

  // The gesture index of the prediction
  int gesture_index;

  // Set up logging.
  static tflite::MicroErrorReporter micro_error_reporter;
  tflite::ErrorReporter* error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  const tflite::Model* model = tflite::GetModel(g_magic_wand_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    error_reporter->Report(
        "Model provided is schema version %d not equal "
        "to supported version %d.",
        model->version(), TFLITE_SCHEMA_VERSION);
    return -1;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  static tflite::MicroOpResolver<6> micro_op_resolver;
  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_MAX_POOL_2D,
                               tflite::ops::micro::Register_MAX_POOL_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                               tflite::ops::micro::Register_FULLY_CONNECTED());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                               tflite::ops::micro::Register_SOFTMAX());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,
                               tflite::ops::micro::Register_RESHAPE(), 1);

  // Build an interpreter to run the model with
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  tflite::MicroInterpreter* interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors
  interpreter->AllocateTensors();

  // Obtain pointer to the model's input tensor
  TfLiteTensor* model_input = interpreter->input(0);
  if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] != config.seq_length) ||
      (model_input->dims->data[2] != kChannelNumber) ||
      (model_input->type != kTfLiteFloat32)) {
    error_reporter->Report("Bad input tensor parameters in model");
    return -1;
  }

  int input_length = model_input->bytes / sizeof(float);

  TfLiteStatus setup_status = SetupAccelerometer(error_reporter);
  if (setup_status != kTfLiteOk) {
    error_reporter->Report("Set up failed\n");
    return -1;
  }

  error_reporter->Report("Set up successful...\n");

  //--------MQTT---------//
  wifi = WiFiInterface::get_default_instance();
  if (!wifi) {
          printf("ERROR: No WiFiInterface found.\r\n");
          return -1;
  }


  printf("\nConnecting to %s...\r\n", MBED_CONF_APP_WIFI_SSID);
  int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
  if (ret != 0) {
          printf("\nConnection error: %d\r\n", ret);
          return -1;
  }


  NetworkInterface* net = wifi;
  MQTTNetwork mqttNetwork(net);
  MQTT::Client<MQTTNetwork, Countdown> client(mqttNetwork);

  //TODO: revise host to your IP
  //const char* host = "192.168.0.12";
  const char* host = "192.168.226.218";
  printf("Connecting to TCP network...\r\n");

  SocketAddress sockAddr;
  sockAddr.set_ip_address(host);
  sockAddr.set_port(1883);

  printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"),  (sockAddr.get_port() ? sockAddr.get_port() : 0) ); //check setting

  int rc = mqttNetwork.connect(sockAddr);//(host, 1883);
  if (rc != 0) {
          printf("Connection error.");
          return -1;
  }
  printf("Successfully connected!\r\n");

  MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
  data.MQTTVersion = 3;
  data.clientID.cstring = "Mbed";

  if ((rc = client.connect(data)) != 0){
          printf("Fail to connect MQTT\r\n");
  }
  if (client.subscribe(topic, MQTT::QOS0, messageArrived) != 0){
          printf("Fail to subscribe\r\n");
  }

  mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));
  mqtt_thread2.start(callback(&mqtt_queue2, &EventQueue::dispatch_forever));
  btn2.rise(mqtt_queue.event(&publish_message, &client));
  //btn3.rise(&close_mqtt);*/

  //------RPC-------//
  // receive commands, and send back the responses
  char buf[256], outbuf[256];
  FILE *devin = fdopen(&pc, "r");
  FILE *devout = fdopen(&pc, "w");
  thread1.start(callback(&queue1, &EventQueue::dispatch_forever));
  thread2.start(callback(&queue2, &EventQueue::dispatch_forever));
  thread3.start(callback(&queue3, &EventQueue::dispatch_forever));
  BSP_ACCELERO_Init();
  //------RPC-------//

  while (true) {

    while(mode == 0) {
        memset(buf, 0, 256);
        for (int i = 0; ; i++) {
            char recv = fgetc(devin);
            if (recv == '\n') {
                printf("\r\n");
                break;
            }
            buf[i] = fputc(recv, devout);
        }
        //Call the static call method on the RPC class
        RPC::call(buf, outbuf);
        printf("%s\r\n", outbuf);

        uLCD.locate(3, 12);
        uLCD.printf("             ");
        uLCD.locate(3, 13);
        uLCD.printf("             ");
    }

    while(mode == 1) {
        
        got_data = ReadAccelerometer(error_reporter, model_input->data.f,
                                    input_length, should_clear_buffer);

        if (!got_data) {
          should_clear_buffer = false;
          continue;
        }

        TfLiteStatus invoke_status = interpreter->Invoke();
        if (invoke_status != kTfLiteOk) {
          error_reporter->Report("Invoke failed on index: %d\n", begin_index);
          continue;
        }

        gesture_index = PredictGesture(interpreter->output(0)->data.f);
        should_clear_buffer = gesture_index < label_num;

        if (gesture_index < label_num) {
          curGesture = gesture_index;
          error_reporter->Report(config.output_message[gesture_index]);
        }
    }

    while (mode == 2) {
      if (measure = 1) {
        myled2 = 1;
        //printf("%d\n", measure);
        BSP_ACCELERO_AccGetXYZ(tiltValue);
        //cos=(x1x2+y1y2+z1z2)/[√(x1^2+y1^2+z1^2)*√(x2^2+y2^2+z2^2)]。
        float a, b, c;
        //double e;
        a = (float)((reference[0]*tiltValue[0] + reference[1]*tiltValue[1] + reference[2]*tiltValue[2]));
        b = (float)(sqrt((double)(reference[0]*reference[0]+reference[1]*reference[1]+reference[2]*reference[2])));
        c = (float)(sqrt((double)(tiltValue[0]*tiltValue[0]+tiltValue[1]*tiltValue[1]+tiltValue[2]*tiltValue[2])));

        //printf("%.2f %.2f %.2f\n", a, b, c);
        cosValue = a / (b * c);
        //printf("%.2f\n", cosValue);
        d = acos(cosValue) * 180.0 / PI;
        //printf("%.2f\n", d);
        if (d < 0) d = 0;
        uLCD.locate(3, 12);
        uLCD.printf("Tilt angle:");
        uLCD.locate(3, 13);
        uLCD.printf("%.2f", d); 
        if (d > angles[thresholdIndicate]) {
          myled3 = 1;
          overThreshold = 1;
          mqtt_queue2.call(&publish_message, &client);
        } else myled3 = 0; //when over threshold
        ThisThread::sleep_for(500ms);
      }
    }
  }
}

void angleSelection() {
    mode = 1;
    
    if (x == 1) {
        myled1 = 1;
    } 
    while(mode == 1) {
      if (curGesture == 0) { //up
        /*uLCD.locate(3, 6);
        uLCD.printf("Now angle: %d", angles[0]);
        confirmGesture = angles[0];
        thresholdIndicate = 0;*/

        //prePosition = currentRow;
        //currentRow -= 1; 
        //if (currentRow < 6) {
            //currentRow = 9;
        //}
        //queue3.call(Info);
        //queue3.call(Selection);
        curGesture = -1;

      } else if (curGesture == 2) { //down
        /*uLCD.locate(3, 6);
        uLCD.printf("Now angle: %d", angles[1]);
        confirmGesture = angles[1];
        thresholdIndicate = 1;*/

        prePosition = currentRow;
        currentRow += 1; 
        if (currentRow > 9) {
            currentRow = 6;
        }
        queue3.call(Info);
        queue3.call(Selection);
        curGesture = -1;

      } else if (curGesture == 1) { //confirm
        /*uLCD.locate(3, 6);
        uLCD.printf("Now angle: %d", angles[2]);
        confirmGesture = angles[2];
        thresholdIndicate = 2;*/

        //queue3.call(Selection);
        curGesture = -1;
      }
    }
}

// Make sure the method takes in Arguments and Reply objects.
// gesture UI
void gestureControl (Arguments *in, Reply *out)   {

    queue1.call(angleSelection);

    bool success = true;
    x = in->getArg<double>();

    char buffer[200], outbuf[256];
    char strings[20];
    int led1 = x;
    
    sprintf(strings, "UI mode = %d", led1);
    strcpy(buffer, strings);
    RPC::call(buffer, outbuf);
    if (success) {
        out->putData(buffer);
    } else {
        out->putData("Failed to execute LED control.");
    }
}

void checkTiltAngle() {
    mode = 2;
    
    /*if (y == 1) {
        myled2 = 1;
    } */

    int16_t initial[3] = {0};
    int num = 0;
    while (num < 5) {
      BSP_ACCELERO_AccGetXYZ(initial);
      myled2 = !myled2;
      num++;
      ThisThread::sleep_for(100ms);
    }
    reference[0] = initial[0];
    reference[1] = initial[1];
    reference[2] = initial[2];
    //printf("Finish Init");
    //printf("%d %d %d\n", reference[0], reference[1], reference[2]);
    measure = 1;
}

void tiltAngleControl(Arguments *in, Reply *out) {
    
    queue2.call(checkTiltAngle);

    bool success = true;
    y = in->getArg<double>();

    char buffer[200], outbuf[256];
    char strings[20];
    int led2 = y;
    
    sprintf(strings, "Detect mode = %d", led2);
    strcpy(buffer, strings);
    RPC::call(buffer, outbuf);
    if (success) {
        out->putData(buffer);
    } else {
        out->putData("Failed to execute LED control.");
    }

}