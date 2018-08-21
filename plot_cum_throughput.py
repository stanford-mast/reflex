#!/usr/bin/env python

import warnings
warnings.filterwarnings("ignore", message="numpy.dtype size changed")
warnings.filterwarnings("ignore", message="numpy.ufunc size changed")
import os
import sys
import numpy as np 
from io import StringIO 
import matplotlib.pyplot as plt
import pandas as pd
import matplotlib.ticker as ticker


#datadir = sys.argv[1]
#netstat_dir = os.path.join(datadir, "netstats")
#rxfilename = "rxstats.txt"
#txfilename = "txstats.txt"



gets = {}
puts = {}
REQ_SIZE = 65536
for filename in sys.argv[1:]:
        print "FILENAME: ", filename
	f = open(filename, 'r')
	for line in f:
		data = np.loadtxt(StringIO(unicode(line)), delimiter='\t') 
		time = data[0]
		put_MBps = data[1] * REQ_SIZE
		get_MBps = data[2] * REQ_SIZE

		if put_MBps == 0 and get_MBps == 0:
			continue

		if time in gets:
			gets[time] = gets[time] + get_MBps
		else:
			gets[time] = get_MBps

		if time in puts:
			puts[time] = puts[time] + put_MBps
		else:
			puts[time] = put_MBps



lists = sorted(gets.items())
x, y = zip(*lists)
plt.plot(x, y)
plt.xlabel('Time(s)')
plt.ylabel('Cumulative GB/s')
plt.show()
exit(0)

