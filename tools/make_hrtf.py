"""Spatial3D — fabrique l'HRTF intégrée (res/hrtf_kemar_48k.bin) à partir du fichier SOFA
MIT KEMAR (Gardner & Martin, MIT Media Lab, 1994 — « provided without any usage restrictions »,
citation demandée). Traitements :
  1. retard commun en tête retiré (les écarts entre oreilles, l'ITD, sont conservés) ;
  2. égalisation champ diffus : moyenne de puissance sur la sphère (pondérée par l'angle solide),
     lissée au 1/3 d'octave, inversée (bornée à ±12 dB) et appliquée en phase minimale — le timbre
     du haut-parleur de mesure et la résonance du conduit de KEMAR disparaissent, la direction reste ;
  3. passage à 48 kHz (polyphase), fenêtrage de fin, quantification 16 bits.
Usage : python3 make_hrtf.py <MIT_KEMAR_normal_pinna.sofa> <sortie.bin>
"""
import sys, struct
import numpy as np
import h5py
from scipy.signal import resample_poly

def main(src, dst):
    h = h5py.File(src, 'r')
    ir = np.array(h['Data.IR'], dtype=np.float64)          # (M, 2, N) à 44,1 kHz
    pos = np.array(h['SourcePosition'], dtype=np.float64)  # az (vers la gauche), el, distance
    fs = float(h['Data.SamplingRate'][0])
    M, _, N = ir.shape

    # 1. retard commun en tête
    env = np.abs(ir).max(axis=(0, 1))
    onset = int(np.argmax(env > env.max() * 0.05))
    ir = ir[:, :, max(0, onset - 4):]
    N = ir.shape[2]

    # 2. égalisation champ diffus
    nfft = 4096
    H = np.fft.rfft(ir, nfft, axis=2)
    w = np.cos(np.radians(pos[:, 1]))                     # angle solide ~ cos(élévation)
    # chaque anneau d'élévation est échantillonné différemment : poids par point = cos(el) / nb de points
    for el in np.unique(pos[:, 1]):
        sel = pos[:, 1] == el
        w[sel] = max(np.cos(np.radians(el)), 0.02) / sel.sum()
    w /= w.sum()
    P = (w[:, None, None] * np.abs(H) ** 2).sum(axis=0).mean(axis=0)   # moyenne des deux oreilles
    f = np.fft.rfftfreq(nfft, 1 / fs)
    sm = np.empty_like(P)
    for k, fk in enumerate(f):                             # lissage 1/3 d'octave
        lo, hi = fk / 2 ** (1 / 6), fk * 2 ** (1 / 6)
        sel = (f >= lo) & (f <= hi)
        sm[k] = P[sel].mean() if sel.any() else P[k]
    mag = 1 / np.sqrt(sm)
    band = (f > 300) & (f < 8000)
    mag /= np.sqrt(np.mean(mag[band] ** 2))                # gain de référence ~ moyen
    mag = np.clip(mag, 10 ** (-12 / 20), 10 ** (12 / 20))
    # au-dessus de 16 kHz et sous 60 Hz : pas de correction (bruit de mesure, limites du haut-parleur)
    mag[f > 16000] = mag[np.searchsorted(f, 16000)]
    mag[f < 60] = mag[np.searchsorted(f, 60)]
    # filtre à phase minimale (cepstre)
    logm = np.log(np.concatenate([mag, mag[-2:0:-1]]))
    cep = np.fft.ifft(logm).real
    fold = np.zeros_like(cep); fold[0] = cep[0]; fold[1:nfft // 2] = 2 * cep[1:nfft // 2]; fold[nfft // 2] = cep[nfft // 2]
    eq = np.fft.ifft(np.exp(np.fft.fft(fold))).real[:1024]
    out = np.array([[np.convolve(ir[m, e], eq) for e in range(2)] for m in range(M)])

    # 3. 48 kHz, longueur utile, fenêtre de fin
    out = resample_poly(out, 160, 147, axis=2)
    energy = np.cumsum((out ** 2).sum(axis=(0, 1)))
    taps = int(np.searchsorted(energy, energy[-1] * 0.99995)) + 1
    taps = int(min(512, max(192, (taps + 31) // 32 * 32)))
    out = out[:, :, :taps]
    fade = 32
    out[:, :, -fade:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, fade)))[None, None, :]
    # niveau : moyenne de puissance champ diffus = 1 (même sonie qu'aux enceintes)
    p = (w[:, None] * (out ** 2).sum(axis=2)).sum(axis=0).mean()
    out /= np.sqrt(p)
    scale = np.abs(out).max() / 32767.0
    q = np.round(out / scale).astype('<i2')
    with open(dst, 'wb') as fo:
        fo.write(b'S3DH')
        fo.write(struct.pack('<IIIIf', 1, M, taps, 48000, scale))
        for m in range(M):
            fo.write(struct.pack('<hh', int(round(pos[m, 0] * 10)), int(round(pos[m, 1] * 10))))
            fo.write(q[m, 0].tobytes()); fo.write(q[m, 1].tobytes())
    print(f'{M} directions, {taps} points a 48 kHz, onset retire {onset}, {dst}')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
