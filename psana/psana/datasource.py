import sys, os
import time
import getopt
import pprint

from psana import dgram
from psana.pydgram import PyDgram
from psana.event import Event

class DataSource:
    """Stores variables and arrays loaded from an XTC source.\n"""
    def __init__(self, expstr, config_bytes=[], filter=None, analyze=None):
        # expstr is 'exp=expid:run=runno' or xtc filename
        if isinstance(expstr, (list)):
            self.xtc_files = expstr
        elif os.path.isfile(expstr):
            self.xtc_files = [expstr]
        else:
            self.xtc_files = ['0.smd.xtc','1.smd.xtc'] # Todo: where from?
        assert len(self.xtc_files) > 0
        
        has_configs = True if len(config_bytes) > 0 else False
        
        self.configs = []
        self.fds = []
        for i, xtcdata_filename in enumerate(self.xtc_files):
            self.fds.append(os.open(xtcdata_filename,
                            os.O_RDONLY|os.O_LARGEFILE))
            if not has_configs: 
                config = PyDgram(dgram.Dgram(file_descriptor=self.fds[-1])) 
            else:
                config = PyDgram(dgram.Dgram(file_descriptor=self.fds[-1], view=config_bytes[i]))
            self.configs.append(config) 
        
    def __iter__(self):
        return self

    def __next__(self):
        return self.jump()

    def jump(self, offsets=[]):
        dgrams = []
        if len(offsets) == 0: offsets = [0]*len(self.fds)
        for fd, config, offset in zip(self.fds,self.configs, offsets):
            _config = dgram.Dgram(file_descriptor=fd, view=config.buf)
            if offset == 0:
                d = dgram.Dgram(config=_config)                # read sequentially
            else:
                d = dgram.Dgram(config=_config, offset=offset) # read rax
            dgrams.append(PyDgram(d))
            PyDgram(_config)                                   # dealloc
        return Event(dgrams=dgrams)
    
def parse_command_line():
    opts, args_proper = getopt.getopt(sys.argv[1:], 'hvd:f:')
    xtcdata_filename="data.xtc"
    for option, parameter in opts:
        if option=='-h': usage_error()
        if option=='-f': xtcdata_filename = parameter
    if xtcdata_filename is None:
        xtcdata_filename="data.xtc"
    return (args_proper, xtcdata_filename)

def getMemUsage():
    pid=os.getpid()
    ppid=os.getppid()
    cmd="/usr/bin/ps -q %d --no-headers -eo size" % pid
    p=os.popen(cmd)
    size=int(p.read())
    return size

def main():
    args_proper, xtcdata_filename = parse_command_line()
    ds=DataSource(xtcdata_filename)
    print("vars(ds):")
    for var_name in sorted(vars(ds)):
        print("  %s:" % var_name)
        e=getattr(ds, var_name)
        if not isinstance(e, (tuple, list, int, float, str)):
            for key in sorted(e.__dict__.keys()):
                print("%s: %s" % (key, e.__dict__[key]))
    print()
    count=0
    for evt in ds:
        print("evt:", count)
        for pydgram in evt:
            for var_name in sorted(vars(pydgram)):
                val=getattr(pydgram, var_name)
                print("  %s: %s" % (var_name, type(val)))
            a=pydgram.xpphsd.raw.array0Pgp
            try:
                a[0][0]=999
            except ValueError:
                print("The pydgram.xpphsd.raw.array0Pgp is read-only, as it should be.")
            else:
                print("Warning: the evt.array0_pgp array is writable")
            print()
        count+=1
    return

def usage_error():
    s="usage: python %s" %  os.path.basename(sys.argv[0])
    sys.stdout.write("%s [-h]\n" % s)
    sys.stdout.write("%s [-f xtcdata_filename]\n" % (" "*len(s)))
    sys.exit(1)

if __name__=='__main__':
    main()

