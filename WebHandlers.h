#pragma once

class Application;
class WebServerManager;

void registerHandlers(WebServerManager* wsm, Application* app);
void flushAndRestart(WebServerManager* wsm, int delayMs = 1500);
