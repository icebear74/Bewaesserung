#include "Application.h"

// Web handlers and JSON processing in the main loop can require more than the
// default Arduino-ESP32 loopTask stack.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

Application app;

void setup() {
    Serial.begin(115200);
    app.begin();
}

void loop() {
    app.update();
}
