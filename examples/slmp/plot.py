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
num_hpus = 16
dat_pkl = 'data.pkl'
loss_pkl = 'loss.pkl'

slmp_payload_size = 1462 // 4 * 4

def consume_trials(wnd_sz, tc, trials):
    # we do not distinguish host dma and notification here
    # since host is notified on every write
    def append_row(len, wnd_sz, tc, elapsed, lost, cycles, msg, pkt, dma, notification, headtail):
        global data
        num_pkts = ceil(len / slmp_payload_size)
        tputs = [len * 8 / 1e6 / e for e in elapsed]

        # 95% confidence interval
        if not tputs:
            tputs = [0]

        tput_median = np.median(tputs)
        if tputs.__len__() > 1:
            bootstrap_ci = st.bootstrap((tputs,), np.median, confidence_level=0.95, method='percentile')
            ci_lo, ci_hi = bootstrap_ci.confidence_interval
        else:
            ci_lo, ci_hi = tput_median, tput_median

        real_pkt = cycles_to_us((pkt - dma) * num_pkts / num_hpus)
        real_dma = cycles_to_us((dma - cycles) * num_pkts / num_hpus)

        real_headtail = cycles_to_us(headtail - cycles * 2.5)
        real_notification = cycles_to_us(notification - cycles)
        all_cycles = cycles_to_us(cycles * (4 * num_pkts / num_hpus + 7))

        el_mean = np.mean(elapsed)

        sender = el_mean - real_headtail - real_pkt - real_dma - real_notification - all_cycles

        tlen = tputs.__len__()

        entry = pd.DataFrame.from_dict({
            'len': [len],
            'wnd_sz': [wnd_sz],
            'tc': [tc],
            'elapsed': [elapsed],
            'pkt': [pkt],
            'headtail': [headtail],
            'msg': [msg],
            'dma': [dma],
            'notification': [notification],
            'cycles': [cycles],
            # derived
            'tput': [tput_median],
            'tput_lo': [tput_median - ci_lo],
            'tput_hi': [ci_hi - tput_median],
            'loss_ratio': [lost / (tlen + lost)],
            # normalised components
            'pkt_r': [real_pkt / el_mean],
            'headtail_r': [real_headtail / el_mean],
            'dma_r': [real_dma / el_mean],
            'notification_r': [real_notification / el_mean],
            'cycles_r': [all_cycles / el_mean],
            'sender_r': [sender / el_mean],
        })
        data = pd.concat([data, entry], ignore_index=True)

    for l in trials:
        prefix = f'{l}-{wnd_sz}-{tc}'

        try:
            with open(join(args.data_root, f'{prefix}.csv'), 'r') as f:
                reader = csv.reader(f)
                assert next(reader) == ['cycles', 'msg', 'pkt', 'dma', 'notification', 'headtail']
                handler_values = [cycles_to_us(float(x)) for x in next(reader)]
        except (FileNotFoundError, StopIteration) as e:
            print(f'Warning: exception consuming {prefix}: {e}')
            continue

        with open(join(args.data_root, f'{prefix}-sender.txt'), 'r') as f:
            lines = f.readlines()

        el = list(filter(lambda l: l.find('Elapsed: ') != -1, lines))
        timeouts = list(filter(lambda l: l.find('Timed out!') != -1, lines))

        elapsed_values = [float(l.split(' ')[1]) for l in el]

        append_row(int(l), wnd_sz, tc, elapsed_values, len(timeouts), *handler_values)

def gen_trial(start, end, step=2):
    x = start
    while x <= end:
        yield x
        x *= step

window_sizes = [*gen_trial(1, 1024, step=4)]
num_threads = [*gen_trial(1, 64)]

plot_curated = False
window_sizes_curated = [1, 4, 16, 250]
num_threads_curated = [1, 4, 16, 250]

if plot_curated:
    window_sizes_sel = window_sizes_curated
    num_threads_sel = num_threads_curated
else:
    window_sizes_sel = window_sizes
    num_threads_sel = num_threads

indices = [
        'cycles_r',
        'pkt_r',
        'headtail_r',
        'dma_r',
        'notification_r',
        'sender_r',
]

if args.data_root:
    trials = list(gen_trial(100, 128 * 1024 * 1024))

    data = pd.DataFrame(columns=[
        'len',
        'wnd_sz',
        'tc',
        'elapsed',
        'pkt',
        'headtail',
        'msg',
        'dma',
        'notification',
        'cycles',
        # derived
        'tput',
        'tput_lo',
        'tput_hi',
    ] + indices)

    for wnd_sz, tc in product(window_sizes, num_threads):
        consume_trials(wnd_sz, tc, trials)

    data['len'] = data['len'].astype(float)
    data.to_pickle(dat_pkl)

params = {
    'font.family': 'Helvetica Neue',
    'font.weight': 'light',
    'font.size': 9,
    'axes.titlesize': 'small',
    'axes.titleweight': 'light',
    'figure.autolayout': True,
}
plt.rcParams.update(params)
figwidth=5.125
def figsize(aspect_ratio):
    return (figwidth, figwidth/aspect_ratio)

# https://stackoverflow.com/a/71540238/5520728
class LegendTitle(object):
    def __init__(self, text_props=None):
        self.text_props = text_props or {}
        super(LegendTitle, self).__init__()

    def legend_artist(self, legend, orig_handle, fontsize, handlebox):
        x0, y0 = handlebox.xdescent, handlebox.ydescent
        title = mtext.Text(x0, y0, orig_handle, **self.text_props)
        handlebox.add_artist(title)
        return title

dp: pd.DataFrame = pd.read_pickle(dat_pkl)
if args.query:
    import code
    code.InteractiveConsole(locals=globals()).interact()
    sys.exit(0)

# Throughput
fig, axes = plt.subplots(2, 2, figsize=figsize(5/4))

legend_dict = {}
for i in range(2):
    for j in range(2):
        ax = axes[j][i]

        ax.sharey(axes[0][0])

        ylabel = 'Throughput (Mbps)'

        if j == 0:
            ax.set_xscale('log')
            x = 'len'
            xlabel = 'File Length (B)'
            x_formatter = FuncFormatter(lambda x, pos: si_format(x, precision=0))
            ax.xaxis.set_major_formatter(x_formatter)
        else:
            ax.set_xscale('log', base=2)
            ax.xaxis.set_major_formatter(ScalarFormatter())
            ax.xaxis.set_minor_formatter(ScalarFormatter())
            if i == 0:
                x = 'wnd_sz'
                xlabel = 'Window Size (#Packets)'
                ax.set_xbound(1, 1024)
            else:
                x = 'tc'
                xlabel = 'Thread Count'
                ax.set_xbound(1, 64)

        if i == 0:
            legends = num_threads_sel
            legend_cid = 'tc'
            legend_name = '#Threads'
            cm_name = 'winter_r'
        else:
            legends = window_sizes_sel
            legend_cid = 'wnd_sz'
            legend_name = 'Window'
            cm_name = 'autumn_r'

        norm = colors.LogNorm(min(legends), max(legends))
        colormap = colormaps.get_cmap(cm_name)
        
        for lbl in legends:
            color = colormap(norm(lbl))

            trial = dp[(dp[legend_cid] == lbl)]
            idx = trial.groupby(x)['tput'].transform(max) == trial['tput']
            trial = trial[idx].sort_values(x)

            trial = trial[trial['loss_ratio'] <= .9] # 20 out of 200
            ax.errorbar(x, 'tput', yerr=(trial['tput_lo'], trial['tput_hi']), data=trial, color=color, ecolor='black')

            # legend_dict.setdefault(legend_name, {})[lbl] = ax.lines[-1]

        ax.set_xlabel(xlabel)
        ax.set_ylabel(ylabel)

        cbar = fig.colorbar(ScalarMappable(norm=norm, cmap=colormap), ax=ax)
        cbar.ax.set_title(legend_name)
        cbar.ax.set_yscale('log', base=2)
        y_formatter = FuncFormatter(lambda y, pos: str(int(y)))
        cbar.ax.yaxis.set_major_formatter(y_formatter)
    
        # adapted from ax.label_outer()
        if i == 1:
            for label in ax.get_yticklabels(which="both"):
                label.set_visible(False)
            ax.get_yaxis().get_offset_text().set_visible(False)
            ax.set_ylabel("")

        # iperf theoretical data
        iperf_dat = [9.22, 10.4, 8.36, 8.49, 7.75, 8.66, 7.63, 8.60, 8.70, 7.89, 7.92, 8.16, 7.87, 8.07, 7.31, 7.78]
        iperf_dat = [x * 1000 for x in iperf_dat] # convert to Mbps

        iperf_line = ax.axhline(y=np.mean(iperf_dat), linestyle='--', color='purple')

        ax.grid(which='minor', alpha=0.2)
        ax.grid(which='major', alpha=0.5)

'''
# make section titles in legend
graphics, texts = [], []

graphics.append(iperf_line)
texts.append('IPerf3')

for k, kv in legend_dict.items():
    graphics.append('')
    texts.append('')

    graphics.append(k)
    texts.append('')

    for kk, vv in kv.items():
        graphics.append(vv)
        texts.append(kk)

fig.legend(graphics, texts, handler_map={str: LegendTitle({'weight': 'normal'})}, bbox_to_anchor=(1, .5), loc='center right')
fig.tight_layout(rect=[0, 0, .85, 1])
'''
fig.legend([iperf_line], ['IPerf3'], bbox_to_anchor=[0, 1], loc='upper left')
fig.savefig('slmp-tput.pdf')

fig, axes = plt.subplots(1, 2, sharey=True, figsize=figsize(2.1))

for i in range(2):
    ax = axes[i]

    ax.set_ylabel('Failure Rate')
    y_formatter = FuncFormatter(lambda y, pos: f'{int(y * 100)}%')
    ax.yaxis.set_major_formatter(y_formatter)
    ax.set_ybound(0, 1)

    ax.set_xscale('log', base=2)
    ax.xaxis.set_major_formatter(ScalarFormatter())
    ax.xaxis.set_minor_formatter(ScalarFormatter())
    if i == 0:
        x = 'wnd_sz'
        xlabel = 'Window Size (#Packets)'
        ax.set_xbound(1, 1024)
    else:
        x = 'tc'
        xlabel = 'Thread Count'
        ax.set_xbound(1, 64)

    if i == 0:
        legends = num_threads_sel
        legend_cid = 'tc'
        legend_name = '#Threads'
        cm_name = 'winter_r'
    else:
        legends = window_sizes_sel
        legend_cid = 'wnd_sz'
        legend_name = 'Window'
        cm_name = 'autumn_r'

    ax.set_xlabel(xlabel)
    ax.label_outer()

    norm = colors.LogNorm(min(legends), max(legends))
    colormap = colormaps.get_cmap(cm_name)

    cbar = fig.colorbar(ScalarMappable(norm=norm, cmap=colormap), ax=ax)
    cbar.ax.set_title(legend_name)
    cbar.ax.set_yscale('log', base=2)
    y_formatter = FuncFormatter(lambda y, pos: str(int(y)))
    cbar.ax.yaxis.set_major_formatter(y_formatter)
    
    for lbl in legends:
        color = colormap(norm(lbl))

        trial = dp[(dp[legend_cid] == lbl)]
        idx = trial.groupby(x)['tput'].transform(max) == trial['tput']
        trial = trial[idx].sort_values(x)

        ax.plot(x, 'loss_ratio', data=trial, color=color)

    ax.grid(which='minor', alpha=0.2)
    ax.grid(which='major', alpha=0.5)

graphics, texts = [], []

fig.savefig('slmp-loss.pdf')

sys.exit(0)

index_labels = ['Syscall', 'Handler(P)', 'Handler(H+T)', 'DMA', 'Host Proc.', 'Sender']

# stackplot for components
fig = plt
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