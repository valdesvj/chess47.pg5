#!/usr/bin/env python3
"""
NNUE training, step 3: a small network for Chess 47's evaluation, trained on tools/nnue/label.py's data (PyTorch,
the GPU if there is one), exported as int16 to src/nnue_chess.h / src/nnue_xq.h.

  python tools/nnue/train.py chess ~/opt/nnue-data/chess.lab [--hidden 128] [--epochs 30] [--out src/nnue_chess.h]
  python tools/nnue/train.py xq    ~/opt/nnue-data/xq.lab

The network: the pieces as seen by each side (chess 12 x 64 = 768 inputs, Xiangqi 14 x 90 = 1260; the board turned
over for Black), one shared layer of HIDDEN for each side (side to move first), clipped to 0..1, then one output:
  eval = out * SCALE (centipawns, side to move)
Trained on sigmoid(eval / K) against LAMBDA * sigmoid(engine score / K) + (1 - LAMBDA) * the game's result.
Quantized: the first layer x QA (255), the output layer x QB (64); the accumulators are int16.
(C) 2026 Victor Valdes - GPL-3.0-or-later
"""
import argparse
import math
import os
import sys
import time

import numpy as np
import torch

QA, QB, SCALE, K = 255, 64, 400, 400.0
PIECES = {'chess': 'PNBRQK', 'xq': 'KABNRCP'}       # Xiangqi FEN: K A B(elephant) N(horse) R C P
SIZE = {'chess': (8, 8), 'xq': (9, 10)}             # files, ranks
MAXF = 32                                           # pieces on the board at most


def features(game, fen):
    """the active inputs of White's / Red's view and Black's view, and the side to move (0 White / Red)"""
    names = PIECES[game]
    nt = len(names)
    files, ranks = SIZE[game]
    board, side = fen.split()[:2]
    w, b = [], []
    r = ranks - 1
    for row in board.split('/'):
        f = 0
        for ch in row:
            if ch.isdigit():
                f += int(ch)
                continue
            u = ch.upper()
            if game == 'xq' and u == 'E':
                u = 'B'
            if game == 'xq' and u == 'H':
                u = 'N'
            t = names.index(u)
            col = 0 if ch.isupper() else 1
            sq = r * files + f
            sqb = (ranks - 1 - r) * files + f            # the board turned over (Black's view)
            w.append((col * nt + t) * files * ranks + sq)
            b.append(((1 - col) * nt + t) * files * ranks + sqb)
            f += 1
        r -= 1
    return w, b, 0 if side == 'w' else 1


def load(game, path, cache):
    if os.path.exists(cache):
        z = np.load(cache)
        return z['fw'], z['fb'], z['stm'], z['score'], z['res']
    n = sum(1 for _ in open(path))
    nin = len(PIECES[game]) * 2 * SIZE[game][0] * SIZE[game][1]
    fw = np.full((n, MAXF), nin, np.int16)          # padding: index nin (a zero row)
    fb = np.full((n, MAXF), nin, np.int16)
    stm = np.zeros(n, np.int8)
    score = np.zeros(n, np.float32)
    res = np.zeros(n, np.float32)
    t0 = time.time()
    k = 0
    for line in open(path):
        fen, r, s = line.rstrip('\n').split('|')
        w, b, side = features(game, fen)
        if len(w) > MAXF:
            continue
        fw[k, :len(w)] = w
        fb[k, :len(b)] = b
        stm[k] = side
        score[k] = max(-3000, min(3000, int(s)))
        res[k] = float(r)
        k += 1
        if k % 1000000 == 0:
            print('  read %dM (%.0f s)' % (k // 1000000, time.time() - t0), file=sys.stderr, flush=True)
    fw, fb, stm, score, res = fw[:k], fb[:k], stm[:k], score[:k], res[:k]
    np.savez(cache, fw=fw, fb=fb, stm=stm, score=score, res=res)
    return fw, fb, stm, score, res


class Net(torch.nn.Module):
    def __init__(self, nin, hidden):
        super().__init__()
        self.ft = torch.nn.EmbeddingBag(nin + 1, hidden, mode='sum', padding_idx=nin)
        self.fb = torch.nn.Parameter(torch.zeros(hidden))
        self.out = torch.nn.Linear(2 * hidden, 1)
        with torch.no_grad():
            self.ft.weight.mul_(0.1)
            self.ft.weight[nin].zero_()

    def forward(self, us, them):
        a = torch.clamp(self.ft(us) + self.fb, 0, 1)
        b = torch.clamp(self.ft(them) + self.fb, 0, 1)
        return self.out(torch.cat([a, b], 1)).squeeze(1)

    def clip(self):
        lim = 32767 / QA / MAXF * 0.95                # an accumulator never overflows int16
        self.ft.weight.data.clamp_(-lim, lim)
        self.out.weight.data.clamp_(-32767 / QB, 32767 / QB)


def export(net, nin, hidden, game, path, info):
    w = net.ft.weight.detach().cpu().numpy()[:nin]           # [nin][hidden]
    fb = net.fb.detach().cpu().numpy()
    ow = net.out.weight.detach().cpu().numpy()[0]            # [2 hidden]
    ob = float(net.out.bias.detach().cpu().numpy()[0])
    q1 = np.round(w * QA).astype(np.int16)
    qb = np.round(fb * QA).astype(np.int16)
    q2 = np.round(ow * QB).astype(np.int16)
    qo = int(round(ob * QA * QB))
    G = 'NNC' if game == 'chess' else 'NNX'

    def arr(a, per=16):
        a = a.ravel()
        return ',\n'.join('  ' + ','.join(str(int(x)) for x in a[i:i + per]) for i in range(0, len(a), per))
    with open(path, 'w') as o:
        o.write('/*\n * CHESS47 for DM42n (DMCP5) - the %s evaluation network (tools/nnue/train.py, generated)\n'
                ' * %s\n * (C) 2026 Victor Valdes - GPL-3.0-or-later\n */\n' % (game, info))
        o.write('#define %s_IN %d\n#define %s_HID %d\n#define %s_QA %d\n#define %s_QB %d\n#define %s_SCALE %d\n'
                % (G, nin, G, hidden, G, QA, G, QB, G, SCALE))
        o.write('static const int16_t %s_W1[%d * %d] = {\n%s\n};\n' % (G, nin, hidden, arr(q1)))
        o.write('static const int16_t %s_B1[%d] = {\n%s\n};\n' % (G, hidden, arr(qb)))
        o.write('static const int16_t %s_W2[%d] = {\n%s\n};\n' % (G, 2 * hidden, arr(q2)))
        o.write('static const int32_t %s_B2 = %d;\n' % (G, qo))
    print('exported', path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', choices=['chess', 'xq'])
    ap.add_argument('data')
    ap.add_argument('--hidden', type=int, default=128)
    ap.add_argument('--epochs', type=int, default=30)
    ap.add_argument('--batch', type=int, default=16384)
    ap.add_argument('--lr', type=float, default=1e-3)
    ap.add_argument('--lam', type=float, default=0.75, help='the engine score\'s weight against the result')
    ap.add_argument('--out', default='')
    a = ap.parse_args()
    a.out = a.out or 'src/nnue_%s.h' % a.game
    dev = 'cuda' if torch.cuda.is_available() else 'cpu'
    print('device', dev, torch.cuda.get_device_name(0) if dev == 'cuda' else '')
    fw, fb, stm, score, res = load(a.game, a.data, a.data + '.npz')
    n = len(stm)
    nin = len(PIECES[a.game]) * 2 * SIZE[a.game][0] * SIZE[a.game][1]
    print('%d positions, %d inputs, hidden %d' % (n, nin, a.hidden))
    # side to move first; the targets from the side to move's view
    s = stm.astype(bool)
    us = np.where(s[:, None], fb, fw)
    them = np.where(s[:, None], fw, fb)
    sc = np.where(s, -score, score)
    rs = np.where(s, 1 - res, res)
    target = a.lam / (1 + np.exp(-sc / K)) + (1 - a.lam) * rs
    nval = min(200000, n // 20)
    T = lambda x, t: torch.from_numpy(np.ascontiguousarray(x)).to(dev, t)
    us_t, them_t, tg_t = T(us, torch.int32), T(them, torch.int32), T(target, torch.float32)
    tr, va = slice(nval, n), slice(0, nval)
    net = Net(nin, a.hidden).to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=a.lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, a.epochs)
    best = 1e9
    for ep in range(a.epochs):
        t0 = time.time()
        net.train()
        perm = torch.randperm(n - nval, device=dev) + nval
        tot = 0.0
        for i in range(0, n - nval, a.batch):
            idx = perm[i:i + a.batch]
            pred = torch.sigmoid(net(us_t[idx], them_t[idx]) * SCALE / K)
            loss = torch.mean((pred - tg_t[idx]) ** 2)
            opt.zero_grad()
            loss.backward()
            opt.step()
            net.clip()
            tot += loss.item() * len(idx)
        sched.step()
        net.eval()
        with torch.no_grad():
            vl = 0.0
            for i in range(0, nval, a.batch):
                pred = torch.sigmoid(net(us_t[i:i + a.batch], them_t[i:i + a.batch]) * SCALE / K)
                vl += torch.sum((pred - tg_t[i:i + a.batch]) ** 2).item()
            vl /= nval
        print('epoch %2d  train %.5f  val %.5f  (%.0f s)' % (ep + 1, tot / (n - nval), vl, time.time() - t0), flush=True)
        if vl < best:
            best = vl
            export(net, nin, a.hidden, a.game, a.out,
                   '%d positions, hidden %d, epoch %d, validation loss %.5f' % (n, a.hidden, ep + 1, vl))


if __name__ == '__main__':
    main()
