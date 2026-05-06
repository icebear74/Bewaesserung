#include "WebServer.h"
#include "WebHandlers.h"
#include "core/Application.h"
#include <WiFi.h>

WebServerManager::WebServerManager() {}

WebServerManager::~WebServerManager() {
    delete _server;
    delete _dnsServer;
}

void WebServerManager::begin(Application* app, bool apMode) {
    _app    = app;
    _server = new WebServer(80);

    if (apMode) {
        _dnsServer = new DNSServer();
        _dnsServer->start(53, "*", WiFi.softAPIP());
        Serial.println("[Web] DNS captive portal started.");
    }

    setupRoutes();
    _server->begin();
    Serial.println("[Web] HTTP server started on port 80.");
}

void WebServerManager::handle(bool apMode) {
    if (apMode && _dnsServer) {
        _dnsServer->processNextRequest();
    }
    if (_server) {
        _server->handleClient();
    }
}

void WebServerManager::setupRoutes() {
    registerHandlers(this, _app);
}
