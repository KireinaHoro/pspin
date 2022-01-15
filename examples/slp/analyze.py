#!/usr/bin/env python3

import json
import gzip
import sys
import pickle
from parse import parse
from os.path import join, getsize, getmtime
from os import system
from itertools import product
from concurrent.futures.process import ProcessPoolExecutor
from concurrent.futures import as_completed, Future

import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter, PercentFormatter, StrMethodFormatter
import numpy as np
import pandas as pd
import seaborn as sns

APP_NAME = 'slp_l1'
SIZE_MAP = {
    'int8_t': 1,
    'int16_t': 2,
    'int32_t': 4,
    'float': 4,
}
P_CANDIDATES = [2 ** k for k in range(3, 11)]
S_CANDIDATES = [128, 256, 512, 1024]
P_CUTOFF = 256
VLEN_CANDIDATES = [2 ** k for k in range(3, 6)]

CHARTS_OUTPUT = 'charts/'
system(f'mkdir -p {CHARTS_OUTPUT}')

STYLE = {'figsize': (5, 2.7), 'layout': 'constrained'}

def single_trace(is_fit, p, s, vec_len, dtype):
    ty = 'fit' if is_fit else 'predict'
    base = f'data/eval-{vec_len}-{dtype}-p{p}-s{s}-{ty}'
    trace_gz = f'{base}.json.gz'

    if getsize(trace_gz) > 80 * 2 ** 20: # 80MB
        raise RuntimeError(f'{trace_gz} larger than 80 MB')
    with gzip.open(trace_gz) as f:
        trace = f.read()
    print(f'parsing trace {base}')

    # remove the last empty trace item
    trace = json.loads(trace)['traceEvents'][:-1]

    th = filter(lambda t: t['tid'] == f'{APP_NAME}_th', trace)
    last = list(th)[-1]
    end_time = int(last['ts']) # in ps; take last instruction in th
    hh = filter(lambda t: t['tid'] == f'{APP_NAME}_hh', trace)
    start_time = int(next(hh)['ts']) # in ps; take first instruction in hh

    time_ns = (end_time - start_time) / (1000.0 ** 1)

    with open(f'{base}.transcript.txt') as f:
        for l in f:
            if l.startswith('Fit batch size:'):
                fit_batch, predict_batch = [int(b) for b in parse('Fit batch size: {}; predict batch size: {}', l)]

    batch = fit_batch if is_fit else predict_batch
    nelem = (vec_len + 1) if is_fit else vec_len
    nops = (5 * vec_len + 4) if is_fit else (2 * vec_len + 1)
    good_payload = nelem * SIZE_MAP[dtype] * batch * p
    giops = nops * batch * p / time_ns
    good_put_gbps = good_payload / time_ns * 8

    def find_lock(trace):
        # jal to unlock
        jal_fit = filter(lambda t: t['tid'] == 'fit_batch' and t['name'] == 'jal', trace)
        # x12:00000000 for acquired lock
        last_bne = filter(lambda t: t['tid'] == 'futex_lock_s' and t['name'] == 'bne' and t['args']['instr'][-1] == '0', trace)
        unlocks = map(lambda l: ((l[1]['ts'] - l[0]['ts'])/1000, parse('./trace_core_0{}_{}.log', l[1]['pid'])), zip(jal_fit, last_bne))
        return unlocks

    lock_acc = {}
    if is_fit:
        for dur, (cid, hid) in find_lock(trace):
            id = int(cid) * 8 + int(hid)
            if id not in lock_acc:
                lock_acc[id] = [dur]
            else:
                lock_acc[id] += [dur]

    #print(trial_name, lock_acc)

    return batch, good_payload, giops, good_put_gbps, lock_acc, time_ns

def parse_trace(is_fit, p, s, vlen, dtype):
    #### predict data
    # packet number -> [(packet size, giops, gbps)]
    cluster_number = [{}, {}]
    # packet size -> [(packet number, giops, gbps)]
    cluster_size = [{}, {}]
    # max throughput across all traces
    max_tput = 0
    #### fit data
    # number of participating hpus -> [spin_time]
    # mean of local spin time vs. remote spin time (hpuid >= 8)
    spin_map = pd.DataFrame(columns=['nhpus', 'dur', 'is_remote'])
    # number of participating hpus -> spin ratio
    spin_ratio_map = {}

    try:
        batch, pld_size, giops, gbps, lock_acc, time_ns = single_trace(is_fit, p, s, vlen, dtype)
    except (FileNotFoundError, RuntimeError) as e:
        print(f'missing trace: {e}')
        return
    except TypeError: # the trace is skipped
        return
    max_tput = max(gbps, max_tput)
    print(f'max_tput: {max_tput}')

    is_fit = int(is_fit)
    if p not in cluster_number[is_fit]:
        cluster_number[is_fit][p] = [(s, giops, gbps)]
    else:
        cluster_number[is_fit][p] += [(s, giops, gbps)]

    if s not in cluster_size[is_fit]:
        cluster_size[is_fit][s] = [(p, giops, gbps)]
    else:
        cluster_size[is_fit][s] += [(p, giops, gbps)]

    if is_fit:
        nhpus = len(lock_acc)
        if nhpus not in spin_ratio_map:
            spin_ratio_map[nhpus] = []

        for id, durs in lock_acc.items():
            is_remote = id >= 8
            wait_sum = 0
            for dur in durs:
                spin_map = spin_map.append({'nhpus': nhpus, 'dur': dur, 'is_remote': is_remote}, ignore_index=True)
                wait_sum += dur
            spin_ratio_map[nhpus] += [(wait_sum / time_ns, p, s)]

    print('finished parsing trace')
    return cluster_number, cluster_size, spin_map, spin_ratio_map, max_tput

def load_data(pool, vlen, dtype, armap):
    arlist = []
    armap[vlen, dtype] = arlist
    try:
        with open(f'dump_{vlen}_{dtype}.pickle', 'rb') as f:
            fut = Future()
            res = pickle.load(f)
            fut.set_result(res)
            arlist.append(fut)
            return
    except (FileNotFoundError, EOFError):
        print(f'Parsed archive not found for {vlen} {dtype}, reloading')

    print(f'spawning for VLEN={vlen} DTYPE={dtype}')

    for p, s in product(P_CANDIDATES, S_CANDIDATES):
        arlist.append(pool.submit(parse_trace, False, p, s, vlen, dtype))
        if p <= P_CUTOFF:
            arlist.append(pool.submit(parse_trace, True, p, s, vlen, dtype))

def combine_results(arlist, vlen, dtype):
    #### predict data
    # packet number -> [(packet size, giops, gbps)]
    cluster_number = [{}, {}]
    # packet size -> [(packet number, giops, gbps)]
    cluster_size = [{}, {}]
    # max throughput across all traces
    max_tput = 0
    #### fit data
    # number of participating hpus -> [spin_time]
    # mean of local spin time vs. remote spin time (hpuid >= 8)
    spin_map = pd.DataFrame(columns=['nhpus', 'dur', 'is_remote'])
    # number of participating hpus -> spin ratio
    spin_ratio_map = {}

    def merge_dict_of_lists(result, part):
        for k, v in part.items():
            if k not in result:
                result[k] = v
            else:
                result[k] += v

    for ar in as_completed(arlist):
        res = ar.result()
        if not res:
            continue
        cn, cs, sm, sr, mt = res
        merge_dict_of_lists(cluster_number[0], cn[0])
        merge_dict_of_lists(cluster_number[1], cn[1])
        merge_dict_of_lists(cluster_size[0], cs[0])
        merge_dict_of_lists(cluster_size[1], cs[1])
        spin_map = spin_map.append(sm, ignore_index=True)
        merge_dict_of_lists(spin_ratio_map, sr)
        max_tput = max(max_tput, mt)
        print(f'max_tput: {max_tput}')

    res = cluster_number, cluster_size, spin_map, spin_ratio_map, max_tput
    with open(f'dump_{vlen}_{dtype}.pickle', 'wb') as f:
        pickle.dump(res, f)

    print(f'finished for VLEN={vlen} DTYPE={dtype}')
    return res

max_tput_map = {}
def plot_data(vlen, dtype, cluster_number, cluster_size, spin_map, spin_ratio_map, max_tput):
    max_tput_map[vlen, dtype] = max_tput
    def do_lines(x, xlabel, y_tagged_l, ylabel, title):
        fig, ax = plt.subplots(**STYLE)
        for y, yt in y_tagged_l:
            # pad to same length
            padded_y = y + [float('nan')] * (len(x) - len(y))
            ax.plot(x, padded_y, label=yt, marker='o', markersize=3)
        ax.set_xlabel(xlabel)
        ax.set_xscale('log', base=2)
        ax.set_ylabel(ylabel)
        ax.set_yscale('log', base=2)
        for axis in [ax.xaxis, ax.yaxis]:
            axis.set_major_formatter(ScalarFormatter())
        ax.set_title(f'{title} | VLEN={vlen} {dtype}')
        ax.legend()
        fig.savefig(f'{CHARTS_OUTPUT}/{title}-{vlen}-{dtype}.pdf')
        plt.close(fig)

    # Predict: packet number - packet size - Gbps
    # Predict: packet size - packet number - Gbps
    fig, (ax1, ax2) = plt.subplots(1, 2, sharey=True, **STYLE)
    for y, yt in [([t[2] for t in v], str(k)) for k, v in cluster_number[0].items()]:
        # pad to same length
        x = S_CANDIDATES
        padded_y = y + [float('nan')] * (len(x) - len(y))
        ax1.plot(x, padded_y, label=yt, marker='o', markersize=3)
    for y, yt in [([t[2] for t in v], str(k)) for k, v in cluster_size[0].items()]:
        # pad to same length
        x = P_CANDIDATES
        padded_y = y + [float('nan')] * (len(x) - len(y))
        ax2.plot(x, padded_y, label=yt, marker='o', markersize=3)
    fig.suptitle(f'VLEN={vlen} {dtype}')
    ax1.set_xlabel('packet size/bytes')
    ax1.set_xscale('log', base=2)
    ax2.set_xlabel('packet number')
    ax2.set_xscale('log', base=2)
    ax1.set_ylabel('Gbps')
    ax1.set_yscale('log', base=2)
    for axis in [ax1.xaxis, ax1.yaxis, ax2.xaxis, ax2.yaxis]:
        axis.set_major_formatter(ScalarFormatter())
    ax1.legend()
    ax2.legend()
    fig.savefig(f'{CHARTS_OUTPUT}/predict-{vlen}-{dtype}.pdf')
    plt.close(fig)

    # Fit: packet number - packet size - Gbps
    xdata = S_CANDIDATES
    y_tagged = [([t[2] for t in v], str(k)) for k, v in cluster_number[1].items()]
    do_lines(xdata, 'packet size/bytes', y_tagged, 'Gbps', 'Fit-#packets')

    # Fit: packet size - packet number - Gbps
    xdata = list(filter(lambda p: p <= P_CUTOFF, P_CANDIDATES))
    y_tagged = [([t[2] for t in v], str(k)) for k, v in cluster_size[1].items()]
    do_lines(xdata, 'packet number', y_tagged, 'Gbps', 'Fit-packet size')

    # Fit: spin ratio
    to_df = {'nhpus': [], 'spin_ratio': []}
    for k, v in spin_ratio_map.items():
        for dpt in v:
            to_df['nhpus'] += [k]
            to_df['spin_ratio'] += [dpt[0]]
    data = pd.DataFrame.from_dict(to_df)

    # Fit: spin map violin plot
    # transform y data into log scale, plot linearly, use antilog labels
    spin_map['dur'] = spin_map['dur'].map(lambda a: np.log10(a))

    fig = plt.figure(**(STYLE | {'figsize': (5, 4)}))
    gs = fig.add_gridspec(2, hspace=0)
    ax1, ax2 = gs.subplots(sharex=True)
    #fig, (ax1, ax2) = plt.subplots(2, sharex=True, **STYLE)
    fig.suptitle(f'VLEN={vlen} {dtype}')
    sns.violinplot(x='nhpus', y='spin_ratio', cut=0, data=data, ax=ax1)
    sns.violinplot(x='nhpus', y='dur', cut=0, hue='is_remote', data=spin_map, ax=ax2)
    #ax2.set_xlabel('nhpus')
    ax1.yaxis.set_major_formatter(PercentFormatter(1.0))
    ax2.yaxis.set_major_formatter(StrMethodFormatter('$10^{{{x:.0f}}}$'))
    ax1.label_outer()
    ax2.label_outer()
    ax2.legend_.remove()

    fig.savefig(f'{CHARTS_OUTPUT}/spin-violin-{vlen}-{dtype}.pdf')
    plt.close(fig)

def consume_data(armap, vlen, dtype):
    arlist = armap[vlen, dtype]
    dat = combine_results(arlist, vlen, dtype)
    try:
        plot_data(vlen, dtype, *dat)
    except ValueError as e:
        print(f'failed to plot something for VLEN={vlen} DTYPE={dtype}: {e}')

if __name__ == '__main__':
    sns.color_palette("tab10")

    with ProcessPoolExecutor(max_workers=4) as pool:
        armap = {}
        for vlen in VLEN_CANDIDATES:
            for dtype in SIZE_MAP.keys():
                load_data(pool, vlen, dtype, armap)
        for vlen in VLEN_CANDIDATES:
            for dtype in SIZE_MAP.keys():
                consume_data(armap, vlen, dtype)

    # throughput bar plot
    dtypes = ['int8_t', 'int16_t', 'int32_t', 'float']
    vlens = [8, 16, 32]
    vlen_datas = [[] for _ in vlens]
    x = np.arange(len(dtypes))
    width = 0.2

    for idx, vl in enumerate(vlens):
        for dt in dtypes:
            vlen_datas[idx].append(max_tput_map[vl, dt])
    fig, ax = plt.subplots(**STYLE)
    for idx, (vdt, vlen) in enumerate(zip(vlen_datas, vlens)):
        ax.bar(x + (idx - 1) * width, vdt, width, label=f'VLEN={vlen}')

    ax.set_ylabel('Throughput/Gbps')
    ax.set_xticks(x, dtypes)
    ax.legend()
    fig.savefig(f'{CHARTS_OUTPUT}/vlen-dtype-tput.pdf')
    plt.close(fig)

    for (vlen, dtype), tput in max_tput_map.items():
        print(f'VLEN={vlen}\tDTYPE={dtype}\t{tput} Gbps')
