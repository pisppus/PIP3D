#include <map>

#include "Palette.hpp"

namespace pip3D
{
    namespace Bake
    {
        namespace Palette
        {
            uint32_t quantizeBlockK(const uint16_t *vals, uint32_t n, uint32_t K, uint16_t *palOut,
                                    uint8_t *idxOut, int32_t ax, int32_t ay)
            {
                if (K > kMaxPal)
                    K = kMaxPal;
                std::map<uint16_t, uint32_t> uniq;
                for (uint32_t i = 0; i < n; ++i)
                    ++uniq[vals[i]];
                uint32_t palSize = 0;
                if (uniq.size() <= K)
                {
                    for (const auto &kv : uniq)
                    {
                        palOut[palSize] = kv.first;
                        ++palSize;
                    }
                    {
                        const auto beginIt = uniq.begin();
                        for (uint32_t i = 0; i < n; ++i)
                            idxOut[i] = static_cast<uint8_t>(std::distance(beginIt, uniq.find(vals[i])));
                    }
                    for (uint32_t pi = palSize; pi < K; ++pi)
                        palOut[pi] = palOut[palSize > 0 ? palSize - 1 : 0];
                    return palSize;
                }
                std::vector<BlockPt> pts(n);
                for (uint32_t i = 0; i < n; ++i)
                {
                    pts[i].r = static_cast<uint8_t>((vals[i] >> 11) & 0x1F);
                    pts[i].g = static_cast<uint8_t>((vals[i] >> 5) & 0x3F);
                    pts[i].b = static_cast<uint8_t>(vals[i] & 0x1F);
                }
                std::vector<std::vector<BlockPt>> boxes(1, pts);
                while (boxes.size() < K)
                {
                    size_t splitAt = SIZE_MAX;
                    int splitCh = -1;
                    int bestRange = 0;
                    for (size_t bi = 0; bi < boxes.size(); ++bi)
                    {
                        if (boxes[bi].size() < 2)
                            continue;
                        int rmn = 255, rmx = 0, gmn = 255, gmx = 0, bmn = 255, bmx = 0;
                        for (const BlockPt &p : boxes[bi])
                        {
                            rmn = std::min(rmn, (int)p.r);
                            rmx = std::max(rmx, (int)p.r);
                            gmn = std::min(gmn, (int)p.g);
                            gmx = std::max(gmx, (int)p.g);
                            bmn = std::min(bmn, (int)p.b);
                            bmx = std::max(bmx, (int)p.b);
                        }
                        const int rr = (rmx - rmn) * 3;
                        const int gr = (gmx - gmn) * 4;
                        const int br = (bmx - bmn) * 2;
                        const int m = std::max(rr, std::max(gr, br));
                        if (m > bestRange)
                        {
                            bestRange = m;
                            splitAt = bi;
                            splitCh = (m == rr) ? 0 : (m == gr ? 1 : 2);
                        }
                    }
                    if (splitAt == SIZE_MAX)
                        break;
                    auto &box = boxes[splitAt];
                    const int ch = splitCh;
                    std::sort(box.begin(), box.end(),
                              [ch](const BlockPt &a, const BlockPt &b)
                              {
                                  return ch == 0 ? a.r < b.r : (ch == 1 ? a.g < b.g : a.b < b.b);
                              });
                    const size_t half = box.size() / 2;
                    std::vector<BlockPt> right(box.begin() + static_cast<long>(half), box.end());
                    box.resize(half);
                    boxes.push_back(std::move(right));
                }
                for (size_t bi = 0; bi < boxes.size() && bi < K; ++bi)
                {
                    const std::vector<BlockPt> &box = boxes[bi];
                    if (box.empty())
                    {
                        palOut[bi] = 0;
                        continue;
                    }
                    int sr = 0, sg = 0, sb = 0;
                    for (const BlockPt &p : box)
                    {
                        sr += p.r;
                        sg += p.g;
                        sb += p.b;
                    }
                    const uint32_t r5 = static_cast<uint32_t>(std::lround(static_cast<double>(sr) / box.size()));
                    const uint32_t g6 = static_cast<uint32_t>(std::lround(static_cast<double>(sg) / box.size()));
                    const uint32_t b5 = static_cast<uint32_t>(std::lround(static_cast<double>(sb) / box.size()));
                    palOut[bi] = static_cast<uint16_t>(((r5 & 0x1F) << 11) | ((g6 & 0x3F) << 5) | (b5 & 0x1F));
                    if (bi + 1 > palSize)
                        palSize = static_cast<uint32_t>(bi) + 1u;
                }
                for (uint32_t it = 0; it < 2 && palSize > 0; ++it)
                {
                    int cr[kMaxPal], cg[kMaxPal], cb[kMaxPal];
                    for (uint32_t pi = 0; pi < palSize; ++pi)
                    {
                        cr[pi] = (palOut[pi] >> 11) & 0x1F;
                        cg[pi] = (palOut[pi] >> 5) & 0x3F;
                        cb[pi] = palOut[pi] & 0x1F;
                    }
                    long ar[kMaxPal] = {0}, ag[kMaxPal] = {0}, ab[kMaxPal] = {0};
                    uint32_t ac[kMaxPal] = {0};
                    bool changed = false;
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        int best = 0;
                        int bestDist = 1 << 30;
                        for (uint32_t pi = 0; pi < palSize; ++pi)
                        {
                            const int dist = palDist(static_cast<uint16_t>((cr[pi] << 11) | (cg[pi] << 5) | cb[pi]), vals[i]);
                            if (dist < bestDist)
                            {
                                bestDist = dist;
                                best = static_cast<int>(pi);
                            }
                        }
                        ar[best] += (vals[i] >> 11) & 0x1F;
                        ag[best] += (vals[i] >> 5) & 0x3F;
                        ab[best] += vals[i] & 0x1F;
                        ++ac[best];
                    }
                    for (uint32_t pi = 0; pi < palSize; ++pi)
                    {
                        if (ac[pi] == 0)
                            continue;
                        const uint32_t r5 = static_cast<uint32_t>(std::lround(static_cast<double>(ar[pi]) / ac[pi]));
                        const uint32_t g6 = static_cast<uint32_t>(std::lround(static_cast<double>(ag[pi]) / ac[pi]));
                        const uint32_t b5 = static_cast<uint32_t>(std::lround(static_cast<double>(ab[pi]) / ac[pi]));
                        const uint16_t nv = static_cast<uint16_t>(((r5 & 0x1F) << 11) | ((g6 & 0x3F) << 5) | (b5 & 0x1F));
                        if (nv != palOut[pi])
                        {
                            palOut[pi] = nv;
                            changed = true;
                        }
                    }
                    if (!changed)
                        break;
                }
                const Noise::BlueNoise &bn = Noise::blueNoise();
                for (uint32_t i = 0; i < n; ++i)
                {
                    int b1 = 0, b2 = -1;
                    double d1 = 1 << 30, d2 = static_cast<double>(1 << 30) * 4.0;
                    for (uint32_t pi = 0; pi < palSize; ++pi)
                    {
                        const double dist = palDist(palOut[pi], vals[i]);
                        if (dist < d1)
                        {
                            d2 = d1;
                            b2 = b1;
                            d1 = dist;
                            b1 = static_cast<int>(pi);
                        }
                        else if (dist < d2)
                        {
                            d2 = dist;
                            b2 = static_cast<int>(pi);
                        }
                    }
                    const float bn01 = bn.atCh(ax + static_cast<int32_t>(i & 7), ay + static_cast<int32_t>(i >> 3), 1);
                    idxOut[i] = static_cast<uint8_t>(ditheredIndex(b1, b2, d1, d2, bn01));
                }
                {
                    uint16_t distinct[kMaxPal];
                    int32_t oldToNew[kMaxPal];
                    uint32_t m = 0;
                    for (uint32_t pi = 0; pi < palSize; ++pi)
                    {
                        int32_t found = -1;
                        for (uint32_t pj = 0; pj < m; ++pj)
                            if (distinct[pj] == palOut[pi])
                            {
                                found = static_cast<int32_t>(pj);
                                break;
                            }
                        if (found >= 0)
                            oldToNew[pi] = found;
                        else
                        {
                            distinct[m] = palOut[pi];
                            oldToNew[pi] = static_cast<int32_t>(m);
                            ++m;
                        }
                    }
                    for (uint32_t i = 0; i < n; ++i)
                        idxOut[i] = static_cast<uint8_t>(oldToNew[idxOut[i]]);
                    for (uint32_t pi = 0; pi < m; ++pi)
                        palOut[pi] = distinct[pi];
                    palSize = m;
                }
                for (uint32_t pi = palSize; pi < K; ++pi)
                    palOut[pi] = palOut[palSize > 0 ? palSize - 1 : 0];
                return palSize;
            }

            void fitRampBlock(const uint16_t *vals, uint32_t n, int32_t ax, int32_t ay, RampFit &out)
            {
                double mr = 0, mg = 0, mb = 0;
                for (uint32_t i = 0; i < n; ++i)
                {
                    mr += static_cast<double>((vals[i] >> 11) & 0x1F) * (1.0 / 31.0);
                    mg += static_cast<double>((vals[i] >> 5) & 0x3F) * (1.0 / 63.0);
                    mb += static_cast<double>(vals[i] & 0x1F) * (1.0 / 31.0);
                }
                mr /= n;
                mg /= n;
                mb /= n;
                double cxx = 0, cxy = 0, cxz = 0, cyy = 0, cyz = 0, czz = 0;
                for (uint32_t i = 0; i < n; ++i)
                {
                    const double dx = static_cast<double>((vals[i] >> 11) & 0x1F) * (1.0 / 31.0) - mr;
                    const double dy = static_cast<double>((vals[i] >> 5) & 0x3F) * (1.0 / 63.0) - mg;
                    const double dz = static_cast<double>(vals[i] & 0x1F) * (1.0 / 31.0) - mb;
                    cxx += dx * dx;
                    cxy += dx * dy;
                    cxz += dx * dz;
                    cyy += dy * dy;
                    cyz += dy * dz;
                    czz += dz * dz;
                }
                double vx = 1.0, vy = 1.0, vz = 1.0;
                for (int it = 0; it < 12; ++it)
                {
                    const double nx = cxx * vx + cxy * vy + cxz * vz;
                    const double ny = cxy * vx + cyy * vy + cyz * vz;
                    const double nz = cxz * vx + cyz * vy + czz * vz;
                    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (len < 1e-12)
                        break;
                    vx = nx / len;
                    vy = ny / len;
                    vz = nz / len;
                }
                double tmin = 1e30, tmax = -1e30;
                for (uint32_t i = 0; i < n; ++i)
                {
                    const double dx = static_cast<double>((vals[i] >> 11) & 0x1F) * (1.0 / 31.0) - mr;
                    const double dy = static_cast<double>((vals[i] >> 5) & 0x3F) * (1.0 / 63.0) - mg;
                    const double dz = static_cast<double>(vals[i] & 0x1F) * (1.0 / 31.0) - mb;
                    const double t = dx * vx + dy * vy + dz * vz;
                    tmin = std::fmin(tmin, t);
                    tmax = std::fmax(tmax, t);
                }
                auto quantCh = [](double v, double levels) -> uint32_t
                {
                    const int32_t q = static_cast<int32_t>(v * (levels - 1.0) + 0.5);
                    return static_cast<uint32_t>(q < 0 ? 0
                                                       : (q > static_cast<int32_t>(levels - 1.0) ? static_cast<int32_t>(levels - 1.0)
                                                                                                 : q));
                };
                const uint32_t e0r = quantCh(mr + vx * tmin, 32.0);
                const uint32_t e0g = quantCh(mg + vy * tmin, 64.0);
                const uint32_t e0b = quantCh(mb + vz * tmin, 32.0);
                const uint32_t e1r = quantCh(mr + vx * tmax, 32.0);
                const uint32_t e1g = quantCh(mg + vy * tmax, 64.0);
                const uint32_t e1b = quantCh(mb + vz * tmax, 32.0);
                out.e0 = static_cast<uint16_t>((e0r << 11) | (e0g << 5) | e0b);
                out.e1 = static_cast<uint16_t>((e1r << 11) | (e1g << 5) | e1b);
                const double ax_ = (static_cast<int>(e1r) - static_cast<int>(e0r)) * 3.0;
                const double ay_ = (static_cast<int>(e1g) - static_cast<int>(e0g)) * 4.0;
                const double az_ = (static_cast<int>(e1b) - static_cast<int>(e0b)) * 2.0;
                const double denom = ax_ * (static_cast<int>(e1r) - static_cast<int>(e0r)) +
                                     ay_ * (static_cast<int>(e1g) - static_cast<int>(e0g)) +
                                     az_ * (static_cast<int>(e1b) - static_cast<int>(e0b));
                const Noise::BlueNoise &bn = Noise::blueNoise();
                auto lerp7 = [](uint32_t a, uint32_t b, uint32_t t) -> int
                {
                    const int diff = static_cast<int>(b) - static_cast<int>(a);
                    const int mag = (((diff < 0 ? -diff : diff) * static_cast<int>(t) * 18724) + 65536) >> 17;
                    return (diff < 0) ? static_cast<int>(a) - mag : static_cast<int>(a) + mag;
                };

                auto evaluate = [&](uint32_t r0, uint32_t g0, uint32_t b0, uint32_t r1, uint32_t g1, uint32_t b1,
                                    uint8_t *idxOut, float &maxD, double &avgD)
                {
                    const double ax_ = (static_cast<int>(r1) - static_cast<int>(r0)) * 3.0;
                    const double ay_ = (static_cast<int>(g1) - static_cast<int>(g0)) * 4.0;
                    const double az_ = (static_cast<int>(b1) - static_cast<int>(b0)) * 2.0;
                    const double denom = ax_ * (static_cast<int>(r1) - static_cast<int>(r0)) +
                                         ay_ * (static_cast<int>(g1) - static_cast<int>(g0)) +
                                         az_ * (static_cast<int>(b1) - static_cast<int>(b0));
                    maxD = 0.0f;
                    double sumD = 0.0;
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        const int vr = static_cast<int>((vals[i] >> 11) & 0x1F);
                        const int vg = static_cast<int>((vals[i] >> 5) & 0x3F);
                        const int vb = static_cast<int>(vals[i] & 0x1F);
                        uint32_t idq = 0;
                        if (denom > 1e-9)
                        {
                            const double t = (static_cast<double>(vr - static_cast<int>(r0)) * ax_ +
                                              static_cast<double>(vg - static_cast<int>(g0)) * ay_ +
                                              static_cast<double>(vb - static_cast<int>(b0)) * az_) /
                                             denom;
                            const double tf = std::fmax(0.0, std::fmin(1.0, t)) * 7.0;
                            const float bn01 = bn.atCh(ax + static_cast<int32_t>(i & 7), ay + static_cast<int32_t>(i >> 3), 1);
                            const double dj = tf + (static_cast<double>(bn01) - 0.5);
                            idq = static_cast<uint32_t>(std::fmax(0.0, std::fmin(7.0, std::floor(dj + 0.5))));
                        }
                        const int rr = lerp7(r0, r1, idq);
                        const int rg = lerp7(g0, g1, idq);
                        const int rb = lerp7(b0, b1, idq);
                        const uint32_t dr = static_cast<uint32_t>(std::abs(rr - vr));
                        const uint32_t dg = static_cast<uint32_t>(std::abs(rg - vg));
                        const uint32_t db = static_cast<uint32_t>(std::abs(rb - vb));
                        const uint32_t dm = std::max(dr, std::max(dg, db));
                        if (static_cast<float>(dm) > maxD)
                            maxD = static_cast<float>(dm);
                        sumD += static_cast<double>(dm);
                        if (idxOut)
                            idxOut[i] = static_cast<uint8_t>(idq);
                    }
                    avgD = sumD / n;
                };

                uint32_t r0 = e0r, g0 = e0g, b0 = e0b, r1 = e1r, g1 = e1g, b1 = e1b;
                float bestMax = 0.0f;
                double bestAvg = 0.0;
                evaluate(r0, g0, b0, r1, g1, b1, out.idx, bestMax, bestAvg);
                for (int sweep = 0; sweep < 2; ++sweep)
                {
                    for (int which = 0; which < 2; ++which)
                    {
                        uint32_t &rr = which == 0 ? r0 : r1;
                        uint32_t &gr = which == 0 ? g0 : g1;
                        uint32_t &br = which == 0 ? b0 : b1;
                        uint32_t br0 = rr, bg0 = gr, bb0 = br;
                        for (int dr = -1; dr <= 1; ++dr)
                            for (int dg = -1; dg <= 1; ++dg)
                                for (int db = -1; db <= 1; ++db)
                                {
                                    const uint32_t cr = std::clamp(static_cast<int>(br0) + dr, 0, 31);
                                    const uint32_t cg = std::clamp(static_cast<int>(bg0) + dg, 0, 63);
                                    const uint32_t cb = std::clamp(static_cast<int>(bb0) + db, 0, 31);
                                    float m;
                                    double a;
                                    if (which == 0)
                                        evaluate(cr, cg, cb, r1, g1, b1, nullptr, m, a);
                                    else
                                        evaluate(r0, g0, b0, cr, cg, cb, nullptr, m, a);
                                    if (a + m * 0.05 < bestAvg + bestMax * 0.05)
                                    {
                                        bestAvg = a;
                                        bestMax = m;
                                        rr = cr;
                                        gr = cg;
                                        br = cb;
                                    }
                                }
                    }
                }
                evaluate(r0, g0, b0, r1, g1, b1, out.idx, bestMax, bestAvg);

                out.e0 = static_cast<uint16_t>((r0 << 11) | (g0 << 5) | b0);
                out.e1 = static_cast<uint16_t>((r1 << 11) | (g1 << 5) | b1);
                out.maxD = bestMax;
                out.avgD = static_cast<float>(bestAvg);
            }

        }
    }
}
