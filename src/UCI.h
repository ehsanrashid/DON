//#pragma once
#ifndef UCI_H_
#define UCI_H_

#include <string>

namespace UCI {

    extern void start (const std::string &args = "");
    extern void stop ();

    extern void send_responce (const char format[], ...);

}

#endif
