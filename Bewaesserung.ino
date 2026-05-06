#include "Application.h"

Application app;

void setup() {
    Serial.begin(115200);
    app.begin();
}

void loop() {
    app.update();
}
