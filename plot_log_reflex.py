import numpy as np
import matplotlib.pyplot as plt
import sys


log_file = sys.argv[1]
with open(log_file, 'r') as f:
    lines = f.readlines()

log_cpu = []
log_tx = []
log_rx = []
# log_data[0] = [#req/sec,txbytes/sec,rxbytes/sec]
for line in lines:
    log_data = line.split()
    if log_data[0] != '0':
        log_cpu.append(int(log_data[0]))
        log_tx.append(int(log_data[1]))
        log_rx.append(int(log_data[2]))

fig = plt.figure()
plt.tight_layout()
log_cpu = np.array(log_cpu)
plt.subplot(3,1,1)
plt.plot(log_cpu)
plt.ylabel('requests/sec')

log_tx = np.array(log_tx)*8.0/1e9 #Gb/s
plt.subplot(3,1,2)
plt.plot(log_tx)
plt.ylabel('TX Gb/s')

log_rx = np.array(log_rx)*8.0/1e9 #Gb/s
plt.subplot(3,1,3)
plt.plot(log_rx)
plt.ylabel('RX Gb/s')

plt.suptitle('ReFlex monitoring (' + log_file + ')')
plt.savefig(log_file+'-network.png')
plt.show()
'''
fig = plt.figure()
plt.plot(log_rx)
#plt.title('rx Gb/s')
plt.ylabel('RX Gb/s')
plt.suptitle('ReFlex RX network monitoring (' + log_file + ')')
#plt.savefig(log_file+'-network.png')
plt.show()
'''

print 'reqs/sec (mean, stddev, peak):'
print np.mean(log_cpu)
print np.std(log_cpu)
print np.amax(log_cpu)

print '\ntxbytes/sec:'
print np.mean(log_tx)
print np.std(log_tx)
print np.amax(log_tx)

print '\nrxbytes/sec:'
print np.mean(log_rx)
print np.std(log_rx)
print np.amax(log_rx)
