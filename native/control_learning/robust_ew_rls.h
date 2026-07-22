#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace control_learning {

template <std::size_t FeatureCount>
class RobustEwRls {
public:
    using Vector = std::array<double, FeatureCount>;
    using Matrix = std::array<Vector, FeatureCount>;

    struct State {
        Vector theta{};
        Matrix covariance{};
        Matrix information{};
        double residual_scale = 25.0;
        double last_residual = 0.0;
        double last_weight = 1.0;
        double excitation = 0.0;
        double condition = 1.0;
        double confidence = 0.0;
        std::uint64_t accepted_updates = 0;
        std::uint64_t rejected_low_excitation = 0;
        std::size_t rank = 0;

        bool operator==(const State& other) const noexcept {
            return theta == other.theta && covariance == other.covariance &&
                information == other.information &&
                residual_scale == other.residual_scale &&
                last_residual == other.last_residual &&
                last_weight == other.last_weight && excitation == other.excitation &&
                condition == other.condition && confidence == other.confidence &&
                accepted_updates == other.accepted_updates &&
                rejected_low_excitation == other.rejected_low_excitation &&
                rank == other.rank;
        }
    };

    RobustEwRls() noexcept { reset(); }

    void reset() noexcept {
        state_ = {};
        state_.residual_scale = 25.0;
        for (std::size_t i = 0; i < FeatureCount; ++i) {
            state_.covariance[i][i] = 10'000.0;
        }
    }

    bool update(const Vector& phi, double output) noexcept {
        if (!std::isfinite(output)) return false;
        double excitation = 0.0;
        for (double value : phi) {
            if (!std::isfinite(value)) return false;
            excitation += value * value;
        }
        if (excitation < kMinimumExcitationSquared) {
            State candidate = state_;
            ++candidate.rejected_low_excitation;
            candidate.excitation = std::sqrt(excitation);
            state_ = candidate;
            return false;
        }

        State candidate = state_;
        candidate.excitation = std::sqrt(excitation);
        double predicted = 0.0;
        for (std::size_t i = 0; i < FeatureCount; ++i) {
            predicted += state_.theta[i] * phi[i];
        }
        const double residual = output - predicted;
        const double robust_scale = std::max(1.0e-3, state_.residual_scale);
        const double huber_limit = 2.5 * robust_scale;
        const double weight = std::min(1.0, huber_limit /
            std::max(std::fabs(residual), 1.0e-12));

        Matrix prior_covariance = state_.covariance;
        for (auto& row : prior_covariance) {
            for (double& value : row) value /= kForgetting;
        }
        Vector covariance_phi{};
        double projected_information = 0.0;
        for (std::size_t row = 0; row < FeatureCount; ++row) {
            for (std::size_t column = 0; column < FeatureCount; ++column) {
                covariance_phi[row] += prior_covariance[row][column] * phi[column];
            }
            projected_information += phi[row] * covariance_phi[row];
        }
        const double denominator = (1.0 / std::max(weight, 1.0e-9)) +
            projected_information;
        if (!std::isfinite(denominator) || denominator <= 1.0e-12) return false;

        Vector gain{};
        for (std::size_t i = 0; i < FeatureCount; ++i) {
            gain[i] = covariance_phi[i] / denominator;
            candidate.theta[i] += gain[i] * residual;
        }
        for (std::size_t row = 0; row < FeatureCount; ++row) {
            for (std::size_t column = 0; column < FeatureCount; ++column) {
                candidate.covariance[row][column] =
                    prior_covariance[row][column] - gain[row] * covariance_phi[column];
                candidate.information[row][column] =
                    kForgetting * state_.information[row][column] +
                    weight * phi[row] * phi[column];
            }
        }
        symmetrize(candidate.covariance);
        candidate.last_residual = residual;
        candidate.last_weight = weight;
        candidate.residual_scale = 0.98 * robust_scale +
            0.02 * std::min(std::fabs(residual), 10.0 * robust_scale);
        ++candidate.accepted_updates;
        update_diagnostics(candidate);
        if (!finite(candidate) || !positive_definite(candidate.covariance)) return false;
        state_ = candidate;
        return true;
    }

    const State& snapshot() const noexcept { return state_; }

private:
    static constexpr double kForgetting = 0.997;
    static constexpr double kMinimumExcitationSquared = 1.0e-8;

    static void symmetrize(Matrix& matrix) noexcept {
        for (std::size_t row = 0; row < FeatureCount; ++row) {
            for (std::size_t column = row + 1; column < FeatureCount; ++column) {
                const double value = 0.5 *
                    (matrix[row][column] + matrix[column][row]);
                matrix[row][column] = value;
                matrix[column][row] = value;
            }
        }
    }

    static bool positive_definite(const Matrix& matrix) noexcept {
        Matrix lower{};
        for (std::size_t row = 0; row < FeatureCount; ++row) {
            for (std::size_t column = 0; column <= row; ++column) {
                double value = matrix[row][column];
                for (std::size_t k = 0; k < column; ++k) {
                    value -= lower[row][k] * lower[column][k];
                }
                if (row == column) {
                    if (!std::isfinite(value) || value <= 1.0e-12) return false;
                    lower[row][column] = std::sqrt(value);
                } else {
                    lower[row][column] = value / lower[column][column];
                }
            }
        }
        return true;
    }

    static bool finite(const State& state) noexcept {
        auto finite_vector = [](const Vector& values) {
            for (double value : values) if (!std::isfinite(value)) return false;
            return true;
        };
        if (!finite_vector(state.theta)) return false;
        for (const auto& row : state.covariance) if (!finite_vector(row)) return false;
        for (const auto& row : state.information) if (!finite_vector(row)) return false;
        return std::isfinite(state.residual_scale) &&
            std::isfinite(state.last_residual) && std::isfinite(state.last_weight) &&
            std::isfinite(state.excitation) && std::isfinite(state.condition) &&
            std::isfinite(state.confidence);
    }

    static void update_diagnostics(State& state) noexcept {
        double maximum = 0.0;
        double minimum = std::numeric_limits<double>::max();
        std::size_t rank = 0;
        for (std::size_t i = 0; i < FeatureCount; ++i) {
            const double diagonal = std::max(0.0, state.information[i][i]);
            maximum = std::max(maximum, diagonal);
            if (diagonal > 1.0e-5) {
                minimum = std::min(minimum, diagonal);
                ++rank;
            }
        }
        state.rank = rank;
        state.condition = rank == 0 ? 1.0 : maximum / std::max(minimum, 1.0e-12);
        const double evidence = std::min(1.0,
            static_cast<double>(state.accepted_updates) / 120.0);
        const double rank_factor = static_cast<double>(rank) / FeatureCount;
        const double condition_factor = 1.0 /
            (1.0 + std::max(0.0, state.condition - 1.0) / 100.0);
        state.confidence = std::clamp(evidence * rank_factor * condition_factor,
                                      0.0, 1.0);
    }

    State state_{};
};

}  // namespace control_learning
