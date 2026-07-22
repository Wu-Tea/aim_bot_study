#include "control_learning/robust_ew_rls.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        using control_learning::RobustEwRls;
        RobustEwRls<4> rls;
        for (int i = 0; i < 160; ++i) {
            const std::array<double, 4> phi{
                0.04 + (i % 7) * 0.01, -0.03 + (i % 5) * 0.012,
                0.02 + (i % 3) * 0.015, -0.01 + (i % 11) * 0.006};
            const double y = 900.0 * phi[0] + 40.0 * phi[1] -
                180.0 * phi[2] + 5.0 * phi[3];
            require(rls.update(phi, y), "finite excited sample must update");
        }
        const auto learned = rls.snapshot();
        require(learned.accepted_updates == 160, "accepted counter mismatch");
        require(learned.rank >= 3, "excited samples must report useful rank");
        require(std::isfinite(learned.condition), "condition must remain finite");
        require(learned.confidence > 0.0, "accepted evidence must build confidence");

        const auto before_outlier = rls.snapshot();
        require(rls.update({0.08, -0.04, 0.03, 0.01}, 1.0e6),
                "finite outlier should be robustly down-weighted");
        require(rls.snapshot().last_weight < 0.1,
                "Huber weight must suppress a severe outlier");
        require(std::fabs(rls.snapshot().theta[0] - before_outlier.theta[0]) < 100.0,
                "outlier must not destabilize response");

        const auto before_low = rls.snapshot();
        require(!rls.update({1.0e-10, 0.0, 0.0, 0.0}, 1.0),
                "low excitation must reject");
        require(rls.snapshot().rejected_low_excitation ==
                    before_low.rejected_low_excitation + 1,
                "low excitation rejection must be counted");

        const auto before_bad = rls.snapshot();
        require(!rls.update({std::numeric_limits<double>::quiet_NaN(), 0, 0, 0}, 1),
                "non-finite sample must reject");
        require(rls.snapshot() == before_bad,
                "non-finite update must roll back every state field");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[RobustEwRlsTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
