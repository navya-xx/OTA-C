from uhd.usrp.cal.visa import VISADevice

class MyVISADevice(VISADevice):
    # *USB0::2733::72::102293::0::INSTR
    res_ids = {
        r'USB\d+::2733::72::\d+::0::INSTR': 'R&S SMA-100A',
        r'USB\d+::0x0AAD::0x0048::\d+::INSTR': 'R&S SMA-100A'
    }

    def init_power_meter(self):
        """
        Enable the sensor to read power
        """
        raise RuntimeError("This device cannot be used for power measurement!")

    def init_signal_generator(self):
        """
        This class is for power meters, so no bueno
        """
        self.res.timeout = 5000
        self.res.write("*RST")

    def set_frequency(self, freq):
        """
        Set frequency
        """
        self.res.write('SOUR:FREQ{}'.format(freq))

    def get_power_dbm(self):
        """
        Return measured power in dBm
        """
        return float(self.res.query('SOUR:POW:LEV?')) 

    def set_power_dbm(self, power_dbm):
        """
        Return measured power in dBm
        """
        self.res.write('SOUR:POW:LEV {}'.format(power_dbm))
        return float(self.res.query('SOUR:POW:LEV?')) 

    def enable(self, enable):
        """
        Enables RF Out
        """
        self.res.write('OUTP:STAT {}'.format(enable))
