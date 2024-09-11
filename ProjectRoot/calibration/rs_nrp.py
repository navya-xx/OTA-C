from uhd.usrp.cal.visa import VISADevice

class MyVISADevice(VISADevice):
    """
    USB-based power measurement devices
    """
    # The keys of res_ids are regular expressions, which have to match the
    # resource ID of the VISA device. The values are a name for the device, which
    # is used for informing the user which driver was found.
    #
    # A class can match any number of resource IDs. If the commands used depend
    # on the specific ID, it can be queried in the appropriate init function
    # using the *IDN? command which is understood by all VISA devices.
    # USB0::2733::347::101567::0::INSTR
    # USB0::2733::352::100926::0::INSTR
    # *USB0::2733::27::106702::0::INSTR
    # 0x0AAD::0x001B:
    res_ids = {
        r'USB\d+::2733::376::\d+::0::INSTR': 'R&S NRP-6A',
        r'USB\d+::2733::352::\d+::0::INSTR': 'R&S NRP40SN',
        r'USB\d+::2733::27::\d+::0::INSTR': 'R&S NRP2',
        r'USB\d+::0x0AAD::0x001B::\d+::INSTR': 'R&S NRP2',
        r'USB\d+::2733::347::\d+::0::INSTR': 'R&S NRQ6',
    }

    def init_power_meter(self):
        """
        Enable the sensor to read power
        """
        self.res.timeout = 10000
        self.res.write("SENS:AVER:COUN 20")
        self.res.write("SENS:AVER:COUN:AUTO ON")
        self.res.write('UNIT:POW DBM')
        self.res.write('SENS:FUNC "POW:AVG"')

    def init_signal_generator(self):
        """
        This class is for power meters, so no bueno
        """
        raise RuntimeError("This device cannot be used for signal generation!")

    def set_frequency(self, freq):
        """
        Set frequency
        """
        self.res.write('SENS:FREQ {}'.format(freq))

    def get_power_dbm(self):
        """
        Return measured power in dBm
        """
        self.res.write('INIT:IMM')
        return float(self.res.query('FETCH?'))
