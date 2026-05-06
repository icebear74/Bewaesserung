#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

class Application;

class WebServerManager {
public:
    WebServerManager();
    ~WebServerManager();

    void begin(Application* app, bool apMode);
    void handle(bool apMode);

    WebServer* getServer() { return _server; }

private:
    void setupRoutes();

    WebServer*  _server    = nullptr;
    DNSServer*  _dnsServer = nullptr;
    Application* _app      = nullptr;
};
