#pragma once
#include "Services/DataBaseInitializer.h"
#include "  Controllers/AdminController.h"
#include "  Controllers/TerminalController.h"

void testHash();

int check(const DataBaseInitializer&);

void testAdmin(AdminController&);

void testTerminal(TerminalController&);