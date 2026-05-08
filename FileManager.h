#pragma once

class WebServer;

/**
 * Register LittleFS file-manager routes on the given WebServer.
 *
 * Routes registered:
 *   GET    /fs              – self-contained file-manager HTML UI
 *   GET    /fs/info         – JSON storage info
 *   GET    /fs/list         – JSON directory listing (?path=/)
 *   GET    /fs/download     – file download (?path=/file)
 *   DELETE /fs/delete       – delete file or empty dir (?path=/file)
 *   POST   /fs/upload       – multipart file upload
 *   GET    /fs/mkdir        – create directory (?path=/dir&name=subdir)
 *   GET    /fs/rename       – rename / move (?src=/old&dest=/new&cwd=/)
 */
void setupFileManagerRoutes(WebServer* server);
