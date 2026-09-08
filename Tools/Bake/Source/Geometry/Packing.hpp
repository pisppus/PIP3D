#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace pip3D
{
    namespace Bake
    {
        class MaxRectsPacker
        {
        public:
            MaxRectsPacker(uint32_t w, uint32_t h)
                : m_binW(w), m_binH(h)
            {
                m_free.push_back({0, 0, w, h});
            }

            bool insert(uint32_t w, uint32_t h, uint32_t &outX, uint32_t &outY, bool &outRotated)
            {
                int bestIdx = -1;
                bool bestRot = false;
                uint32_t bestShort = 0xFFFFFFFFu;
                uint32_t bestLong = 0xFFFFFFFFu;
                uint32_t bestY = 0xFFFFFFFFu;
                uint32_t bestX = 0xFFFFFFFFu;

                for (size_t i = 0; i < m_free.size(); ++i)
                {
                    const FreeRect &r = m_free[i];
                    for (int o = 0; o < 2; ++o)
                    {
                        if (o == 1 && w == h)
                            break;
                        const uint32_t rw = (o == 1) ? h : w;
                        const uint32_t rh = (o == 1) ? w : h;
                        if (r.w < rw || r.h < rh)
                            continue;
                        const uint32_t sh = std::min(r.w - rw, r.h - rh);
                        const uint32_t lg = std::max(r.w - rw, r.h - rh);
                        if (sh < bestShort ||
                            (sh == bestShort && (lg < bestLong ||
                                                 (lg == bestLong && (r.y < bestY || (r.y == bestY && r.x < bestX))))))
                        {
                            bestShort = sh;
                            bestLong = lg;
                            bestY = r.y;
                            bestX = r.x;
                            bestIdx = static_cast<int>(i);
                            bestRot = (o == 1);
                        }
                    }
                }
                if (bestIdx < 0)
                    return false;

                const FreeRect chosen = m_free[static_cast<size_t>(bestIdx)];
                const uint32_t rw = bestRot ? h : w;
                const uint32_t rh = bestRot ? w : h;
                outX = chosen.x;
                outY = chosen.y;
                outRotated = bestRot;

                std::vector<FreeRect> next;
                next.reserve(m_free.size() + 4);
                const uint32_t px = chosen.x, py = chosen.y;
                const uint32_t pe = px + rw, pb = py + rh;
                for (const FreeRect &f : m_free)
                {
                    if (f.x >= pe || f.x + f.w <= px || f.y >= pb || f.y + f.h <= py)
                    {
                        next.push_back(f);
                        continue;
                    }
                    if (f.x < px)
                        next.push_back({f.x, f.y, px - f.x, f.h});
                    if (f.y < py)
                        next.push_back({f.x, f.y, f.w, py - f.y});
                    if (f.x + f.w > pe)
                        next.push_back({pe, f.y, f.x + f.w - pe, f.h});
                    if (f.y + f.h > pb)
                        next.push_back({f.x, pb, f.w, f.y + f.h - pb});
                }
                m_free = std::move(next);
                prune();
                return true;
            }

        private:
            struct FreeRect
            {
                uint32_t x, y, w, h;
            };

            bool contains(const FreeRect &a, const FreeRect &b) const
            {
                return a.x <= b.x && a.y <= b.y && a.x + a.w >= b.x + b.w && a.y + a.h >= b.y + b.h;
            }

            void prune()
            {
                for (size_t i = 0; i < m_free.size();)
                {
                    bool erasedI = false;
                    for (size_t j = i + 1; j < m_free.size();)
                    {
                        if (contains(m_free[i], m_free[j]))
                        {
                            m_free.erase(m_free.begin() + static_cast<long>(j));
                            continue;
                        }
                        if (contains(m_free[j], m_free[i]))
                        {
                            m_free.erase(m_free.begin() + static_cast<long>(i));
                            erasedI = true;
                            break;
                        }
                        ++j;
                    }
                    if (!erasedI)
                        ++i;
                }
            }

            uint32_t m_binW, m_binH;
            std::vector<FreeRect> m_free;
        };
    }
}
