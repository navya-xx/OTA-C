#include "pch.hpp"

#include "log_macros.hpp"
#include "utility.hpp"
#include "config_parser.hpp"
#include "usrp_class.hpp"

#define LOG_LEVEL LogLevel::DEBUG
static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    /*------ Initialize ---------------*/
    std::string homeDirStr = get_home_dir();
    std::string projectDir = homeDirStr + "/OTA-C/ProjectRoot";
    std::string curr_time_str = currentDateTimeFilename();

    if (argc < 2)
        throw std::invalid_argument("ERROR : device address missing! Pass it as first argument to the function call.");

    std::string device_id = argv[1];

    /*----- LOG ------------------------*/
    std::string logFileName = projectDir + "/storage/logs/leaf_" + device_id + "_" + curr_time_str + ".log";
    Logger::getInstance().initialize(logFileName);
    Logger::getInstance().setLogLevel(LOG_LEVEL);

    /*------ Parse Config -------------*/
    ConfigParser parser(projectDir + "/config/config.conf");
    parser.set_value("device-id", device_id, "str", "USRP device number");
    parser.print_values();

    /*------- USRP setup --------------*/
    USRP_class usrp_classobj(parser);
    usrp_classobj.external_ref = parser.getValue_str("external-clock-ref") == "true" ? true : false;
    usrp_classobj.initialize();
    double rx_rate = usrp_classobj.rx_rate;

    /*-------- Receive data stream --------*/
    size_t num_samples = 0, num_samples_saved = 0;
    if (argc > 2)
    {
        num_samples = std::stoi(argv[2]);
    }
    else
    {
        float duration = 10.0;
        num_samples = size_t(duration * usrp_classobj.rx_rate);
    }

    size_t N_zfc = parser.getValue_int("Ref-N-zfc");
    size_t reps_zfc = parser.getValue_int("Ref-R-zfc");
    size_t ex_save_mul = 2;

    size_t capacity = N_zfc * (reps_zfc + ex_save_mul);

    std::deque<std::complex<float>> saved_P(capacity);
    std::vector<std::complex<float>> saved_buffer(2 * N_zfc, std::complex<float>(0.0));
    bool buffer_init = false, detection_flag = false;
    int save_extra = ex_save_mul * N_zfc, extra = 0, counter = 0;
    std::complex<float> P(0.0);
    float R = 0.0;
    float M = 0;
    float M_threshold = 0.01;
    uhd::time_spec_t ref_start_timer(0.0);

    // std::string filename = projectDir + "/storage/rxdata_data_" + device_id + "_" + curr_time_str + ".dat";
    // std::ofstream rx_save_stream(filename, std::ios::out | std::ios::binary | std::ios::app);
    // std::function save_stream_callback = [&](const std::vector<std::complex<float>> &rx_stream, const size_t &rx_stream_size, const uhd::time_spec_t &rx_timer)
    // {
    //     save_stream_to_file(filename, rx_save_stream, rx_stream);
    //     // Low-pass filter (FFTW3) and Downsample
    //     // stream_deq.pop_front();
    //     // stream_deq.push_back(std::move(rx_stream));
    //     // timer_deq.pop_front();
    //     // timer_deq.push_back(rx_timer);
    //     // check power of samples over a window
    //     // size_t samples_processed = 0;
    //     // while (samples_processed < rx_stream_size)
    //     // {
    //     //     float sum = 0.0;
    //     //     for (size_t i = 0; i < window_len; ++i)
    //     //     {
    //     //         sum += std::norm(rx_stream[samples_processed]);
    //     //         ++samples_processed;
    //     //     }
    //     //     sum /= window_len;
    //     //     LOG_INFO_FMT("Average signal strength over window = %1%", sum);
    //     // }
    //     num_samples_saved += rx_stream_size;
    //     if (num_samples_saved < num_samples)
    //         return false;
    //     else
    //         return true;
    // };

    std::function schmidt_cox = [&](const std::vector<std::complex<float>> &rx_stream, const size_t &rx_stream_size, const uhd::time_spec_t &rx_timer)
    {
        std::complex<float> samp_1(0.0), samp_2(0.0), samp_3(0.0);
        // std::vector<std::complex<float>> P_samps;

        for (int i = 0; i < rx_stream.size(); ++i)
        {
            if (i < 2 * N_zfc)
                samp_1 = saved_buffer[i];
            else
                samp_1 = rx_stream[i - 2 * N_zfc];

            if (i < N_zfc)
                samp_2 = saved_buffer[i + N_zfc];
            else
                samp_2 = rx_stream[i - N_zfc];

            samp_3 = rx_stream[i];

            P = P + (std::conj(samp_2) * samp_3) - (std::conj(samp_1) * samp_2);

            if (buffer_init)
                R = R + std::norm(samp_3) - std::norm(samp_2);
            else
            {
                if (i < 2 * N_zfc)
                    R = R + std::norm(samp_3);
                else
                    buffer_init = true;
            }

            M = std::norm(P) / std::max(R, float(1e-6));
            // if (M_max < M)
            //     M_max = M;

            if (M > M_threshold)
            {
                // LOG_INFO_FMT("UP -- (%4%) |P|^2 = %1%, R = %2%, M = %3%", std::norm(P), R, M, i);
                saved_P.pop_front();
                saved_P.push_back(P);

                if (not detection_flag)
                    detection_flag = true;

                ++counter;
            }
            else
            {
                if (detection_flag)
                {
                    if ((counter < N_zfc * (reps_zfc - 1)) or (counter > N_zfc * (reps_zfc + ex_save_mul)))
                    {
                        LOG_DEBUG_FMT("Resetting counter for detection! Counter = %1%", counter);
                        detection_flag = false;
                        saved_P.clear();
                        saved_P.resize(capacity);
                        counter = 0;
                        continue;
                    }

                    // LOG_INFO_FMT("DOWN -- (%4%) |P|^2 = %1%, R = %2%, M = %3%", std::norm(P), R, M, i);
                    saved_P.pop_front();
                    saved_P.push_back(P);
                    if (extra > save_extra)
                    {
                        int ref_start = (i - counter - save_extra) + std::floor(counter / 2) - int(std::floor(N_zfc * reps_zfc / 2) + N_zfc);
                        LOG_DEBUG_FMT("Ref start index count = %1%", ref_start);
                        ref_start_timer = rx_timer + uhd::time_spec_t(double(ref_start / rx_rate));
                        return true;
                    }
                    else
                        ++extra;
                }
            }

            if (i >= rx_stream.size() - 2 * N_zfc)
                saved_buffer[i - (rx_stream.size() - 2 * N_zfc)] = samp_3;
        }

        num_samples_saved += rx_stream.size();
        // LOG_INFO_FMT("(%4%) |P|^2 = %1%, R = %2%, M = %3%", std::norm(P), R, M, num_samples_saved);
        if (num_samples_saved < num_samples)
            return false;
        else
            return true;
    };

    usrp_classobj.receive_continuously_with_callback(stop_signal_called, schmidt_cox);

    LOG_INFO_FMT("REF timer = %1%, Current timer = %2%", ref_start_timer.get_tick_count(rx_rate), usrp_classobj.usrp->get_time_now().get_tick_count(rx_rate));

    // LOG_INFO_FMT("M_max = %1%", M_max);

    std::string P_filename = projectDir + "/storage/P_data_" + device_id + "_" + curr_time_str + ".dat";
    std::ofstream P_save_stream;
    std::vector<std::complex<float>> save_data;
    save_data.insert(save_data.begin(), saved_P.begin(), saved_P.end());
    save_stream_to_file(P_filename, P_save_stream, save_data);
    P_save_stream.close();

    // std::string R_filename = projectDir + "/storage/R_data_" + device_id + "_" + curr_time_str + ".dat";
    // std::ofstream R_save_stream;
    // save_data.insert(save_data.begin(), saved_R.begin(), saved_R.end());
    // save_stream_to_file(R_filename, R_save_stream, save_data);
    // R_save_stream.close();

    LOG_INFO_FMT("Reception over! Total number of samples saved = %1%", num_samples_saved);

    return EXIT_SUCCESS;
};