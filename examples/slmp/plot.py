#!/usr/bin/env python3

import csv
import argparse
import re
import numpy as np
import scipy.stats as st
import pandas as pd
import sys
from math import ceil
from itertools import chain, product
from os import listdir
from os.path import isfile, join

import matplotlib.pyplot as plt
import matplotlib.text as mtext
import matplotlib.colors as colors
from matplotlib.ticker import FuncFormatter, ScalarFormatter
from matplotlib.cm import ScalarMappable
from matplotlib import container, colormaps
import seaborn as sns

from si_prefix import si_format

parser = argparse.ArgumentParser(
    prog='plot.py',
    description='Plot data generated by the slmp file transfer benchmark',
    epilog='Report bugs to Pengcheng Xu <pengxu@ethz.ch>.'
)

parser.add_argument('--data_root', help='root of the CSV files from the SLMP benchmark', default=None)
parser.add_argument('--query', action='store_true', help='query data interactively')

args = parser.parse_args()

def cycles_to_us(cycles):
    return 1e6 / pspin_freq * cycles

pspin_freq = 40e6 # 40 MHz
dat_pkl = 'data.pkl'

normal_label = 'Default'
ack_label = 'Force ACK'
slmp_payload_size = 1462 // 4 * 4

def consume_trials(key, trials):
    # we do not distinguish host dma and notification here
    # since host is notified on every write
    def append_row(len, type, elapsed, cycles, msg, pkt, dma, notification):
        global data
        num_pkts = ceil(len / slmp_payload_size)
        tputs = [len * 8 / 1e6 / e for e in elapsed]

        # 95% confidence interval
        tput_median = np.median(tputs)
        bootstrap_ci = st.bootstrap((tputs,), np.median, confidence_level=0.95, method='percentile')
        ci_lo, ci_hi = bootstrap_ci.confidence_interval

        # TODO: breakdown

        entry = pd.DataFrame.from_dict({
            'len': [len / 1e3], # KB
            'type': [type],
            'elapsed': [elapsed],
            'pkt': [pkt],
            'msg': [msg],
            'dma': [dma],
            'notification': [notification],
            'cycles': [cycles],
            # derived
            'tput': [tput_median],
            'tput_lo': [tput_median - ci_lo],
            'tput_hi': [ci_hi - tput_median],
        })
        data = pd.concat([data, entry], ignore_index=True)

    for l in trials:
        if key == normal_label:
            prefix = f'0-{l}'
        elif key == ack_label:
            prefix = f'1-{l}'
        else:
            raise ValueError(key)

        with open(join(args.data_root, f'{prefix}.csv'), 'r') as f:
            reader = csv.reader(f)
            assert next(reader) == ['cycles', 'msg', 'pkt', 'dma', 'notification']
            handler_values = [cycles_to_us(float(x)) for x in next(reader)]

        with open(join(args.data_root, f'{prefix}-sender.txt'), 'r') as f:
            lines = filter(lambda l: l.find('Elapsed: ') != -1, f.readlines())

        elapsed_values = [float(l.split(' ')[1]) for l in lines]

        append_row(int(l), key, elapsed_values, *handler_values)

if args.data_root:
    def gen_trial():
        x = 100
        while x <= 2000000:
            yield x
            x *= 2
    trials = list(gen_trial())

    data = pd.DataFrame(columns=[
        'len',
        'type',
        'elapsed',
        'pkt',
        'msg',
        'dma',
        'notification',
        'cycles',
        # derived
        'tput',
        'tput_lo',
        'tput_hi',
    ])
    consume_trials(normal_label, trials)
    consume_trials(ack_label, trials)

    data['len'] = data['len'].astype(int)
    data.to_pickle(dat_pkl)

params = {
    'font.family': 'Helvetica Neue',
    'font.weight': 'light',
    'font.size': 9,
    'axes.titleweight': 'normal',
    'figure.autolayout': True,
}
plt.rcParams.update(params)
figwidth=5.125
def figsize(aspect_ratio):
    return (figwidth, figwidth/aspect_ratio)

dp: pd.DataFrame = pd.read_pickle(dat_pkl)
if args.query:
    import code
    code.InteractiveConsole(locals=globals()).interact()
    sys.exit(0)

# Throughput
fig, ax = plt.subplots(figsize=figsize(5/3))

task_dict = {}
for lbl in [normal_label, ack_label]:
    trial = dp[dp['type'] == lbl]
    trial.plot(x='len', y='tput', yerr=(trial['tput_lo'], trial['tput_hi']), ax=ax, ecolor='black', label=lbl)

ax.grid(which='both')
ax.set_xlabel('File Length (KB)')
ax.set_ylabel('Throughput (Mbps)')
ax.get_legend().remove()

ax.legend(bbox_to_anchor=(1.03, .5), loc='center left')
fig.savefig('slmp-tput.pdf')

sys.exit(0)

indices = ['cycles', 'real_handler', 'host_dma', 'sender']
index_labels = ['Syscall', 'Handler', 'Host Proc.', 'Sender']

# stackplot for components
fig, axes = plt.subplots(2, 2, sharey=True, sharex=True, figsize=figsize(5/4))
ax_labels = [[icmp_pspin_label, icmp_combined_label], [udp_pspin_label, udp_combined_label]]
did_labels = False
for ax, lbl in zip(chain(*axes), chain(*ax_labels)):
    ax.set_xlabel('Payload Length (B)')
    ax.set_ylabel('Latency (us)')
    ax.set_yticks(range(0, 300, 100))
    ax.set_yticks(range(0, 300, 20), minor=True)

    ax.grid(which='minor', alpha=0.2)
    ax.grid(which='major', alpha=0.5)

    ax.set_title(lbl)
    
    trial = dp[dp['type'] == lbl]
    
    host_lbl = icmp_baseline_label if 'ICMP' in lbl else udp_baseline_label
    host_avg = dp[dp['type'] == host_lbl]['e2e'].median()
    if not did_labels:
        did_labels = True
        ax.stackplot('len', *indices, labels=index_labels, data=trial)
        ax.axhline(y=host_avg, linestyle='--', color='purple', label='Baseline')
    else:
        ax.stackplot('len', *indices, data=trial)
        ax.axhline(y=host_avg, linestyle='--', color='purple')

    # plot error bars of e2e latencies
    ax.errorbar('len', 'e2e', (trial['e2e_lo'], trial['e2e_hi']), data=trial, fmt='none', label=None, ecolor='black')

fig.legend(loc='center right')
fig.tight_layout(rect=[0, 0, .82, 1])
fig.savefig('pingpong-breakdown.pdf')