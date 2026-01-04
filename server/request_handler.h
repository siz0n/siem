#pragma once

#include <string>

#include "../db/minidbms.h"
#include "protocol.h"

// Обработка одного запроса от клиента
Response processRequest(const Request& req, MiniDBMS& db);

