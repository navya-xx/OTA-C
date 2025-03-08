#ifndef PCH_HPP
#define PCH_HPP

#pragma once

// Push the current definition of 'mode' (if any) and then undefine it
#pragma push_macro("mode")
#undef mode

#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/convert.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/exception_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/date_time.hpp>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <fstream>
#include <thread>
#include <iomanip>
#include <sstream>
#include <deque>
#include <map>
#include <unordered_map>
#include <exception>
#include <stdexcept>
#include <functional>
#include <random>
#include <cmath>
#include <limits>
#include <mqtt/async_client.h>
#include <json.hpp>

using json = nlohmann::json;

// typedef std::complex<std::int16_t> sample_type;
typedef float iq_type;
typedef std::complex<iq_type> sample_type;

#endif // PCH_HPP