/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <stdexcept>

#include "json.hpp"
#include "cables/log.h"
#include "cables/adv_dbg_itf/adv_dbg_itf.hpp"
#include "cables/jtag-proxy/jtag-proxy.hpp"
#ifdef __USE_FTDI__
#include "cables/ftdi/ftdi.hpp"
#endif
#include "gdb-server/gdb-server.hpp"

using namespace std;


static js::config *system_config = NULL;

char Log::last_error[MAX_LOG_LINE] = "unknown error";
int Log::log_level = LOG_ERROR;
std::mutex Log::m_last_error;

static Log *s_log;

void Log::print(log_level_e level, const char *str, ...)
{
  if (this->log_level <= level) return;
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

// void Log::DumpHex(const void* data, size_t size) {
// 	char ascii[17];
// 	size_t i, j;
// 	ascii[16] = '\0';
// 	for (i = 0; i < size; ++i) {
// 		printf("%02X ", ((unsigned char*)data)[i]);
// 		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
// 			ascii[i % 16] = ((unsigned char*)data)[i];
// 		} else {
// 			ascii[i % 16] = '.';
// 		}
// 		if ((i+1) % 8 == 0 || i+1 == size) {
// 			printf(" ");
// 			if ((i+1) % 16 == 0) {
// 				printf("|  %s \n", ascii);
// 			} else if (i+1 == size) {
// 				ascii[(i+1) % 16] = '\0';
// 				if ((i+1) % 16 <= 8) {
// 					printf(" ");
// 				}
// 				for (j = (i+1) % 16; j < 16; ++j) {
// 					printf("   ");
// 				}
// 				printf("|  %s \n", ascii);
// 			}
// 		}
// 	}
// }

void Log::user(const char *str, ...)
{
  if (this->log_level <= LOG_INFO) return;
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

void Log::detail(const char *str, ...)
{
  if (this->log_level <= LOG_DETAIL) return;
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

void Log::debug(const char *str, ...)
{
  if (this->log_level <= LOG_DEBUG) return;
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

void Log::warning(const char *str, ...)
{
  if (this->log_level <= LOG_WARNING) return;
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

void Log::error(const char *str, ...)
{
  char buf[MAX_LOG_LINE];
  va_list va;
  va_start(va, str);
  vsnprintf(buf, MAX_LOG_LINE, str, va);
  va_end(va);
  {
    std::unique_lock<std::mutex> lck(m_last_error);
    strncpy(last_error, buf, MAX_LOG_LINE);
  }
  if (this->log_level <= LOG_ERROR) return;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

extern "C" int get_max_log_level()
{
  return LOG_LEVEL_MAX;
}

extern "C" void *cable_new(const char *config_string, const char *system_config_string)
{
  const char *cable_name = NULL;
  js::config *config = NULL;
  js::config *system_config = js::import_config_from_string(std::string(system_config_string));

  if (config_string != NULL)
  {
    config = js::import_config_from_string(std::string(config_string));
    js::config *type_config = config->get("type");
    if (type_config != NULL)
    {
      cable_name = type_config->get_str().c_str();
    }
  }

  if (cable_name == NULL) {
    s_log->error("No cable name specified\n");
    return NULL;
  }

  if (strncmp(cable_name, "ftdi", 4) == 0)
  {
#ifdef __USE_FTDI__
    Ftdi::FTDIDeviceID id = Ftdi::Olimex;
    if (strcmp(cable_name, "ftdi@digilent") == 0) id = Ftdi::Digilent;
    Adv_dbg_itf *adu = new Adv_dbg_itf(system_config, new Log("FTDI"), new Ftdi(system_config, s_log, id));
    if (!adu->connect(config)) return NULL;
    int tap = 0;
    if (config->get("tap")) tap = config->get("tap")->get_int();
    adu->device_select(tap);
    return (void *)static_cast<Cable *>(adu);
#else
    s_log->error("Debug bridge has not been compiled with FTDI support\n");
    return NULL;
#endif
  }
  else if (strcmp(cable_name, "jtag-proxy") == 0)
  {
    Adv_dbg_itf *adu = new Adv_dbg_itf(system_config, new Log("JPROX"), new Jtag_proxy(s_log));
    if (!adu->connect(config)) return NULL;
    int tap = 0;
    if (config->get("tap")) tap = config->get("tap")->get_int();
    adu->device_select(tap);
    return (void *)static_cast<Cable *>(adu);
  }
  else
  {
    s_log->error("Unknown cable: %s\n", cable_name);
    return NULL;
  }
  
  return NULL;
}

extern "C" void cable_write(void *cable, unsigned int addr, int size, const char *data)
{
  Adv_dbg_itf *adu = (Adv_dbg_itf *)cable;
  adu->access(true, addr, size, (char *)data);
}

extern "C" void cable_read(void *cable, unsigned int addr, int size, const char *data)
{
  Adv_dbg_itf *adu = (Adv_dbg_itf *)cable;
  adu->access(false, addr, size, (char *)data);
}

extern "C" void chip_reset(void *handler, bool active)
{
  Adv_dbg_itf *cable = (Adv_dbg_itf *)handler;
  cable->chip_reset(active);
}

extern "C" void jtag_reset(void *handler, bool active)
{
  Adv_dbg_itf *cable = (Adv_dbg_itf *)handler;
  cable->jtag_reset(active);
}

extern "C" void jtag_soft_reset(void *handler)
{
  Adv_dbg_itf *cable = (Adv_dbg_itf *)handler;
  cable->jtag_soft_reset();
}


extern "C" bool cable_jtag_set_reg(void *handler, unsigned int reg, int width, unsigned int value)
{
  Cable *cable = (Cable *)handler;
  return cable->jtag_set_reg(reg, width, value);
}

extern "C" bool cable_jtag_get_reg(void *handler, unsigned int reg, int width, unsigned int *out_value, unsigned int value)
{
  Cable *cable = (Cable *)handler;
  return cable->jtag_get_reg(reg, width, out_value, value);
}

extern "C" void cable_lock(void *handler)
{
  Cable *cable = (Cable *)handler;
  cable->lock();
}

extern "C" void cable_unlock(void *handler)
{
  Cable *cable = (Cable *)handler;
  cable->unlock();
}

static void init_sigint_handler(int s) {
  raise(SIGTERM);
}

extern "C" char * bridge_get_error()
{
  return strdup(Log::last_error);
}

extern "C" void bridge_set_log_level(int level)
{
  Log::log_level = level;
}

extern "C" void bridge_init(const char *config_string, int log_level)
{
  printf("Bridge init - log level %d\n", log_level);

  Log::log_level = log_level;
  s_log = new Log();
  system_config = js::import_config_from_string(std::string(config_string));

  // This should be the first C method called by python.
  // As python is not catching SIGINT where we are in C world, we have to
  // setup a  sigint handler to exit in case control+C is hit.
  signal (SIGINT, init_sigint_handler);
}


extern "C" void *gdb_server_open(void *cable, int socket_port, cmd_cb_t cmd_cb, const char * capabilities)
{
  return (void *)new Gdb_server(new Log("GDB_SRV"), (Cable *)cable, system_config, socket_port, cmd_cb, capabilities);
}

extern "C" void gdb_server_close(void *arg, int kill)
{
  Gdb_server *server = (Gdb_server *)arg;
  server->stop(kill);
}

extern "C" int gdb_send_str(void *arg, const char * str)
{
  Gdb_server *server = (Gdb_server *)arg;
  if (server->rsp) {
    Rsp::Client *client = server->rsp->get_client();
    if (client) {
      return client->send_str(str);
    }
  }
  return 0;
}

extern "C" void gdb_server_refresh_target(void *arg)
{
  Gdb_server *server = (Gdb_server *)arg;
  server->refresh_target();
}

#if 0

extern "C" void plt_exit(void *_bridge, bool status)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getMemIF()->exit(status);
}

extern "C" bool jtag_reset(void *_bridge)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_reset();
}

extern "C" bool jtag_idle(void *_bridge)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_idle();
}

extern "C" bool jtag_shift_ir(void *_bridge)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_shift_ir();
}

extern "C" bool jtag_shift_dr(void *_bridge)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_shift_dr();
}

extern "C" void jtag_shift(void *_bridge, int width, const char *datain, const char *dataout, int noex)
{
  Bridge *bridge = (Bridge *)_bridge;
  bridge->getJtagIF()->jtag_shift(width, (unsigned char *)datain, (unsigned char *)dataout, noex);
}

#endif
