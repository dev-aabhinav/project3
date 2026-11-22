#pragma once

// Zipfian generator over ranks 0 .. n-1 (rank 0 = most popular).
// P(rank i) is proportional to 1 / (i + 1)^theta.
//
// Algorithm from Gray et al., "Quickly Generating Billion-Record Synthetic
// Databases" (SIGMOD 1994), the same one YCSB's ZipfianGenerator uses.
// Setup computes zeta(n, theta) once (O(n)); each sample is O(1).
// Valid for theta > 0, theta != 1.

#include <cmath>
#include <cstdint>
#include <random>

namespace bench {

class ZipfGenerator {
public:
    ZipfGenerator(std::uint64_t n, double theta, std::uint64_t seed)
        : n_(n), theta_(theta), rng_(seed), uniform_(0.0, 1.0) {
        zeta_n_ = Zeta(n_, theta_);
        double zeta_2 = Zeta(2, theta_);

        alpha_ = 1.0 / (1.0 - theta_);
        eta_ = (1.0 - std::pow(2.0 / static_cast<double>(n_), 1.0 - theta_)) /
               (1.0 - zeta_2 / zeta_n_);
    }

    std::uint64_t Next() {
        double u = uniform_(rng_);
        double uz = u * zeta_n_;

        if (uz < 1.0) {
            return 0;
        }
        if (uz < 1.0 + std::pow(0.5, theta_)) {
            return 1;
        }
        auto rank = static_cast<std::uint64_t>(
            static_cast<double>(n_) * std::pow(eta_ * u - eta_ + 1.0, alpha_));
        return rank < n_ ? rank : n_ - 1;
    }

    // Theoretical probability of rank 0 (for sanity-checking the generator).
    double TopProbability() const { return 1.0 / zeta_n_; }

private:
    static double Zeta(std::uint64_t n, double theta) {
        double sum = 0.0;
        for (std::uint64_t i = 1; i <= n; ++i) {
            sum += 1.0 / std::pow(static_cast<double>(i), theta);
        }
        return sum;
    }

    std::uint64_t n_;
    double theta_;
    double zeta_n_;
    double alpha_;
    double eta_;

    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> uniform_;
};

}  // namespace bench
