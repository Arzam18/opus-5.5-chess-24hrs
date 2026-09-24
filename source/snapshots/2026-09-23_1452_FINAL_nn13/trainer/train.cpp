// NNUE trainer: (768 -> H) x2 -> SCReLU -> 8 output buckets. Written from scratch for this project.
// Usage: train <out_prefix> <epochs> <lr> <lambda> <threads> [--init file.fnet] [--hidden H] data1.bin [data2.bin ...]
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include <random>
#include <algorithm>
#include <chrono>

#pragma pack(push, 1)
struct PackedPos {
    uint64_t occ;
    uint8_t pcs[16];
    int16_t score;
    uint8_t result;
    uint8_t stm;
    uint16_t ply;
    uint16_t pad;
};
#pragma pack(pop)

static int H = 256;
static int KB = 1;
const int NB = 8;
const int QA = 255, QB = 64;
const float EVAL_SCALE = 400.0f;

struct Params {
    std::vector<float> W1, B1, W2, B2;
    void alloc() {
        W1.assign(768 * KB * H, 0);
        B1.assign(H, 0);
        W2.assign(NB * 2 * H, 0);
        B2.assign(NB, 0);
    }
    void zero() {
        std::fill(W1.begin(), W1.end(), 0.f);
        std::fill(B1.begin(), B1.end(), 0.f);
        std::fill(W2.begin(), W2.end(), 0.f);
        std::fill(B2.begin(), B2.end(), 0.f);
    }
};

static inline int king_bucket(int relKsq) {
    if (KB == 1) return 0;
    int f = relKsq & 7, r = relKsq >> 3;
    if (f >= 4) f = 7 - f;
    if (r == 0) return f < 2 ? 0 : 1;
    if (r == 1) return 2;
    return 3;
}

static inline int feature(int persp, int pc, int sq, int ksq) {
    int c = pc / 6, pt = pc % 6;
    int kb = king_bucket(persp == 0 ? ksq : ksq ^ 56);
    if ((ksq & 7) >= 4) sq ^= 7;
    if (persp == 0) return kb * 768 + (c == 0 ? 0 : 384) + pt * 64 + sq;
    return kb * 768 + (c == 1 ? 0 : 384) + pt * 64 + (sq ^ 56);
}

static inline float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

struct Sample {
    int n;
    int fus[32], fthem[32];
    int bucket;
    float target;
};

static float LAMBDA = 0.7f;

static inline void decode(const PackedPos &p, Sample &s) {
    int sqs[32], pcs[32];
    int ksq[2] = {0, 0};
    uint64_t occ = p.occ;
    int n = 0;
    while (occ) {
        int sq = __builtin_ctzll(occ);
        occ &= occ - 1;
        int pc = (p.pcs[n / 2] >> (4 * (n & 1))) & 15;
        sqs[n] = sq;
        pcs[n] = pc;
        if (pc == 5) ksq[0] = sq;
        else if (pc == 11) ksq[1] = sq;
        n++;
    }
    int stm = p.stm;
    for (int i = 0; i < n; i++) {
        s.fus[i] = feature(stm, pcs[i], sqs[i], ksq[stm]);
        s.fthem[i] = feature(!stm, pcs[i], sqs[i], ksq[!stm]);
    }
    int i = n;
    s.n = i;
    s.bucket = std::min((i - 2) / 4, NB - 1);
    float score = p.stm == 0 ? p.score : -p.score;
    float wdl = p.result / 2.0f;
    if (p.stm == 1) wdl = 1.0f - wdl;
    s.target = LAMBDA * sigmoid(score / EVAL_SCALE) + (1.0f - LAMBDA) * wdl;
}

// forward + optional backward; returns loss
static float run_sample(const Params &P, Params *G, const Sample &s, float *accU, float *accT) {
    for (int j = 0; j < H; j++) accU[j] = accT[j] = P.B1[j];
    for (int k = 0; k < s.n; k++) {
        const float *wu = &P.W1[s.fus[k] * H];
        const float *wt = &P.W1[s.fthem[k] * H];
        for (int j = 0; j < H; j++) {
            accU[j] += wu[j];
            accT[j] += wt[j];
        }
    }
    const float *w2 = &P.W2[s.bucket * 2 * H];
    float out = P.B2[s.bucket];
    for (int j = 0; j < H; j++) {
        float u = std::min(std::max(accU[j], 0.f), 1.f);
        float t = std::min(std::max(accT[j], 0.f), 1.f);
        out += w2[j] * u * u + w2[H + j] * t * t;
    }
    float p = sigmoid(out);
    float err = p - s.target;
    float loss = err * err;
    if (!G) return loss;
    float g = 2.f * err * p * (1.f - p);
    float *gw2 = &G->W2[s.bucket * 2 * H];
    G->B2[s.bucket] += g;
    // reuse acc arrays to hold gradient wrt accumulators
    for (int j = 0; j < H; j++) {
        float a = accU[j], b = accT[j];
        float u = std::min(std::max(a, 0.f), 1.f);
        float t = std::min(std::max(b, 0.f), 1.f);
        gw2[j] += g * u * u;
        gw2[H + j] += g * t * t;
        accU[j] = (a > 0.f && a < 1.f) ? g * w2[j] * 2.f * a : 0.f;
        accT[j] = (b > 0.f && b < 1.f) ? g * w2[H + j] * 2.f * b : 0.f;
        G->B1[j] += accU[j] + accT[j];
    }
    for (int k = 0; k < s.n; k++) {
        float *gu = &G->W1[s.fus[k] * H];
        float *gt = &G->W1[s.fthem[k] * H];
        for (int j = 0; j < H; j++) {
            gu[j] += accU[j];
            gt[j] += accT[j];
        }
    }
    return loss;
}

static void save_quantized(const Params &P, const std::string &path) {
    FILE *f = fopen(path.c_str(), "wb");
    std::vector<int16_t> w1(768 * KB * H), b1(H), w2(NB * 2 * H);
    std::vector<int32_t> b2(NB);
    for (size_t i = 0; i < w1.size(); i++) w1[i] = (int16_t)std::lround(std::clamp(P.W1[i] * QA, -32767.f, 32767.f));
    for (int i = 0; i < H; i++) b1[i] = (int16_t)std::lround(std::clamp(P.B1[i] * QA, -32767.f, 32767.f));
    for (size_t i = 0; i < w2.size(); i++) w2[i] = (int16_t)std::lround(std::clamp(P.W2[i] * QB, -127.f, 127.f));
    for (int i = 0; i < NB; i++) b2[i] = (int32_t)std::lround(P.B2[i] * QA * QB);
    fwrite(w1.data(), 2, w1.size(), f);
    fwrite(b1.data(), 2, b1.size(), f);
    fwrite(w2.data(), 2, w2.size(), f);
    fwrite(b2.data(), 4, b2.size(), f);
    fclose(f);
}

static void save_float(const Params &P, const std::string &path) {
    FILE *f = fopen(path.c_str(), "wb");
    fwrite(&H, 4, 1, f);
    fwrite(P.W1.data(), 4, P.W1.size(), f);
    fwrite(P.B1.data(), 4, P.B1.size(), f);
    fwrite(P.W2.data(), 4, P.W2.size(), f);
    fwrite(P.B2.data(), 4, P.B2.size(), f);
    fclose(f);
}

static bool load_float(Params &P, const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    int h;
    if (fread(&h, 4, 1, f) != 1 || h != H) { fclose(f); return false; }
    size_t ok = fread(P.W1.data(), 4, P.W1.size(), f) + fread(P.B1.data(), 4, P.B1.size(), f) +
                fread(P.W2.data(), 4, P.W2.size(), f) + fread(P.B2.data(), 4, P.B2.size(), f);
    fclose(f);
    return ok == P.W1.size() + P.B1.size() + P.W2.size() + P.B2.size();
}

int main(int argc, char **argv) {
    if (argc < 7) {
        printf("usage: train <out_prefix> <epochs> <lr> <lambda> <threads> [--init f] [--hidden H] [--batch B] data...\n");
        return 1;
    }
    std::string outPrefix = argv[1];
    int epochs = atoi(argv[2]);
    float lr0 = (float)atof(argv[3]);
    LAMBDA = (float)atof(argv[4]);
    int T = atoi(argv[5]);
    std::string initFile;
    int B = 16384;
    int dumpN = 0;
    std::vector<std::string> files;
    for (int i = 6; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--init") initFile = argv[++i];
        else if (a == "--hidden") H = atoi(argv[++i]);
        else if (a == "--kb") KB = atoi(argv[++i]);
        else if (a == "--batch") B = atoi(argv[++i]);
        else if (a == "--dump") dumpN = atoi(argv[++i]);
        else files.push_back(a);
    }

    std::vector<PackedPos> data;
    for (auto &fn : files) {
        FILE *f = fopen(fn.c_str(), "rb");
        if (!f) { printf("cannot open %s\n", fn.c_str()); continue; }
        fseek(f, 0, SEEK_END);
        long long sz = _ftelli64(f);
        fseek(f, 0, SEEK_SET);
        size_t n = sz / sizeof(PackedPos);
        size_t old = data.size();
        data.resize(old + n);
        size_t got = fread(data.data() + old, sizeof(PackedPos), n, f);
        data.resize(old + got);
        fclose(f);
        printf("loaded %s: %zu positions\n", fn.c_str(), got);
    }
    {
        size_t cnt[3] = {0, 0, 0};
        for (auto &p : data) cnt[p.result < 3 ? p.result : 1]++;
        printf("results: white %.3f draw %.3f black %.3f\n", cnt[2] / (double)data.size(), cnt[1] / (double)data.size(),
               cnt[0] / (double)data.size());
    }
    printf("total %zu positions, H=%d, lambda=%.2f, lr=%g, epochs=%d, batch=%d, threads=%d\n", data.size(), H, LAMBDA, lr0, epochs, B, T);
    fflush(stdout);
    if (dumpN > 0) {
        Params Q;
        Q.alloc();
        if (!load_float(Q, initFile)) { printf("need --init\n"); return 1; }
        std::vector<float> au(H), at(H);
        for (int i = 0; i < dumpN && i < (int)data.size(); i++) {
            Sample s;
            decode(data[i], s);
            run_sample(Q, nullptr, s, au.data(), at.data());
            const float *w2 = &Q.W2[s.bucket * 2 * H];
            float out = Q.B2[s.bucket];
            for (int j = 0; j < H; j++) {
                float u = std::min(std::max(au[j], 0.f), 1.f), t = std::min(std::max(at[j], 0.f), 1.f);
                out += w2[j] * u * u + w2[H + j] * t * t;
            }
            printf("%d float eval %.1f\n", i, out * EVAL_SCALE);
        }
        return 0;
    }
    if (data.size() < 100000) { printf("not enough data\n"); return 1; }

    std::mt19937_64 rng(12345);
    std::shuffle(data.begin(), data.end(), rng);
    size_t nVal = std::min<size_t>(200000, data.size() / 50);
    size_t nTrain = data.size() - nVal;

    Params P, M, V;
    P.alloc();
    M.alloc();
    V.alloc();
    if (!initFile.empty() && load_float(P, initFile)) {
        printf("initialized from %s\n", initFile.c_str());
    } else {
        std::normal_distribution<float> n1(0.f, 0.08f), n2(0.f, 0.05f);
        for (auto &w : P.W1) w = n1(rng);
        for (auto &b : P.B1) b = 0.05f;
        for (auto &w : P.W2) w = n2(rng);
    }
    std::vector<Params> grads(T);
    for (auto &g : grads) g.alloc();

    size_t batchesPerEpoch = nTrain / B;
    size_t totalSteps = batchesPerEpoch * epochs;
    size_t step = 0;
    const float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
    std::vector<uint32_t> idx(nTrain);
    for (size_t i = 0; i < nTrain; i++) idx[i] = (uint32_t)i;

    auto valLoss = [&]() {
        std::vector<double> ls(T, 0);
        std::vector<std::thread> th;
        for (int t = 0; t < T; t++)
            th.emplace_back([&, t]() {
                std::vector<float> au(H), at(H);
                Sample s;
                for (size_t i = nTrain + t; i < data.size(); i += T) {
                    decode(data[i], s);
                    ls[t] += run_sample(P, nullptr, s, au.data(), at.data());
                }
            });
        for (auto &x : th) x.join();
        double sum = 0;
        for (double l : ls) sum += l;
        return sum / nVal;
    };
    printf("initial val loss %.6f\n", valLoss());
    fflush(stdout);

    auto t0 = std::chrono::steady_clock::now();
    for (int ep = 0; ep < epochs; ep++) {
        std::shuffle(idx.begin(), idx.end(), rng);
        double epLoss = 0;
        for (size_t bi = 0; bi < batchesPerEpoch; bi++) {
            float progress = (float)step / (float)totalSteps;
            float lr = lr0 * (0.01f + 0.99f * 0.5f * (1.f + std::cos(3.14159265f * progress)));
            std::vector<double> ls(T, 0);
            std::vector<std::thread> th;
            for (int t = 0; t < T; t++)
                th.emplace_back([&, t]() {
                    Params &G = grads[t];
                    G.zero();
                    std::vector<float> au(H), at(H);
                    Sample s;
                    for (size_t i = bi * B + t; i < (bi + 1) * B; i += T) {
                        decode(data[idx[i]], s);
                        ls[t] += run_sample(P, &G, s, au.data(), at.data());
                    }
                });
            for (auto &x : th) x.join();
            for (double l : ls) epLoss += l;
            step++;
            float bc1 = 1.f - std::pow(b1, (float)step), bc2 = 1.f - std::pow(b2, (float)step);
            float invB = 1.0f / B;
            // reduce + Adam, parallel over parameter ranges
            auto adam = [&](std::vector<float> Params::*member, float clipv) {
                size_t n = (P.*member).size();
                std::vector<std::thread> th2;
                for (int t = 0; t < T; t++)
                    th2.emplace_back([&, t]() {
                        size_t lo = n * t / T, hi = n * (t + 1) / T;
                        float *p = (P.*member).data(), *m = (M.*member).data(), *v = (V.*member).data();
                        for (size_t i = lo; i < hi; i++) {
                            float g = 0;
                            for (int k = 0; k < T; k++) g += (grads[k].*member)[i];
                            g *= invB;
                            m[i] = b1 * m[i] + (1 - b1) * g;
                            v[i] = b2 * v[i] + (1 - b2) * g * g;
                            p[i] -= lr * (m[i] / bc1) / (std::sqrt(v[i] / bc2) + eps);
                            if (clipv > 0) p[i] = std::clamp(p[i], -clipv, clipv);
                        }
                    });
                for (auto &x : th2) x.join();
            };
            adam(&Params::W1, 0.f);
            adam(&Params::B1, 0.f);
            adam(&Params::W2, 1.98f);
            adam(&Params::B2, 0.f);
        }
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("epoch %d train loss %.6f val loss %.6f  time %.0fs\n", ep + 1, epLoss / (batchesPerEpoch * B), valLoss(), el);
        fflush(stdout);
        save_float(P, outPrefix + ".fnet");
        save_quantized(P, outPrefix + ".nnue");
    }
    return 0;
}
